#include "platform/psp/renderer/PspGl.h"

#include <cstdlib>
#include <cstring>

#include "core/EngineDebug.h"
#include "core/EngineMemory.h"
#include "graphics/TextureExpand.h"
#include "Macros.h"
#include "platform/Platform.h"
#include "platform/psp/UtilityDialog.h"
#include "PlatformConstants.h"

extern "C" {
#include <GL/gl.h>
#include <GLES/egl.h>
}

namespace
{
    // What video memory has left once the two display buffers and the depth
    // buffer are placed, which is all this backend can spend on textures.
    uint32_t VramTextureBudget()
    {
        const uint32_t display = GFX_PSP_BUFFER_STRIDE * GFX_SCREEN_HEIGHT * GFX_PSP_DISPLAY_BPP;
        const uint32_t depth = GFX_PSP_BUFFER_STRIDE * GFX_SCREEN_HEIGHT * GFX_PSP_DEPTH_BPP;
        const uint32_t spent = (display * GFX_PSP_DISPLAY_BUFFERS) + depth;
        return (spent < GFX_PSP_VRAM_BYTES) ? (GFX_PSP_VRAM_BYTES - spent) : 0u;
    }
}

PspGlRenderer::PspGlRenderer(const EngineConfig& config)
    : m_display(nullptr)
    , m_surface(nullptr)
    , m_context(nullptr)
    , m_whiteTexture(0)
    , m_geometry()
    , m_clearColor(Color3{0.0f, 0.0f, 0.0f})
    , m_width(GFX_SCREEN_WIDTH)
    , m_height(GFX_SCREEN_HEIGHT)
    , m_frameStats()
    , m_initialized(false)
{
    UNUSED_VAR(config);
    memset(m_textures, 0, sizeof(m_textures));

    float* arena = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!arena)
    {
        Engine_LogError("PspGlRenderer: failed to retrieve ARENA_RENDERER slot 0");
        return;
    }
    m_drawLists.Init(arena);

    if (!CreateContext())
        return;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (!CreateWhiteTexture())
    {
        Engine_LogError("PspGlRenderer: could not create the untextured fallback");
        return;
    }

    m_initialized = true;
    Engine_LogInfo("PspGlRenderer: ready (%ux%u, %u KB texture budget)", m_width, m_height, VramTextureBudget() / 1024u);
}

bool PspGlRenderer::CreateContext()
{
    EGLDisplay display = eglGetDisplay(0);
    if (!display || !eglInitialize(display, nullptr, nullptr))
    {
        Engine_LogError("PspGlRenderer: eglInitialize failed (0x%04X)", static_cast<unsigned>(eglGetError()));
        return false;
    }

    const EGLint attributes[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE};

    EGLConfig config = 0;
    EGLint configCount = 0;
    if (!eglChooseConfig(display, attributes, &config, 1, &configCount) || configCount == 0)
    {
        Engine_LogError("PspGlRenderer: no usable EGL config (0x%04X)", static_cast<unsigned>(eglGetError()));
        eglTerminate(display);
        return false;
    }

    EGLContext context = eglCreateContext(display, config, nullptr, nullptr);
    EGLSurface surface = eglCreateWindowSurface(display, config, 0, nullptr);
    if (!context || !surface || !eglMakeCurrent(display, surface, surface, context))
    {
        Engine_LogError("PspGlRenderer: could not make the context current (0x%04X)", static_cast<unsigned>(eglGetError()));
        if (surface)
            eglDestroySurface(display, surface);
        if (context)
            eglDestroyContext(display, context);
        eglTerminate(display);
        return false;
    }

    m_display = display;
    m_surface = surface;
    m_context = context;
    return true;
}

bool PspGlRenderer::CreateWhiteTexture()
{
    const uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex)
        return false;

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    m_whiteTexture = static_cast<uint32_t>(tex);
    return true;
}

uint32_t PspGlRenderer::GetTextureBudgetBytes() const { return VramTextureBudget(); }

uint32_t PspGlRenderer::UploadTexture(const TextureUpload& upload)
{
    const uint32_t width = static_cast<uint32_t>(upload.width);
    const uint32_t height = static_cast<uint32_t>(upload.height);
    if (!m_initialized || width == 0 || height == 0)
        return 0;

    int slot = -1;
    for (int i = 0; i < PSPGL_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (m_textures[i] == 0)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        Engine_LogError("PspGlRenderer: texture registry full (%d)", PSPGL_MAX_RESIDENT_TEXTURES);
        return 0;
    }

    const size_t bytes = static_cast<size_t>(width) * height * 4u;
    uint8_t* rgba = static_cast<uint8_t*>(malloc(bytes));
    if (!rgba)
    {
        Engine_LogError("PspGlRenderer: out of memory expanding a %ux%u texture", width, height);
        return 0;
    }

    if (!Gfx_ExpandToRgba8(upload, rgba, bytes))
    {
        free(rgba);
        Engine_LogError("PspGlRenderer: could not expand a %ux%u texture", width, height);
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex)
    {
        free(rgba);
        Engine_LogError("PspGlRenderer: glGenTextures failed");
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    const GLint filter = (upload.filter == TextureFilter::Nearest) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);

    m_textures[slot] = static_cast<uint32_t>(tex);
    return static_cast<uint32_t>(tex);
}

void PspGlRenderer::ReleaseTexture(uint32_t handle)
{
    if (handle == 0)
        return;

    for (int i = 0; i < PSPGL_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (m_textures[i] != handle)
            continue;
        GLuint tex = static_cast<GLuint>(handle);
        glDeleteTextures(1, &tex);
        m_textures[i] = 0;
        return;
    }
}

void PspGlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, -1);
}

void PspGlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, color, -1);
}

void PspGlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, textureId);
}

void PspGlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    PrimitiveDrawEntry entry;
    entry.transform = Transform3D(position, rotation, scale);
    entry.color = color;
    entry.type = primitive;
    entry.textureId = textureId;
    m_drawLists.AddPrimitive(entry);
}

void PspGlRenderer::AddLevelToDrawList(const Level& level) { UNUSED_VAR(level); }

void PspGlRenderer::AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    ModelDrawEntry entry;
    entry.resourceId = modelId;
    entry.transform = Transform3D(position, rotation, scale);
    m_drawLists.AddModel(entry);
}

void PspGlRenderer::AddSkyToDrawList(int32_t resourceId) { m_drawLists.SetSkyboxTexture(resourceId); }

void PspGlRenderer::ClearDrawLists() { m_drawLists.Reset(false); }

void PspGlRenderer::ClearFrame(const Color3& color) { m_clearColor = color; }

void PspGlRenderer::DrawQuad2D(const Quad2D& quad) { m_geometry.AddQuad2D(quad); }

void PspGlRenderer::DrawGrid(int32_t slices, float spacing)
{
    UNUSED_VAR(slices);
    UNUSED_VAR(spacing);
}

void PspGlRenderer::BeginFrame()
{
    Platform* platform = Engine_GetPlatform();
    platform->GetFramebufferSize(&m_width, &m_height);

    m_geometry.SetFrameBudget(GFX_PSP_MAX_FRAME_VERTICES, m_width, m_height);
    m_geometry.BeginFrame();
    m_frameStats = DrawStats{};
}

void PspGlRenderer::Render() { m_geometry.BuildFrame(m_drawLists, &m_frameStats); }

void PspGlRenderer::BindVertexArrays(const StagedGeometry::Vertex* base)
{
    const GLsizei stride = sizeof(StagedGeometry::Vertex);
    glVertexPointer(3, GL_FLOAT, stride, &base->x);
    glNormalPointer(GL_FLOAT, stride, &base->nx);
    glTexCoordPointer(2, GL_FLOAT, stride, &base->u);
    glColorPointer(4, GL_FLOAT, stride, &base->r);
}

void PspGlRenderer::DrawStagedGeometry()
{
    const uint32_t count3D = m_geometry.Count3D();
    const uint32_t count2D = m_geometry.Count2D();
    if (count3D == 0 && count2D == 0)
        return;

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    float matrix[16];

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);

    if (count3D > 0)
    {
        glEnable(GL_DEPTH_TEST);
        StagedGeometry::BuildViewProjection(m_drawLists.GetCamera3D(), m_width, m_height, false, matrix);
        glLoadMatrixf(matrix);

        BindVertexArrays(m_geometry.Vertices3D());

        const StagedGeometry::DrawRun* runs = m_geometry.Runs();
        for (uint32_t i = 0; i < m_geometry.RunCount(); ++i)
        {
            glBindTexture(GL_TEXTURE_2D, runs[i].texture ? runs[i].texture : m_whiteTexture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(runs[i].first), static_cast<GLsizei>(runs[i].count));
        }
    }

    if (count2D > 0)
    {
        glDisable(GL_DEPTH_TEST);
        StagedGeometry::BuildOrtho2D(m_width, m_height, false, matrix);
        glLoadMatrixf(matrix);

        BindVertexArrays(m_geometry.Vertices2D());

        const StagedGeometry::DrawRun* runs2D = m_geometry.Runs2D();
        for (uint32_t i = 0; i < m_geometry.RunCount2D(); ++i)
        {
            glBindTexture(GL_TEXTURE_2D, runs2D[i].texture ? runs2D[i].texture : m_whiteTexture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(runs2D[i].first), static_cast<GLsizei>(runs2D[i].count));
        }
    }

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void PspGlRenderer::EndFrame()
{
    if (!m_initialized)
        return;

    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    DrawStagedGeometry();

    PspUtilityDialog_Update();

    Platform* platform = Engine_GetPlatform();
    const double waitStart = platform->GetTimeSeconds();
    eglSwapBuffers(m_display, m_surface);
    m_frameStats.presentWaitMs = static_cast<float>((platform->GetTimeSeconds() - waitStart) * 1000.0);

    m_geometry.EndFrame();

    m_drawLists.SetLastStats(m_frameStats);
    m_drawLists.Reset(false);
}

void PspGlRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }
void PspGlRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }
void PspGlRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }

bool PspGlRenderer::IsInitialized() const { return m_initialized; }

void PspGlRenderer::Shutdown()
{
    if (!m_initialized)
        return;

    for (int i = 0; i < PSPGL_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (!m_textures[i])
            continue;
        GLuint tex = static_cast<GLuint>(m_textures[i]);
        glDeleteTextures(1, &tex);
        m_textures[i] = 0;
    }
    if (m_whiteTexture)
    {
        GLuint tex = static_cast<GLuint>(m_whiteTexture);
        glDeleteTextures(1, &tex);
        m_whiteTexture = 0;
    }

    if (m_display)
    {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_surface)
            eglDestroySurface(m_display, m_surface);
        if (m_context)
            eglDestroyContext(m_display, m_context);
        eglTerminate(m_display);
    }
    m_display = nullptr;
    m_surface = nullptr;
    m_context = nullptr;

    m_initialized = false;
}

RendererType PspGlRenderer::GetRendererType() const { return RendererType::PspGl; }
DrawStats PspGlRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }
Camera3D PspGlRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }

void PspGlRenderer::RenderSkybox(const DrawLists& lists) { UNUSED_VAR(lists); }
void PspGlRenderer::RenderPrimitives(DrawLists& lists) { UNUSED_VAR(lists); }
void PspGlRenderer::RenderModels(const DrawLists& lists) { UNUSED_VAR(lists); }
