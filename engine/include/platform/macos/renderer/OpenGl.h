#pragma once

#include <OpenGL/gl3.h>

#include "core/EngineCore.h"
#include "graphics/Renderer.h"
#include "graphics/StagedGeometry.h"

#define GL_MAX_RESIDENT_TEXTURES 256

/// The OpenGL backend on Apple's driver: a 4.1 core-profile context, the highest version macOS grants. Apple
/// deprecated OpenGL and froze it at 4.1, so this exists as the fallback for the default WebGPU backend.
class OpenGlRenderer final : public Renderer
{
public:
    OpenGlRenderer() = delete;
    explicit OpenGlRenderer(const EngineConfig& config);
    ~OpenGlRenderer() override = default;

    OpenGlRenderer(const OpenGlRenderer&) = delete;
    OpenGlRenderer(OpenGlRenderer&&) = delete;
    OpenGlRenderer& operator=(const OpenGlRenderer&) = delete;
    OpenGlRenderer& operator=(OpenGlRenderer&&) = delete;

    RendererType GetRendererType() const override;

    void AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale) override;
    void AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color) override;
    void AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, int32_t textureId) override;
    void AddPrimitiveToDrawList(Primitive3D primitive, const Vector3& position, const Vector3& rotation, const Vector3& scale, Color3 color, int32_t textureId) override;
    void AddLevelToDrawList(const Level& level) override;
    void AddModelToDrawList(int32_t modelId, const Vector3& position, const Vector3& rotation, const Vector3& scale) override;
    void AddSkyToDrawList(int32_t resourceId) override;
    void ClearDrawLists() override;

    void Render() override;
    void BeginFrame() override;
    void EndFrame() override;
    void ClearFrame(const Color3& color) override;
    void DrawQuad2D(const Quad2D& quad) override;
    void DrawGrid(int32_t slices, float spacing) override;

    void SetCamera3D(CameraID id, const Camera3D& camera) override;
    void SetActiveCamera3D(CameraID id) override;
    void SetActiveCamera2D(const Camera2D& camera) override;

    void SetLight3D(LightID id, const Light3D& light) override;
    void SetAmbientLight(const Color3& color) override;
    void SetShadowCasterLight(LightID id) override;
    bool SupportsPbrShading() const override { return true; }

    uint32_t UploadTexture(const TextureUpload& upload) override;
    void ReleaseTexture(uint32_t handle) override;

    uint32_t RenderToImage3D(const Renderable3D& what, const Camera3D& camera, int width, int height, const Color3& clearColor) override;

    bool IsInitialized() const override;
    void Shutdown() override;

    DrawStats GetLastStats() const override;
    Camera3D GetActiveCamera3D() const override;

protected:
    void RenderSkybox(const DrawLists& lists) override;
    void RenderPrimitives(DrawLists& lists) override;
    void RenderModels(const DrawLists& lists) override;
    void RenderShadowMap(const DrawLists& lists) override;

private:
    bool CreateProgram();
    bool CreatePbrProgram();
    bool CreateShadowProgram();
    bool CreateWhiteTexture();
    bool CreateDefaultMaterialTextures();
    bool EnsureShadowMap();
    void SetupVertexAttributes();
    void UploadAndDraw();

    bool m_contextCreated;
    int m_versionMajor;
    int m_versionMinor;

    GLuint m_program;
    GLint m_uniformViewProj;
    GLint m_uniformTexture;

    GLuint m_programPbr;
    GLint m_pbrUniformViewProj;
    GLint m_pbrUniformCameraPos;
    GLint m_pbrUniformAmbient;
    GLint m_pbrUniformLightPosOrDir;
    GLint m_pbrUniformLightColorIntensity;
    GLint m_pbrUniformLightRange;
    GLint m_pbrUniformShadowCaster;
    GLint m_pbrUniformLightViewProj;
    GLint m_pbrUniformAlbedoTex;
    GLint m_pbrUniformNormalTex;
    GLint m_pbrUniformOrmTex;
    GLint m_pbrUniformShadowMap;
    GLint m_pbrUniformBaseColor;
    GLint m_pbrUniformEmissive;
    GLint m_pbrUniformMrna;
    GLint m_pbrUniformAlphaMask;

    GLuint m_shadowProgram;
    GLint m_shadowUniformLightViewProj;
    GLuint m_shadowFbo;
    GLuint m_shadowDepthTex;

    GLuint m_defaultNormalTex;
    GLuint m_defaultOrmTex;

    float m_lastLightViewProj[16];
    bool m_shadowActive;
    LightID m_shadowCasterIndex;

    GLuint m_vao;
    GLuint m_vertexBuffer;
    GLsizei m_vertexBufferCapacity;

    GLuint m_whiteTexture;
    GLuint m_textures[GL_MAX_RESIDENT_TEXTURES];

    StagedGeometry m_geometry;

    Color3 m_clearColor;
    uint32_t m_width;
    uint32_t m_height;

    DrawStats m_frameStats;
    bool m_initialized;

    StagedGeometry m_imageGeometry;
    GLuint m_imageFbo;
    GLuint m_imageColorTex;
    GLuint m_imageDepthRb;
    int m_imageWidth;
    int m_imageHeight;

    /// (Re)allocate the scratch image target at this size if it does not already match.
    /// @return False when allocation failed; RenderToImage3D answers 0.
    bool EnsureImageTarget(int width, int height);
};
