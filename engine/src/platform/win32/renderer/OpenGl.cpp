#include "platform/win32/renderer/OpenGl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Macros.h"
#include "core/EngineDebug.h"
#include "core/EngineMemory.h"
#include "graphics/TextureExpand.h"
#include "platform/Platform.h"

namespace
{

    // Two dialects of one shader. The only differences are the version pragma,
    // attribute/varying vs in/out, and texture2D vs texture - which is exactly
    // why supporting 2.1 costs so little and buys a fallback that runs on Mesa,
    // in VMs and over remote desktop.
    const char* const kVertex330 = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;
uniform mat4 uViewProj;
out vec2 vUv;
out vec4 vColor;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vUv = aUv;
    float n = length(aNormal);
    float shade = 1.0;
    if (n > 0.0001) {
        vec3 l = normalize(vec3(0.4, 0.8, 0.45));
        shade = 0.35 + 0.65 * max(dot(normalize(aNormal), l), 0.0);
    }
    vColor = vec4(aColor.rgb * shade, aColor.a);
}
)GLSL";

    const char* const kFragment330 = R"GLSL(#version 330 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 oColor;
void main() {
    vec4 t = texture(uTexture, vUv);
    oColor = vec4(t.rgb * vColor.rgb, t.a * vColor.a);
}
)GLSL";

    const char* const kVertex120 = R"GLSL(#version 120
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec2 aUv;
attribute vec4 aColor;
uniform mat4 uViewProj;
varying vec2 vUv;
varying vec4 vColor;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vUv = aUv;
    float n = length(aNormal);
    float shade = 1.0;
    if (n > 0.0001) {
        vec3 l = normalize(vec3(0.4, 0.8, 0.45));
        shade = 0.35 + 0.65 * max(dot(normalize(aNormal), l), 0.0);
    }
    vColor = vec4(aColor.rgb * shade, aColor.a);
}
)GLSL";

    const char* const kFragment120 = R"GLSL(#version 120
varying vec2 vUv;
varying vec4 vColor;
uniform sampler2D uTexture;
void main() {
    vec4 t = texture2D(uTexture, vUv);
    gl_FragColor = vec4(t.rgb * vColor.rgb, t.a * vColor.a);
}
)GLSL";

    GLuint CompileShader(GLenum type, const char* source)
    {
        GLuint shader = gl_CreateShader(type);
        gl_ShaderSource(shader, 1, &source, nullptr);
        gl_CompileShader(shader);

        GLint status = 0;
        gl_GetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (!status)
        {
            char log[1024];
            GLsizei len = 0;
            gl_GetShaderInfoLog(shader, sizeof(log), &len, log);
            log[sizeof(log) - 1] = '\0';
            Engine_LogError("OpenGl: %s shader failed to compile: %s", (type == GL_VERTEX_SHADER) ? "vertex" : "fragment", log);
            gl_DeleteShader(shader);
            return 0;
        }
        return shader;
    }

} // namespace

OpenGlRenderer::OpenGlRenderer(const EngineConfig& config) :
    m_hwnd(nullptr), m_dc(nullptr), m_context(nullptr), m_coreProfile(false), m_versionMajor(0), m_versionMinor(0), m_program(0), m_uniformViewProj(-1), m_uniformTexture(-1), m_vao(0),
    m_vertexBuffer(0), m_vertexBufferCapacity(0), m_whiteTexture(0), m_clearColor{0.0f, 0.0f, 0.0f}, m_width(0), m_height(0), m_frameStats{}, m_initialized(false), m_imageFbo(0), m_imageColorTex(0),
    m_imageDepthRb(0), m_imageWidth(0), m_imageHeight(0)
{
    UNUSED_VAR(config);
    memset(m_textures, 0, sizeof(m_textures));

    Platform* platform = Engine_GetPlatform();
    platform->GetFramebufferSize(&m_width, &m_height);
    Engine_LogInfo("OpenGlRenderer: initializing (%ux%u)", m_width, m_height);

    // The primitive geometry tables live in the renderer arena, same as every
    // other backend.
    float* arena = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!arena)
    {
        Engine_LogError("OpenGlRenderer: failed to retrieve ARENA_RENDERER slot 0");
        return;
    }
    m_drawLists.Init(arena);

    if (!CreateContext())
        return;
    if (!CreateProgram())
        return;
    if (!CreateWhiteTexture())
        return;

    gl_GenBuffers(1, &m_vertexBuffer);
    if (m_coreProfile && gl_GenVertexArrays)
    {
        gl_GenVertexArrays(1, &m_vao);
        gl_BindVertexArray(m_vao);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    // No back-face culling: the baked level and model geometry does not carry a
    // guaranteed winding, and dropping triangles is worse than drawing extra.
    glDisable(GL_CULL_FACE);

    m_initialized = true;
    Engine_LogInfo("OpenGlRenderer: ready (GL %d.%d, %s path)", m_versionMajor, m_versionMinor, m_coreProfile ? "core" : "legacy 2.1");
}

bool OpenGlRenderer::CreateContext()
{
    Platform* platform = Engine_GetPlatform();
    m_hwnd = static_cast<HWND>(platform->GetNativeWindowHandle());
    if (!m_hwnd)
    {
        Engine_LogError("OpenGl: platform has no native window handle");
        return false;
    }

    m_dc = GetDC(m_hwnd);
    if (!m_dc)
    {
        Engine_LogError("OpenGl: GetDC failed");
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(m_dc, &pfd);
    if (!format || !SetPixelFormat(m_dc, format, &pfd))
    {
        Engine_LogError("OpenGl: no suitable pixel format (%lu)", GetLastError());
        return false;
    }

    // The chicken-and-egg every WGL app hits: wglCreateContextAttribsARB is an
    // extension, and extensions can only be resolved through a context that
    // already exists. So make a legacy context, borrow its proc address, then
    // throw it away.
    HGLRC legacy = wglCreateContext(m_dc);
    if (!legacy || !wglMakeCurrent(m_dc, legacy))
    {
        Engine_LogError("OpenGl: could not create a bootstrap context");
        return false;
    }

    // Via void*: casting PROC straight to a typed function pointer trips
    // -Wcast-function-type, and the two-step is what the WGL contract expects.
    gl_wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARB>(reinterpret_cast<void*>(wglGetProcAddress("wglCreateContextAttribsARB")));

    // Which version to ask for. --gl-version pins one; otherwise walk down from
    // the newest, because a driver returns null rather than a lower version when
    // it cannot honour the request.
    static const struct
    {
        int major, minor;
        bool core;
    } kCandidates[] = {{4, 6, true}, {4, 3, true}, {3, 3, true}, {2, 1, false}};

    const CommandLine* cmd = platform->GetStartupArgs().commandLine;
    const char* requested = cmd ? cmd->GetString("gl-version", nullptr) : nullptr;

    if (gl_wglCreateContextAttribsARB)
    {
        for (size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); ++i)
        {
            if (requested)
            {
                char want[16];
                snprintf(want, sizeof(want), "%d.%d", kCandidates[i].major, kCandidates[i].minor);
                if (strcmp(requested, want) != 0)
                    continue;
            }

            int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                             kCandidates[i].major,
                             WGL_CONTEXT_MINOR_VERSION_ARB,
                             kCandidates[i].minor,
                             WGL_CONTEXT_PROFILE_MASK_ARB,
                             kCandidates[i].core ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                             0};

            HGLRC ctx = gl_wglCreateContextAttribsARB(m_dc, nullptr, attribs);
            if (!ctx)
                continue;

            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(legacy);
            legacy = nullptr;

            if (!wglMakeCurrent(m_dc, ctx))
            {
                wglDeleteContext(ctx);
                Engine_LogError("OpenGl: could not activate the GL %d.%d context", kCandidates[i].major, kCandidates[i].minor);
                return false;
            }

            m_context = ctx;
            m_coreProfile = kCandidates[i].core;
            m_versionMajor = kCandidates[i].major;
            m_versionMinor = kCandidates[i].minor;
            break;
        }
    }

    if (!m_context)
    {
        if (requested)
            Engine_LogError("OpenGl: the driver would not grant a GL %s context", requested);

        // Keep the bootstrap context rather than failing: it is whatever the
        // driver considers its default, which is enough for the 2.1 path.
        if (!legacy)
        {
            Engine_LogError("OpenGl: no usable context");
            return false;
        }
        m_context = legacy;
        m_coreProfile = false;
        m_versionMajor = 2;
        m_versionMinor = 1;
        legacy = nullptr;
    }

    if (legacy)
    {
        wglDeleteContext(legacy);
    }

    if (!Gl_LoadFunctions(m_coreProfile))
    {
        Engine_LogError("OpenGl: required entry points are missing");
        return false;
    }

    // Report what the driver actually gave us, which may exceed what was asked.
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    Engine_LogInfo("OpenGl: %s", version ? reinterpret_cast<const char*>(version) : "<unknown version>");
    Engine_LogInfo("OpenGl: %s", renderer ? reinterpret_cast<const char*>(renderer) : "<unknown renderer>");

    if (gl_wglSwapIntervalEXT)
        gl_wglSwapIntervalEXT(1); // vsync

    return true;
}

bool OpenGlRenderer::CreateProgram()
{
    const char* vertexSource = m_coreProfile ? kVertex330 : kVertex120;
    const char* fragmentSource = m_coreProfile ? kFragment330 : kFragment120;

    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vs || !fs)
        return false;

    m_program = gl_CreateProgram();
    gl_AttachShader(m_program, vs);
    gl_AttachShader(m_program, fs);

    // GLSL 120 has no layout qualifiers, so the locations are bound explicitly.
    // Doing it on both paths keeps SetupVertexAttributes identical either way.
    gl_BindAttribLocation(m_program, 0, "aPos");
    gl_BindAttribLocation(m_program, 1, "aNormal");
    gl_BindAttribLocation(m_program, 2, "aUv");
    gl_BindAttribLocation(m_program, 3, "aColor");

    gl_LinkProgram(m_program);

    GLint status = 0;
    gl_GetProgramiv(m_program, GL_LINK_STATUS, &status);
    if (!status)
    {
        char log[1024];
        GLsizei len = 0;
        gl_GetProgramInfoLog(m_program, sizeof(log), &len, log);
        log[sizeof(log) - 1] = '\0';
        Engine_LogError("OpenGl: program link failed: %s", log);
        return false;
    }

    gl_DeleteShader(vs);
    gl_DeleteShader(fs);

    m_uniformViewProj = gl_GetUniformLocation(m_program, "uViewProj");
    m_uniformTexture = gl_GetUniformLocation(m_program, "uTexture");
    return true;
}

bool OpenGlRenderer::CreateWhiteTexture()
{
    const uint32_t white = 0xFFFFFFFFu;
    glGenTextures(1, &m_whiteTexture);
    if (!m_whiteTexture)
        return false;

    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    return true;
}

void OpenGlRenderer::SetupVertexAttributes()
{
    const GLsizei stride = sizeof(StagedGeometry::Vertex);
    gl_EnableVertexAttribArray(0);
    gl_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    gl_EnableVertexAttribArray(1);
    gl_VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(sizeof(float) * 3));
    gl_EnableVertexAttribArray(2);
    gl_VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(sizeof(float) * 6));
    gl_EnableVertexAttribArray(3);
    gl_VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(sizeof(float) * 8));
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

uint32_t OpenGlRenderer::UploadTexture(const TextureUpload& upload)
{
    const uint32_t width = static_cast<uint32_t>(upload.width);
    const uint32_t height = static_cast<uint32_t>(upload.height);
    if (width == 0 || height == 0)
        return 0;

    int slot = -1;
    for (int i = 0; i < GL_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (m_textures[i] == 0)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        Engine_LogError("OpenGlRenderer: texture registry full (%d)", GL_MAX_RESIDENT_TEXTURES);
        return 0;
    }

    const size_t texels = static_cast<size_t>(width) * height;

    // Every source format is expanded to RGBA8, same as the WebGPU backend: a
    // desktop GPU has no reason to carry the GS storage modes, and the resource
    // manager budgets against that assumption.
    uint8_t* rgba = static_cast<uint8_t*>(malloc(texels * 4));
    if (!rgba)
    {
        Engine_LogError("OpenGlRenderer: out of memory expanding a %ux%u texture", width, height);
        return 0;
    }

    if (!Gfx_ExpandToRgba8(upload, rgba, texels * 4u))
    {
        free(rgba);
        Engine_LogError("OpenGlRenderer: could not expand a %ux%u texture", width, height);
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex)
    {
        free(rgba);
        Engine_LogError("OpenGlRenderer: glGenTextures failed");
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // GL_LINEAR rather than a mipmapped filter: only level 0 is uploaded, and a
    // mipmapped filter with no mip chain samples as incomplete and renders black.
    const GLint filter = (upload.filter == TextureFilter::Nearest) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);

    m_textures[slot] = tex;

    // The GL name IS the handle: GL never returns 0 for a valid texture, which
    // is exactly the engine's invalid-handle convention.
    return static_cast<uint32_t>(tex);
}

void OpenGlRenderer::ReleaseTexture(uint32_t handle)
{
    if (handle == 0)
        return;

    for (int i = 0; i < GL_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (m_textures[i] != handle)
            continue;
        GLuint tex = m_textures[i];
        glDeleteTextures(1, &tex);
        m_textures[i] = 0;
        return;
    }
}

// ---------------------------------------------------------------------------
// Draw-list submission
// ---------------------------------------------------------------------------

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

// Level geometry is pulled from the sector manager at render time rather than
// queued here, matching every other backend.
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

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void OpenGlRenderer::BeginFrame()
{
    Platform* platform = Engine_GetPlatform();
    platform->GetFramebufferSize(&m_width, &m_height);

    m_geometry.SetFrameBudget(0, m_width, m_height);
    m_geometry.BeginFrame();
    m_frameStats = DrawStats{};
}

void OpenGlRenderer::Render() { m_geometry.BuildFrame(m_drawLists, &m_frameStats); }

void OpenGlRenderer::UploadAndDraw()
{
    const uint32_t count3D = m_geometry.Count3D();
    const uint32_t count2D = m_geometry.Count2D();
    const uint32_t total = count3D + count2D;
    if (total == 0)
        return;

    const GLsizei stride = sizeof(StagedGeometry::Vertex);
    const GLsizei bytes = static_cast<GLsizei>(total) * stride;

    gl_BindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    if (bytes > m_vertexBufferCapacity)
        m_vertexBufferCapacity = bytes * 2; // headroom, so a growing scene stops resizing

    // Orphan the whole buffer, then fill the two spans in place: 3D first, then
    // 2D, so each pass draws a contiguous slice. Orphaning lets the driver hand
    // back fresh storage instead of waiting on the in-flight frame.
    gl_BufferData(GL_ARRAY_BUFFER, m_vertexBufferCapacity, nullptr, GL_STREAM_DRAW);
    if (count3D)
        gl_BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptrARB_>(count3D) * stride, m_geometry.Vertices3D());
    if (count2D)
        gl_BufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptrARB_>(count3D) * stride, static_cast<GLsizeiptrARB_>(count2D) * stride, m_geometry.Vertices2D());

    SetupVertexAttributes();

    gl_UseProgram(m_program);
    gl_Uniform1i(m_uniformTexture, 0);
    gl_ActiveTexture(GL_TEXTURE0);

    float matrix[16];

    // --- 3D: one draw per texture run --------------------------------------
    if (count3D > 0)
    {
        glEnable(GL_DEPTH_TEST);
        StagedGeometry::BuildViewProjection(m_drawLists.GetCamera3D(), m_width, m_height, false, matrix);
        gl_UniformMatrix4fv(m_uniformViewProj, 1, GL_FALSE, matrix);

        const StagedGeometry::DrawRun* runs = m_geometry.Runs();
        for (uint32_t i = 0; i < m_geometry.RunCount(); ++i)
        {
            glBindTexture(GL_TEXTURE_2D, runs[i].texture ? runs[i].texture : m_whiteTexture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(runs[i].first), static_cast<GLsizei>(runs[i].count));
        }
    }

    // --- 2D: unlit, blended, drawn over the top, one draw per texture run ---
    if (count2D > 0)
    {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        StagedGeometry::BuildOrtho2D(m_width, m_height, false, matrix);
        gl_UniformMatrix4fv(m_uniformViewProj, 1, GL_FALSE, matrix);

        const StagedGeometry::DrawRun* runs2D = m_geometry.Runs2D();
        for (uint32_t i = 0; i < m_geometry.RunCount2D(); ++i)
        {
            glBindTexture(GL_TEXTURE_2D, runs2D[i].texture ? runs2D[i].texture : m_whiteTexture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(count3D + runs2D[i].first), static_cast<GLsizei>(runs2D[i].count));
        }
        glDisable(GL_BLEND);
    }
}

void OpenGlRenderer::EndFrame()
{
    if (!m_initialized)
        return;

    glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    UploadAndDraw();

    SwapBuffers(m_dc);

    // 2D is consumed here, not at BeginFrame: this is when the game has finished
    // submitting it.
    m_geometry.EndFrame();

    m_drawLists.SetLastStats(m_frameStats);
    m_drawLists.Reset(false);
}


// ---------------------------------------------------------------------------
// Render-to-image (Ui_Image3D)
// ---------------------------------------------------------------------------

bool OpenGlRenderer::EnsureImageTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;
    if (!gl_GenFramebuffers || !gl_BindFramebuffer || !gl_FramebufferTexture2D || !gl_CheckFramebufferStatus || !gl_GenRenderbuffers || !gl_BindRenderbuffer || !gl_RenderbufferStorage ||
        !gl_FramebufferRenderbuffer)
        return false; // this context has no FBO support -- see GlApi.h

    if (m_imageFbo != 0 && m_imageWidth == width && m_imageHeight == height)
        return true;

    if (m_imageFbo == 0)
    {
        gl_GenFramebuffers(1, &m_imageFbo);
        glGenTextures(1, &m_imageColorTex);
        gl_GenRenderbuffers(1, &m_imageDepthRb);
    }
    if (!m_imageFbo || !m_imageColorTex || !m_imageDepthRb)
        return false;

    GLint prevTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
    glBindTexture(GL_TEXTURE_2D, m_imageColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture));

    gl_BindRenderbuffer(GL_RENDERBUFFER, m_imageDepthRb);
    gl_RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    gl_BindRenderbuffer(GL_RENDERBUFFER, 0);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    gl_BindFramebuffer(GL_FRAMEBUFFER, m_imageFbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_imageColorTex, 0);
    gl_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_imageDepthRb);
    const bool complete = gl_CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl_BindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

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
    if (!m_initialized)
        return 0;
    if (!EnsureImageTarget(width, height))
        return 0;
    if (!m_imageGeometry.BuildOne(what, m_drawLists))
        return 0;

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    gl_BindFramebuffer(GL_FRAMEBUFFER, m_imageFbo);
    glViewport(0, 0, width, height);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const uint32_t count = m_imageGeometry.Count3D();
    if (count > 0)
    {
        const GLsizei stride = sizeof(StagedGeometry::Vertex);
        const GLsizei bytes = static_cast<GLsizei>(count) * stride;

        // Safe to share the ordinary vertex buffer: this call blocks on
        // glFinish() below before returning, so nothing overwrites it before
        // this draw has consumed it, and EndFrame()'s own upload -- which
        // runs later this same frame -- refills it fresh regardless.
        gl_BindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
        if (bytes > m_vertexBufferCapacity)
            m_vertexBufferCapacity = bytes * 2;
        gl_BufferData(GL_ARRAY_BUFFER, m_vertexBufferCapacity, nullptr, GL_STREAM_DRAW);
        gl_BufferSubData(GL_ARRAY_BUFFER, 0, bytes, m_imageGeometry.Vertices3D());
        SetupVertexAttributes();

        gl_UseProgram(m_program);
        gl_Uniform1i(m_uniformTexture, 0);
        gl_ActiveTexture(GL_TEXTURE0);

        float matrix[16];
        StagedGeometry::BuildViewProjection(camera, static_cast<uint32_t>(width), static_cast<uint32_t>(height), false, matrix);
        // An FBO colour attachment's row 0 is GL's window-space bottom, the
        // opposite of an uploaded texture's row 0 -- negating clip-space Y
        // here keeps every sampler of the returned handle agree on "row 0 is
        // the top" regardless of whether the source was uploaded or rendered.
        matrix[1] = -matrix[1];
        matrix[5] = -matrix[5];
        matrix[9] = -matrix[9];
        matrix[13] = -matrix[13];
        gl_UniformMatrix4fv(m_uniformViewProj, 1, GL_FALSE, matrix);

        const StagedGeometry::DrawRun* runs = m_imageGeometry.Runs();
        for (uint32_t i = 0; i < m_imageGeometry.RunCount(); ++i)
        {
            glBindTexture(GL_TEXTURE_2D, runs[i].texture ? runs[i].texture : m_whiteTexture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(runs[i].first), static_cast<GLsizei>(runs[i].count));
        }
    }

    // Must be sampleable before this returns: the UI compositing it this same
    // frame has no later point to pick it up.
    glFinish();

    gl_BindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    return static_cast<uint32_t>(m_imageColorTex);
}

// ---------------------------------------------------------------------------
// Cameras and lifecycle
// ---------------------------------------------------------------------------

void OpenGlRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }
void OpenGlRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }
void OpenGlRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }

bool OpenGlRenderer::IsInitialized() const { return m_initialized; }

void OpenGlRenderer::Shutdown()
{
    if (!m_initialized)
        return;

    for (int i = 0; i < GL_MAX_RESIDENT_TEXTURES; ++i)
    {
        if (m_textures[i])
            glDeleteTextures(1, &m_textures[i]);
    }
    if (m_whiteTexture)
        glDeleteTextures(1, &m_whiteTexture);
    if (m_vertexBuffer)
        gl_DeleteBuffers(1, &m_vertexBuffer);
    if (m_vao && gl_DeleteVertexArrays)
        gl_DeleteVertexArrays(1, &m_vao);
    if (m_program)
        gl_DeleteProgram(m_program);

    if (m_imageColorTex)
        glDeleteTextures(1, &m_imageColorTex);
    if (m_imageDepthRb && gl_DeleteRenderbuffers)
        gl_DeleteRenderbuffers(1, &m_imageDepthRb);
    if (m_imageFbo && gl_DeleteFramebuffers)
        gl_DeleteFramebuffers(1, &m_imageFbo);

    wglMakeCurrent(nullptr, nullptr);
    if (m_context)
        wglDeleteContext(m_context);
    if (m_dc && m_hwnd)
        ReleaseDC(m_hwnd, m_dc);

    m_initialized = false;
}

RendererType OpenGlRenderer::GetRendererType() const { return RendererType::OpenGl; }
DrawStats OpenGlRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }
Camera3D OpenGlRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }

// The Renderer interface exposes these as separate phases; this backend builds
// everything in Render() and submits once in EndFrame(), so they stay empty.
void OpenGlRenderer::RenderSkybox(const DrawLists& lists) { UNUSED_VAR(lists); }
void OpenGlRenderer::RenderPrimitives(DrawLists& lists) { UNUSED_VAR(lists); }
void OpenGlRenderer::RenderModels(const DrawLists& lists) { UNUSED_VAR(lists); }
