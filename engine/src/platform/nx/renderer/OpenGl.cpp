#include "renderer/OpenGl.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "Macros.h"
#include "core/EngineDebug.h"
#include "core/EngineMemory.h"
#include "graphics/TextureExpand.h"
#include "platform/Platform.h"
#include "scene_frag_glsl.h"
#include "scene_vert_glsl.h"

#include <switch.h>

namespace
{
    const EGLint NXGL_CONTEXT_MAJOR = 4;
    const EGLint NXGL_CONTEXT_MINOR = 3;
    const GLsizeiptr NXGL_UNIFORM_BYTES = 16 * sizeof(float);
    const GLuint NXGL_UNIFORM_BINDING = 0;

    GLuint CompileShader(GLenum type, const char* source)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint status = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (!status)
        {
            char log[1024];
            GLsizei length = 0;
            glGetShaderInfoLog(shader, sizeof(log), &length, log);
            log[sizeof(log) - 1] = '\0';
            Engine_LogError("OpenGl: %s shader failed to compile: %s", (type == GL_VERTEX_SHADER) ? "vertex" : "fragment", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    double Now() { return Engine_GetPlatform()->GetTimeSeconds(); }
} // namespace

OpenGlRenderer::OpenGlRenderer(const EngineConfig& config) :
    m_display(EGL_NO_DISPLAY), m_context(EGL_NO_CONTEXT), m_surface(EGL_NO_SURFACE), m_program(0), m_vao(0), m_vertexBuffer(0), m_vertexBufferCapacity(0), m_uniformBuffer(0), m_whiteTexture(0),
    m_clearColor{0.0f, 0.0f, 0.0f}, m_width(GFX_SCREEN_WIDTH), m_height(GFX_SCREEN_HEIGHT), m_cropWidth(0), m_cropHeight(0), m_frameStats{}, m_initialized(false), m_imageFbo(0), m_imageColorTex(0),
    m_imageDepthRb(0), m_imageWidth(0), m_imageHeight(0)
{
    UNUSED_VAR(config);
    memset(m_textures, 0, sizeof(m_textures));

    Engine_GetPlatform()->GetFramebufferSize(&m_width, &m_height);
    Engine_LogInfo("OpenGlRenderer: initializing (%ux%u)", m_width, m_height);

    float* arena = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!arena)
    {
        Engine_LogError("OpenGlRenderer: failed to retrieve ARENA_RENDERER slot 0");
        return;
    }
    m_drawLists.Init(arena);

    if (!CreateContext() || !CreateProgram() || !CreateWhiteTexture())
    {
        DestroyContext();
        return;
    }

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vertexBuffer);

    glGenBuffers(1, &m_uniformBuffer);
    glBindBuffer(GL_UNIFORM_BUFFER, m_uniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, NXGL_UNIFORM_BYTES, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, NXGL_UNIFORM_BINDING, m_uniformBuffer);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);

    m_initialized = true;
    Engine_LogInfo("OpenGlRenderer: ready");
}

bool OpenGlRenderer::CreateContext()
{
    NWindow* window = static_cast<NWindow*>(Engine_GetPlatform()->GetNativeWindowHandle());
    if (!window)
    {
        Engine_LogError("OpenGl: platform has no native window");
        return false;
    }
    nwindowSetDimensions(window, GFX_NX_DOCKED_WIDTH, GFX_NX_DOCKED_HEIGHT);

    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_display == EGL_NO_DISPLAY || !eglInitialize(m_display, nullptr, nullptr))
    {
        Engine_LogError("OpenGl: no EGL display (0x%X)", static_cast<unsigned>(eglGetError()));
        return false;
    }

    if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE)
    {
        Engine_LogError("OpenGl: the driver does not offer desktop OpenGL (0x%X)", static_cast<unsigned>(eglGetError()));
        return false;
    }

    static const EGLint framebufferAttributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLConfig eglConfig = nullptr;
    EGLint configCount = 0;
    if (!eglChooseConfig(m_display, framebufferAttributes, &eglConfig, 1, &configCount) || configCount == 0)
    {
        Engine_LogError("OpenGl: no RGBA8 / D24S8 framebuffer configuration (0x%X)", static_cast<unsigned>(eglGetError()));
        return false;
    }

    m_surface = eglCreateWindowSurface(m_display, eglConfig, reinterpret_cast<EGLNativeWindowType>(window), nullptr);
    if (m_surface == EGL_NO_SURFACE)
    {
        Engine_LogError("OpenGl: could not create the window surface (0x%X); is another renderer still attached?", static_cast<unsigned>(eglGetError()));
        return false;
    }

    const EGLint contextAttributes[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_CONTEXT_MAJOR_VERSION, NXGL_CONTEXT_MAJOR, EGL_CONTEXT_MINOR_VERSION, NXGL_CONTEXT_MINOR, EGL_NONE};
    m_context = eglCreateContext(m_display, eglConfig, EGL_NO_CONTEXT, contextAttributes);
    if (m_context == EGL_NO_CONTEXT || !eglMakeCurrent(m_display, m_surface, m_surface, m_context))
    {
        Engine_LogError("OpenGl: could not create a GL %d.%d core context (0x%X)", NXGL_CONTEXT_MAJOR, NXGL_CONTEXT_MINOR, static_cast<unsigned>(eglGetError()));
        return false;
    }

    if (!gladLoadGL())
    {
        Engine_LogError("OpenGl: could not load the GL entry points");
        return false;
    }

    eglSwapInterval(m_display, 1);

    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    Engine_LogInfo("OpenGl: %s", version ? reinterpret_cast<const char*>(version) : "<unknown version>");
    Engine_LogInfo("OpenGl: %s", renderer ? reinterpret_cast<const char*>(renderer) : "<unknown renderer>");
    return true;
}

bool OpenGlRenderer::CreateProgram()
{
    GLuint vs = CompileShader(GL_VERTEX_SHADER, g_SceneVertexGlsl);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, g_SceneFragmentGlsl);
    if (!vs || !fs)
    {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint status = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &status);
    if (!status)
    {
        char log[1024];
        GLsizei length = 0;
        glGetProgramInfoLog(m_program, sizeof(log), &length, log);
        log[sizeof(log) - 1] = '\0';
        Engine_LogError("OpenGl: program link failed: %s", log);
        return false;
    }
    return true;
}

bool OpenGlRenderer::CreateWhiteTexture()
{
    const uint32_t white = 0xFFFFFFFFu;
    glGenTextures(1, &m_whiteTexture);
    if (!m_whiteTexture)
        return false;

    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    return true;
}

void OpenGlRenderer::SetupVertexAttributes()
{
    const GLsizei stride = sizeof(StagedGeometry::Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(StagedGeometry::Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(StagedGeometry::Vertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(StagedGeometry::Vertex, u)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(StagedGeometry::Vertex, r)));
}

void OpenGlRenderer::SetViewProjection(const float matrix[16])
{
    glBindBuffer(GL_UNIFORM_BUFFER, m_uniformBuffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, NXGL_UNIFORM_BYTES, matrix);
}

uint32_t OpenGlRenderer::UploadTexture(const TextureUpload& upload)
{
    const uint32_t width = static_cast<uint32_t>(upload.width);
    const uint32_t height = static_cast<uint32_t>(upload.height);
    if (width == 0 || height == 0)
        return 0;

    int slot = -1;
    for (int i = 0; i < NXGL_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (m_textures[i] == 0)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        Engine_LogError("OpenGlRenderer: texture registry full (%d)", NXGL_MAX_RESIDENT_TEXTURES);
        return 0;
    }

    const size_t bytes = static_cast<size_t>(width) * height * 4u;
    uint8_t* rgba = static_cast<uint8_t*>(malloc(bytes));
    if (!rgba)
    {
        Engine_LogError("OpenGlRenderer: out of memory expanding a %ux%u texture", width, height);
        return 0;
    }

    if (!Gfx_ExpandToRgba8(upload, rgba, bytes))
    {
        free(rgba);
        Engine_LogError("OpenGlRenderer: could not expand a %ux%u texture", width, height);
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture)
    {
        free(rgba);
        Engine_LogError("OpenGlRenderer: glGenTextures failed");
        return 0;
    }

    const GLint filter = (upload.filter == TextureFilter::Nearest) ? GL_NEAREST : GL_LINEAR;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);

    m_textures[slot] = texture;
    return static_cast<uint32_t>(texture);
}

void OpenGlRenderer::ReleaseTexture(uint32_t handle)
{
    if (handle == 0)
        return;

    for (int i = 0; i < NXGL_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (m_textures[i] != handle)
            continue;
        GLuint texture = m_textures[i];
        glDeleteTextures(1, &texture);
        m_textures[i] = 0;
        return;
    }
}

void OpenGlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, -1);
}

void OpenGlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, color, -1);
}

void OpenGlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId)
{
    AddPrimitiveToDrawList(primitive, position, rotation, scale, Color3{1.0f, 1.0f, 1.0f}, textureId);
}

void OpenGlRenderer::AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId)
{
    PrimitiveDrawEntry entry;
    entry.transform = Transform3D(position, rotation, scale);
    entry.color = color;
    entry.type = primitive;
    entry.textureId = textureId;
    m_drawLists.AddPrimitive(entry);
}

void OpenGlRenderer::AddLevelToDrawList(const Level& level) { UNUSED_VAR(level); }

void OpenGlRenderer::AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale)
{
    ModelDrawEntry entry;
    entry.resourceId = modelId;
    entry.transform = Transform3D(position, rotation, scale);
    m_drawLists.AddModel(entry);
}

void OpenGlRenderer::AddSkyToDrawList(int32_t resourceId) { m_drawLists.SetSkyboxTexture(resourceId); }

void OpenGlRenderer::ClearDrawLists() { m_drawLists.Reset(false); }

void OpenGlRenderer::ClearFrame(const Color3& color) { m_clearColor = color; }

void OpenGlRenderer::DrawQuad2D(const Quad2D& quad) { m_geometry.AddQuad2D(quad); }

void OpenGlRenderer::DrawGrid(int32_t slices, float spacing)
{
    UNUSED_VAR(slices);
    UNUSED_VAR(spacing);
}

void OpenGlRenderer::BeginFrame()
{
    Engine_GetPlatform()->GetFramebufferSize(&m_width, &m_height);
    m_geometry.SetFrameBudget(GFX_NX_MAX_FRAME_VERTICES, m_width, m_height);
    m_geometry.BeginFrame();
    m_frameStats = DrawStats{};
}

void OpenGlRenderer::Render()
{
    const double start = Now();
    m_geometry.BuildFrame(m_drawLists, &m_frameStats);
    m_frameStats.geometryBuildMs = static_cast<float>((Now() - start) * 1000.0);
}

void OpenGlRenderer::UploadAndDraw()
{
    uint32_t count2D = m_geometry.Count2D();
    uint32_t count3D = m_geometry.Count3D();
    if (count2D > GFX_NX_MAX_FRAME_VERTICES)
        count2D = GFX_NX_MAX_FRAME_VERTICES;
    if (count3D > GFX_NX_MAX_FRAME_VERTICES - count2D)
        count3D = GFX_NX_MAX_FRAME_VERTICES - count2D;

    const uint32_t total = count3D + count2D;
    if (total == 0)
        return;

    const GLsizeiptr stride = sizeof(StagedGeometry::Vertex);
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(total) * stride;

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    const GLsizeiptr ceiling = static_cast<GLsizeiptr>(GFX_NX_MAX_FRAME_VERTICES) * stride;
    if (bytes > m_vertexBufferCapacity)
        m_vertexBufferCapacity = (bytes * 2 < ceiling) ? bytes * 2 : ceiling;

    glBufferData(GL_ARRAY_BUFFER, m_vertexBufferCapacity, nullptr, GL_STREAM_DRAW);
    if (count3D)
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count3D) * stride, m_geometry.Vertices3D());
    if (count2D)
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(count3D) * stride, static_cast<GLsizeiptr>(count2D) * stride, m_geometry.Vertices2D());

    SetupVertexAttributes();
    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);

    float matrix[16];

    if (count3D > 0)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        StagedGeometry::BuildViewProjection(m_drawLists.GetCamera3D(), m_width, m_height, false, matrix);
        SetViewProjection(matrix);

        const StagedGeometry::DrawRun* runs = m_geometry.Runs();
        for (uint32_t i = 0; i < m_geometry.RunCount(); ++i)
        {
            if (runs[i].first >= count3D)
                continue;
            const uint32_t count = (runs[i].first + runs[i].count > count3D) ? count3D - runs[i].first : runs[i].count;
            glBindTexture(GL_TEXTURE_2D, runs[i].texture ? runs[i].texture : m_whiteTexture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(runs[i].first), static_cast<GLsizei>(count - count % 3u));
            ++m_frameStats.texBinds;
        }
    }

    if (count2D > 0)
    {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        StagedGeometry::BuildOrtho2D(m_width, m_height, false, matrix);
        SetViewProjection(matrix);

        const StagedGeometry::DrawRun* runs2D = m_geometry.Runs2D();
        for (uint32_t i = 0; i < m_geometry.RunCount2D(); ++i)
        {
            if (runs2D[i].first >= count2D)
                continue;
            const uint32_t count = (runs2D[i].first + runs2D[i].count > count2D) ? count2D - runs2D[i].first : runs2D[i].count;
            glBindTexture(GL_TEXTURE_2D, runs2D[i].texture ? runs2D[i].texture : m_whiteTexture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(count3D + runs2D[i].first), static_cast<GLsizei>(count - count % 3u));
            ++m_frameStats.texBinds;
        }
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
}

void OpenGlRenderer::EndFrame()
{
    if (!m_initialized)
        return;

    if (m_width != m_cropWidth || m_height != m_cropHeight)
    {
        nwindowSetCrop(static_cast<NWindow*>(Engine_GetPlatform()->GetNativeWindowHandle()), 0, 0, static_cast<s32>(m_width), static_cast<s32>(m_height));
        m_cropWidth = m_width;
        m_cropHeight = m_height;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, static_cast<GLint>(GFX_NX_DOCKED_HEIGHT - m_height), static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    UploadAndDraw();

    const double swapStart = Now();
    eglSwapBuffers(m_display, m_surface);
    m_frameStats.presentWaitMs = static_cast<float>((Now() - swapStart) * 1000.0);

    m_geometry.EndFrame();
    m_drawLists.SetLastStats(m_frameStats);
    m_drawLists.Reset(false);
}

bool OpenGlRenderer::EnsureImageTarget(int width, int height)
{
    if (width <= 0 || height <= 0 || width > GFX_MAX_TEXTURE_WIDTH || height > GFX_MAX_TEXTURE_HEIGHT)
        return false;
    if (m_imageFbo != 0 && m_imageWidth == width && m_imageHeight == height)
        return true;

    if (m_imageFbo == 0)
    {
        glGenFramebuffers(1, &m_imageFbo);
        glGenTextures(1, &m_imageColorTex);
        glGenRenderbuffers(1, &m_imageDepthRb);
    }
    if (!m_imageFbo || !m_imageColorTex || !m_imageDepthRb)
        return false;

    glBindTexture(GL_TEXTURE_2D, m_imageColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindRenderbuffer(GL_RENDERBUFFER, m_imageDepthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_imageFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_imageColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_imageDepthRb);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete)
    {
        Engine_LogError("OpenGlRenderer: offscreen image target %dx%d is incomplete", width, height);
        return false;
    }

    m_imageWidth = width;
    m_imageHeight = height;
    return true;
}

uint32_t OpenGlRenderer::RenderToImage3D(const Renderable3D& what, const Camera3D& camera, int width, int height, const Color3& clearColor)
{
    if (!m_initialized || !EnsureImageTarget(width, height))
        return 0;
    if (!m_imageGeometry.BuildOne(what, m_drawLists))
        return 0;

    glBindFramebuffer(GL_FRAMEBUFFER, m_imageFbo);
    glViewport(0, 0, width, height);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    uint32_t count = m_imageGeometry.Count3D();
    if (count > GFX_NX_MAX_FRAME_VERTICES)
        count = GFX_NX_MAX_FRAME_VERTICES;

    if (count > 0)
    {
        const GLsizeiptr stride = sizeof(StagedGeometry::Vertex);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
        const GLsizeiptr bytes = static_cast<GLsizeiptr>(count) * stride;
        if (bytes > m_vertexBufferCapacity)
            m_vertexBufferCapacity = bytes;
        glBufferData(GL_ARRAY_BUFFER, m_vertexBufferCapacity, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, m_imageGeometry.Vertices3D());
        SetupVertexAttributes();

        glUseProgram(m_program);
        glActiveTexture(GL_TEXTURE0);

        float matrix[16];
        StagedGeometry::BuildViewProjection(camera, static_cast<uint32_t>(width), static_cast<uint32_t>(height), false, matrix);
        matrix[1] = -matrix[1];
        matrix[5] = -matrix[5];
        matrix[9] = -matrix[9];
        matrix[13] = -matrix[13];
        SetViewProjection(matrix);

        const StagedGeometry::DrawRun* runs = m_imageGeometry.Runs();
        for (uint32_t i = 0; i < m_imageGeometry.RunCount(); ++i)
        {
            if (runs[i].first >= count)
                continue;
            const uint32_t runCount = (runs[i].first + runs[i].count > count) ? count - runs[i].first : runs[i].count;
            glBindTexture(GL_TEXTURE_2D, runs[i].texture ? runs[i].texture : m_whiteTexture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(runs[i].first), static_cast<GLsizei>(runCount - runCount % 3u));
        }
    }

    glFinish();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return static_cast<uint32_t>(m_imageColorTex);
}

void OpenGlRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }
void OpenGlRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }
void OpenGlRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }

bool OpenGlRenderer::IsInitialized() const { return m_initialized; }

void OpenGlRenderer::DestroyContext()
{
    if (m_display == EGL_NO_DISPLAY)
        return;

    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (m_context != EGL_NO_CONTEXT)
        eglDestroyContext(m_display, m_context);
    if (m_surface != EGL_NO_SURFACE)
        eglDestroySurface(m_display, m_surface);
    eglTerminate(m_display);

    m_context = EGL_NO_CONTEXT;
    m_surface = EGL_NO_SURFACE;
    m_display = EGL_NO_DISPLAY;
}

void OpenGlRenderer::Shutdown()
{
    if (!m_initialized)
        return;

    for (int i = 0; i < NXGL_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (m_textures[i])
            glDeleteTextures(1, &m_textures[i]);
    }
    if (m_whiteTexture)
        glDeleteTextures(1, &m_whiteTexture);
    if (m_vertexBuffer)
        glDeleteBuffers(1, &m_vertexBuffer);
    if (m_uniformBuffer)
        glDeleteBuffers(1, &m_uniformBuffer);
    if (m_vao)
        glDeleteVertexArrays(1, &m_vao);
    if (m_program)
        glDeleteProgram(m_program);
    if (m_imageColorTex)
        glDeleteTextures(1, &m_imageColorTex);
    if (m_imageDepthRb)
        glDeleteRenderbuffers(1, &m_imageDepthRb);
    if (m_imageFbo)
        glDeleteFramebuffers(1, &m_imageFbo);

    DestroyContext();
    m_initialized = false;
}

RendererType OpenGlRenderer::GetRendererType() const { return RendererType::OpenGl; }
DrawStats OpenGlRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }
Camera3D OpenGlRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }

void OpenGlRenderer::RenderSkybox(const DrawLists& lists) { UNUSED_VAR(lists); }
void OpenGlRenderer::RenderPrimitives(DrawLists& lists) { UNUSED_VAR(lists); }
void OpenGlRenderer::RenderModels(const DrawLists& lists) { UNUSED_VAR(lists); }
