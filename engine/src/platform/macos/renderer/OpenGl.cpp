#include "platform/macos/renderer/OpenGl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Macros.h"
#include "core/EngineDebug.h"
#include "core/EngineMemory.h"
#include "graphics/ShaderAssets.h"
#include "graphics/TextureExpand.h"
#include "platform/Platform.h"
#include "platform/macos/renderer/GlContext.h"

namespace
{

    GLuint CompileShaderSources(GLenum type, const char* const* sources, GLsizei count)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, count, sources, nullptr);
        glCompileShader(shader);

        GLint status = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (!status)
        {
            char log[1024];
            GLsizei len = 0;
            glGetShaderInfoLog(shader, sizeof(log), &len, log);
            log[sizeof(log) - 1] = '\0';
            Engine_LogError("OpenGl: %s shader failed to compile: %s", (type == GL_VERTEX_SHADER) ? "vertex" : "fragment", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint CompileShaderAsset(GLenum type, const char* name)
    {
        ShaderSource source;
        if (!ShaderAssets_Load(name, &source))
            return 0;

        char defines[128];
        snprintf(defines, sizeof(defines), "#define GFX_MAX_LIGHTS %d\n#define GFX_SHADOW_MAP_SIZE %d.0\n", GFX_MAX_LIGHTS, GFX_SHADOW_MAP_SIZE);
        const char* sources[3] = {SHADER_GLSL_CORE_VERSION_LINE, defines, source.text.get()};
        return CompileShaderSources(type, sources, 3);
    }

} // namespace

OpenGlRenderer::OpenGlRenderer(const EngineConfig& config) :
    m_contextCreated(false), m_versionMajor(0), m_versionMinor(0), m_program(0), m_uniformViewProj(-1), m_uniformTexture(-1), m_programPbr(0), m_pbrUniformViewProj(-1), m_pbrUniformCameraPos(-1),
    m_pbrUniformAmbient(-1), m_pbrUniformLightPosOrDir(-1), m_pbrUniformLightColorIntensity(-1), m_pbrUniformLightRange(-1), m_pbrUniformShadowCaster(-1), m_pbrUniformLightViewProj(-1),
    m_pbrUniformAlbedoTex(-1), m_pbrUniformNormalTex(-1), m_pbrUniformOrmTex(-1), m_pbrUniformShadowMap(-1), m_pbrUniformBaseColor(-1), m_pbrUniformEmissive(-1), m_pbrUniformMrna(-1),
    m_pbrUniformAlphaMask(-1), m_shadowProgram(0), m_shadowUniformLightViewProj(-1), m_shadowFbo(0), m_shadowDepthTex(0), m_defaultNormalTex(0), m_defaultOrmTex(0), m_shadowActive(false),
    m_shadowCasterIndex(-1), m_vao(0), m_vertexBuffer(0), m_vertexBufferCapacity(0), m_whiteTexture(0), m_clearColor{0.0f, 0.0f, 0.0f}, m_width(0), m_height(0), m_frameStats{}, m_initialized(false),
    m_imageFbo(0), m_imageColorTex(0), m_imageDepthRb(0), m_imageWidth(0), m_imageHeight(0)
{
    UNUSED_VAR(config);
    memset(m_textures, 0, sizeof(m_textures));
    memset(m_lastLightViewProj, 0, sizeof(m_lastLightViewProj));

    Platform* platform = Engine_GetPlatform();
    platform->GetFramebufferSize(&m_width, &m_height);
    Engine_LogInfo("OpenGlRenderer: initializing (%ux%u)", m_width, m_height);

    float* arena = static_cast<float*>(Engine_GetSlot(ARENA_RENDERER, 0));
    if (!arena)
    {
        Engine_LogError("OpenGlRenderer: failed to retrieve ARENA_RENDERER slot 0");
        return;
    }
    m_drawLists.Init(arena);

    if (!MacosGl_Create())
        return;
    m_contextCreated = true;

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    m_versionMajor = major;
    m_versionMinor = minor;

    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    Engine_LogInfo("OpenGl: %s", version ? reinterpret_cast<const char*>(version) : "<unknown version>");
    Engine_LogInfo("OpenGl: %s", renderer ? reinterpret_cast<const char*>(renderer) : "<unknown renderer>");

    if (!CreateProgram())
        return;
    if (!CreateWhiteTexture())
        return;
    if (!CreateDefaultMaterialTextures())
        return;
    if (!EnsureShadowMap())
        return;
    if (!CreatePbrProgram())
        return;
    if (!CreateShadowProgram())
        return;

    glGenBuffers(1, &m_vertexBuffer);
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);

    m_initialized = true;
    Engine_LogInfo("OpenGlRenderer: ready (GL %d.%d core)", m_versionMajor, m_versionMinor);
}

bool OpenGlRenderer::CreateProgram()
{
    GLuint vs = CompileShaderAsset(GL_VERTEX_SHADER, "flat.vert.glsl");
    GLuint fs = CompileShaderAsset(GL_FRAGMENT_SHADER, "flat.frag.glsl");
    if (!vs || !fs)
        return false;

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);

    glBindAttribLocation(m_program, 0, "aPos");
    glBindAttribLocation(m_program, 1, "aNormal");
    glBindAttribLocation(m_program, 2, "aUv");
    glBindAttribLocation(m_program, 3, "aColor");

    glLinkProgram(m_program);

    GLint status = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &status);
    if (!status)
    {
        char log[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(m_program, sizeof(log), &len, log);
        log[sizeof(log) - 1] = '\0';
        Engine_LogError("OpenGl: program link failed: %s", log);
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    m_uniformViewProj = glGetUniformLocation(m_program, "uViewProj");
    m_uniformTexture = glGetUniformLocation(m_program, "uTexture");
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

bool OpenGlRenderer::CreateDefaultMaterialTextures()
{
    glGenTextures(1, &m_defaultNormalTex);
    if (!m_defaultNormalTex)
        return false;
    glBindTexture(GL_TEXTURE_2D, m_defaultNormalTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const uint8_t flatNormal[4] = {128, 128, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flatNormal);

    glGenTextures(1, &m_defaultOrmTex);
    if (!m_defaultOrmTex)
        return false;
    glBindTexture(GL_TEXTURE_2D, m_defaultOrmTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const uint8_t neutralOrm[4] = {255, 255, 0, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, neutralOrm);

    return true;
}

bool OpenGlRenderer::EnsureShadowMap()
{
    if (m_shadowFbo)
        return true;
    glGenTextures(1, &m_shadowDepthTex);
    glBindTexture(GL_TEXTURE_2D, m_shadowDepthTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, GFX_SHADOW_MAP_SIZE, GFX_SHADOW_MAP_SIZE, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

    glGenFramebuffers(1, &m_shadowFbo);
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowDepthTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

    if (!complete)
    {
        Engine_LogError("OpenGl: shadow map framebuffer is incomplete");
        return false;
    }
    return true;
}

bool OpenGlRenderer::CreatePbrProgram()
{
    GLuint vs = CompileShaderAsset(GL_VERTEX_SHADER, "pbr.vert.glsl");
    GLuint fs = CompileShaderAsset(GL_FRAGMENT_SHADER, "pbr.frag.glsl");
    if (!vs || !fs)
        return false;

    m_programPbr = glCreateProgram();
    glAttachShader(m_programPbr, vs);
    glAttachShader(m_programPbr, fs);

    glBindAttribLocation(m_programPbr, 0, "aPos");
    glBindAttribLocation(m_programPbr, 1, "aNormal");
    glBindAttribLocation(m_programPbr, 2, "aUv");
    glBindAttribLocation(m_programPbr, 3, "aColor");

    glLinkProgram(m_programPbr);

    GLint status = 0;
    glGetProgramiv(m_programPbr, GL_LINK_STATUS, &status);
    if (!status)
    {
        char log[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(m_programPbr, sizeof(log), &len, log);
        log[sizeof(log) - 1] = '\0';
        Engine_LogError("OpenGl: PBR program link failed: %s", log);
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    m_pbrUniformViewProj = glGetUniformLocation(m_programPbr, "uViewProj");
    m_pbrUniformCameraPos = glGetUniformLocation(m_programPbr, "uCameraPos");
    m_pbrUniformAmbient = glGetUniformLocation(m_programPbr, "uAmbient");
    m_pbrUniformLightPosOrDir = glGetUniformLocation(m_programPbr, "uLightPosOrDir");
    m_pbrUniformLightColorIntensity = glGetUniformLocation(m_programPbr, "uLightColorIntensity");
    m_pbrUniformLightRange = glGetUniformLocation(m_programPbr, "uLightRange");
    m_pbrUniformShadowCaster = glGetUniformLocation(m_programPbr, "uShadowCaster");
    m_pbrUniformLightViewProj = glGetUniformLocation(m_programPbr, "uLightViewProj");
    m_pbrUniformAlbedoTex = glGetUniformLocation(m_programPbr, "uAlbedoTex");
    m_pbrUniformNormalTex = glGetUniformLocation(m_programPbr, "uNormalTex");
    m_pbrUniformOrmTex = glGetUniformLocation(m_programPbr, "uOrmTex");
    m_pbrUniformShadowMap = glGetUniformLocation(m_programPbr, "uShadowMap");
    m_pbrUniformBaseColor = glGetUniformLocation(m_programPbr, "uBaseColor");
    m_pbrUniformEmissive = glGetUniformLocation(m_programPbr, "uEmissive");
    m_pbrUniformMrna = glGetUniformLocation(m_programPbr, "uMrna");
    m_pbrUniformAlphaMask = glGetUniformLocation(m_programPbr, "uAlphaMask");
    return true;
}

bool OpenGlRenderer::CreateShadowProgram()
{
    GLuint vs = CompileShaderAsset(GL_VERTEX_SHADER, "shadow.vert.glsl");
    GLuint fs = CompileShaderAsset(GL_FRAGMENT_SHADER, "shadow.frag.glsl");
    if (!vs || !fs)
        return false;

    m_shadowProgram = glCreateProgram();
    glAttachShader(m_shadowProgram, vs);
    glAttachShader(m_shadowProgram, fs);
    glBindAttribLocation(m_shadowProgram, 0, "aPos");
    glLinkProgram(m_shadowProgram);

    GLint status = 0;
    glGetProgramiv(m_shadowProgram, GL_LINK_STATUS, &status);
    if (!status)
    {
        char log[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(m_shadowProgram, sizeof(log), &len, log);
        log[sizeof(log) - 1] = '\0';
        Engine_LogError("OpenGl: shadow program link failed: %s", log);
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    m_shadowUniformLightViewProj = glGetUniformLocation(m_shadowProgram, "uLightViewProj");
    return true;
}

void OpenGlRenderer::SetupVertexAttributes()
{
    const GLsizei stride = sizeof(StagedGeometry::Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(sizeof(float) * 3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(sizeof(float) * 6));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(sizeof(float) * 8));
}

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
    const GLint filter = (upload.filter == TextureFilter::Nearest) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);

    m_textures[slot] = tex;

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
    Platform* platform = Engine_GetPlatform();
    const uint32_t previousWidth = m_width;
    const uint32_t previousHeight = m_height;
    platform->GetFramebufferSize(&m_width, &m_height);
    if (m_width != previousWidth || m_height != previousHeight)
        MacosGl_Update();

    m_geometry.SetFrameBudget(0, m_width, m_height);
    m_geometry.BeginFrame();
    m_frameStats = DrawStats{};
}

void OpenGlRenderer::Render() { m_geometry.BuildFrame(m_drawLists, &m_frameStats); }

void OpenGlRenderer::RenderShadowMap(const DrawLists& lists)
{
    m_shadowActive = false;

    const LightID casterId = lists.GetShadowCasterLight();
    if (casterId < 0 || casterId >= GFX_MAX_LIGHTS)
        return;
    const Light3D& caster = lists.GetLights()[casterId];
    if (caster.intensity <= 0.0f || caster.type != LightType::Directional)
        return;

    const uint32_t dynStart = m_geometry.DynamicVertexStart();
    const uint32_t dynCount = m_geometry.Count3D() - dynStart;
    if (dynCount == 0)
        return;

    const float kShadowHalfExtent = 24.0f;
    const float kShadowDepthExtent = 120.0f;
    StagedGeometry::BuildLightViewProjection(caster.direction, lists.GetCamera3D().position, kShadowHalfExtent, kShadowDepthExtent, false, m_lastLightViewProj);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glViewport(0, 0, GFX_SHADOW_MAP_SIZE, GFX_SHADOW_MAP_SIZE);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_shadowProgram);
    glUniformMatrix4fv(m_shadowUniformLightViewProj, 1, GL_FALSE, m_lastLightViewProj);
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(dynStart), static_cast<GLsizei>(dynCount));

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    m_shadowActive = true;
    m_shadowCasterIndex = casterId;
}

void OpenGlRenderer::UploadAndDraw()
{
    const uint32_t count3D = m_geometry.Count3D();
    const uint32_t count2D = m_geometry.Count2D();
    const uint32_t total = count3D + count2D;
    if (total == 0)
        return;

    const GLsizei stride = sizeof(StagedGeometry::Vertex);
    const GLsizei bytes = static_cast<GLsizei>(total) * stride;

    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    if (bytes > m_vertexBufferCapacity)
        m_vertexBufferCapacity = bytes * 2;

    glBufferData(GL_ARRAY_BUFFER, m_vertexBufferCapacity, nullptr, GL_STREAM_DRAW);
    if (count3D)
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count3D) * stride, m_geometry.Vertices3D());
    if (count2D)
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(count3D) * stride, static_cast<GLsizeiptr>(count2D) * stride, m_geometry.Vertices2D());

    SetupVertexAttributes();

    RenderShadowMap(m_drawLists);

    float matrix[16];

    if (count3D > 0)
    {
        glEnable(GL_DEPTH_TEST);
        glUseProgram(m_programPbr);

        StagedGeometry::BuildViewProjection(m_drawLists.GetCamera3D(), m_width, m_height, false, matrix);
        glUniformMatrix4fv(m_pbrUniformViewProj, 1, GL_FALSE, matrix);
        glUniformMatrix4fv(m_pbrUniformLightViewProj, 1, GL_FALSE, m_lastLightViewProj);

        const Camera3D& camera = m_drawLists.GetCamera3D();
        const float cameraPos[3] = {camera.position.x, camera.position.y, camera.position.z};
        glUniform3fv(m_pbrUniformCameraPos, 1, cameraPos);
        const Color3& ambient = m_drawLists.GetAmbientLight();
        const float ambientArr[3] = {ambient.r, ambient.g, ambient.b};
        glUniform3fv(m_pbrUniformAmbient, 1, ambientArr);

        float lightPosOrDir[GFX_MAX_LIGHTS * 4];
        float lightColorIntensity[GFX_MAX_LIGHTS * 4];
        float lightRange[GFX_MAX_LIGHTS * 4];
        const Light3D* lights = m_drawLists.GetLights();
        for (uint32_t i = 0; i < GFX_MAX_LIGHTS; ++i)
        {
            const Light3D& l = lights[i];
            const bool directional = (l.type == LightType::Directional);
            lightPosOrDir[i * 4 + 0] = directional ? l.direction.x : l.position.x;
            lightPosOrDir[i * 4 + 1] = directional ? l.direction.y : l.position.y;
            lightPosOrDir[i * 4 + 2] = directional ? l.direction.z : l.position.z;
            lightPosOrDir[i * 4 + 3] = directional ? 0.0f : 1.0f;
            lightColorIntensity[i * 4 + 0] = l.color.r;
            lightColorIntensity[i * 4 + 1] = l.color.g;
            lightColorIntensity[i * 4 + 2] = l.color.b;
            lightColorIntensity[i * 4 + 3] = l.intensity;
            lightRange[i * 4 + 0] = l.range;
            lightRange[i * 4 + 1] = 0.0f;
            lightRange[i * 4 + 2] = 0.0f;
            lightRange[i * 4 + 3] = 0.0f;
        }
        glUniform4fv(m_pbrUniformLightPosOrDir, GFX_MAX_LIGHTS, lightPosOrDir);
        glUniform4fv(m_pbrUniformLightColorIntensity, GFX_MAX_LIGHTS, lightColorIntensity);
        glUniform4fv(m_pbrUniformLightRange, GFX_MAX_LIGHTS, lightRange);
        glUniform1i(m_pbrUniformShadowCaster, m_shadowActive ? static_cast<GLint>(m_shadowCasterIndex) : -1);

        glUniform1i(m_pbrUniformAlbedoTex, 0);
        glUniform1i(m_pbrUniformNormalTex, 1);
        glUniform1i(m_pbrUniformOrmTex, 2);
        glUniform1i(m_pbrUniformShadowMap, 3);
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + 3));
        glBindTexture(GL_TEXTURE_2D, m_shadowDepthTex);

        const StagedGeometry::DrawRun* runs = m_geometry.Runs();
        for (uint32_t i = 0; i < m_geometry.RunCount(); ++i)
        {
            const StagedGeometry::RunMaterial& mat = runs[i].material;
            glUniform4fv(m_pbrUniformBaseColor, 1, mat.baseColor);
            glUniform3fv(m_pbrUniformEmissive, 1, mat.emissive);
            const float mrna[4] = {mat.metallic, mat.roughness, mat.normalScale, mat.alphaCutoff};
            glUniform4fv(m_pbrUniformMrna, 1, mrna);
            glUniform1f(m_pbrUniformAlphaMask, (mat.flags & MATERIAL_FLAG_ALPHA_MASK) ? 1.0f : 0.0f);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, runs[i].texture ? runs[i].texture : m_whiteTexture);
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + 1));
            glBindTexture(GL_TEXTURE_2D, mat.normalTexture ? mat.normalTexture : m_defaultNormalTex);
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + 2));
            glBindTexture(GL_TEXTURE_2D, mat.ormTexture ? mat.ormTexture : m_defaultOrmTex);

            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(runs[i].first), static_cast<GLsizei>(runs[i].count));
        }
        glActiveTexture(GL_TEXTURE0);
    }

    if (count2D > 0)
    {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(m_program);
        glUniform1i(m_uniformTexture, 0);
        glActiveTexture(GL_TEXTURE0);

        StagedGeometry::BuildOrtho2D(m_width, m_height, false, matrix);
        glUniformMatrix4fv(m_uniformViewProj, 1, GL_FALSE, matrix);

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

    MacosGl_Swap();

    m_geometry.EndFrame();

    m_drawLists.SetLastStats(m_frameStats);
    m_drawLists.Reset(false);
}

bool OpenGlRenderer::EnsureImageTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
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

    GLint prevTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
    glBindTexture(GL_TEXTURE_2D, m_imageColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture));

    glBindRenderbuffer(GL_RENDERBUFFER, m_imageDepthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_imageFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_imageColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_imageDepthRb);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

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

    glBindFramebuffer(GL_FRAMEBUFFER, m_imageFbo);
    glViewport(0, 0, width, height);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const uint32_t count = m_imageGeometry.Count3D();
    if (count > 0)
    {
        const GLsizei stride = sizeof(StagedGeometry::Vertex);
        const GLsizei bytes = static_cast<GLsizei>(count) * stride;

        glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
        if (bytes > m_vertexBufferCapacity)
            m_vertexBufferCapacity = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, m_vertexBufferCapacity, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, m_imageGeometry.Vertices3D());
        SetupVertexAttributes();

        glUseProgram(m_program);
        glUniform1i(m_uniformTexture, 0);
        glActiveTexture(GL_TEXTURE0);

        float matrix[16];
        StagedGeometry::BuildViewProjection(camera, static_cast<uint32_t>(width), static_cast<uint32_t>(height), false, matrix);
        matrix[1] = -matrix[1];
        matrix[5] = -matrix[5];
        matrix[9] = -matrix[9];
        matrix[13] = -matrix[13];
        glUniformMatrix4fv(m_uniformViewProj, 1, GL_FALSE, matrix);

        const StagedGeometry::DrawRun* runs = m_imageGeometry.Runs();
        for (uint32_t i = 0; i < m_imageGeometry.RunCount(); ++i)
        {
            glBindTexture(GL_TEXTURE_2D, runs[i].texture ? runs[i].texture : m_whiteTexture);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(runs[i].first), static_cast<GLsizei>(runs[i].count));
        }
    }

    glFinish();

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    return static_cast<uint32_t>(m_imageColorTex);
}

void OpenGlRenderer::SetCamera3D(CameraID id, const Camera3D& camera) { m_drawLists.SetCamera3D(id, camera); }
void OpenGlRenderer::SetActiveCamera3D(CameraID id) { m_drawLists.SetActiveCamera3D(id); }
void OpenGlRenderer::SetActiveCamera2D(const Camera2D& camera) { m_drawLists.SetActiveCamera2D(camera); }
void OpenGlRenderer::SetLight3D(LightID id, const Light3D& light) { m_drawLists.SetLight3D(id, light); }
void OpenGlRenderer::SetAmbientLight(const Color3& color) { m_drawLists.SetAmbientLight(color); }
void OpenGlRenderer::SetShadowCasterLight(LightID id) { m_drawLists.SetShadowCasterLight(id); }

bool OpenGlRenderer::IsInitialized() const { return m_initialized; }

void OpenGlRenderer::Shutdown()
{
    if (m_contextCreated)
    {
        for (int i = 0; i < GL_MAX_RESIDENT_TEXTURES; ++i)
        {
            if (m_textures[i])
                glDeleteTextures(1, &m_textures[i]);
        }
        if (m_whiteTexture)
            glDeleteTextures(1, &m_whiteTexture);
        if (m_defaultNormalTex)
            glDeleteTextures(1, &m_defaultNormalTex);
        if (m_defaultOrmTex)
            glDeleteTextures(1, &m_defaultOrmTex);
        if (m_vertexBuffer)
            glDeleteBuffers(1, &m_vertexBuffer);
        if (m_vao)
            glDeleteVertexArrays(1, &m_vao);
        if (m_program)
            glDeleteProgram(m_program);
        if (m_programPbr)
            glDeleteProgram(m_programPbr);
        if (m_shadowProgram)
            glDeleteProgram(m_shadowProgram);

        if (m_shadowDepthTex)
            glDeleteTextures(1, &m_shadowDepthTex);
        if (m_shadowFbo)
            glDeleteFramebuffers(1, &m_shadowFbo);

        if (m_imageColorTex)
            glDeleteTextures(1, &m_imageColorTex);
        if (m_imageDepthRb)
            glDeleteRenderbuffers(1, &m_imageDepthRb);
        if (m_imageFbo)
            glDeleteFramebuffers(1, &m_imageFbo);
    }

    MacosGl_Destroy();
    m_contextCreated = false;
    m_initialized = false;
}

RendererType OpenGlRenderer::GetRendererType() const { return RendererType::OpenGl; }
DrawStats OpenGlRenderer::GetLastStats() const { return m_drawLists.GetLastStats(); }
Camera3D OpenGlRenderer::GetActiveCamera3D() const { return m_drawLists.GetCamera3D(); }

void OpenGlRenderer::RenderSkybox(const DrawLists& lists) { UNUSED_VAR(lists); }
void OpenGlRenderer::RenderPrimitives(DrawLists& lists) { UNUSED_VAR(lists); }
void OpenGlRenderer::RenderModels(const DrawLists& lists) { UNUSED_VAR(lists); }
