#pragma once

#include "PlatformConstants.h"
#include "core/EngineCore.h"
#include "graphics/Renderer.h"
#include "graphics/StagedGeometry.h"

#include <EGL/egl.h>
#include <glad/glad.h>

#define NXGL_MAX_RESIDENT_TEXTURES 256

/// The reference nx backend: a core-profile OpenGL 4.3 context from the Mesa port, attached to the
/// system window through EGL. Shares no code with the Win32 backend of the same name.
/// See docs/nx/renderers/OPENGL.md.
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

    /// Draw one object into a framebuffer object with clip-space vertical negated, finishing the pipeline before returning.
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
    bool CreateContext();
    bool CreateProgram();
    bool CreatePbrProgram();
    bool CreateShadowProgram();
    bool CreateWhiteTexture();
    bool CreateDefaultMaterialTextures();
    bool EnsureShadowMap();
    void SetupVertexAttributes();
    void SetViewProjection(const float matrix[16]);
    void UploadAndDraw();
    void DestroyContext();

    /// (Re)allocate the offscreen target at this size if it does not already match.
    /// @return False when allocation failed; RenderToImage3D answers 0 in that case.
    bool EnsureImageTarget(int width, int height);

    EGLDisplay m_display;
    EGLContext m_context;
    EGLSurface m_surface;

    // Flat/unlit program: the 2D pass and RenderToImage3D's preview target
    // (deliberately -- see the member comment on the equivalent Win32
    // pipeline in WebGpu.h: a preview's lighting must never depend on the
    // scene's own dynamic lights/shadow caster).
    GLuint m_program;
    GLuint m_vao;
    GLuint m_vertexBuffer;
    GLsizeiptr m_vertexBufferCapacity;
    GLuint m_uniformBuffer; // binding 0, bound only while m_program is in use

    // PBR program: the main scene pass. uFrame/uMat mirror the WGSL/GLSL
    // FrameUniforms/MaterialUniform blocks in WebGpu.cpp/OpenGl.cpp (Win32).
    GLuint m_pbrProgram;
    GLuint m_pbrFrameUniformBuffer; // binding 0, bound only while m_pbrProgram is in use
    GLuint m_pbrMaterialUniformBuffer; // binding 1, one 256-byte-strided slot per draw run this frame

    // Depth-only shadow program: dynamic geometry only.
    GLuint m_shadowProgram;
    GLuint m_shadowUniformBuffer; // binding 0, bound only while m_shadowProgram is in use
    GLuint m_shadowFbo;
    GLuint m_shadowColorTex; // depth written into the R channel -- see scene_shadow.frag.glsl
    GLuint m_shadowDepthRb; // the shadow pass's own depth test; never sampled

    // PBR defaults for a material with no normal/ORM map of its own: flat
    // tangent-space normal, and occlusion=1/roughness=1/metallic=0.
    GLuint m_defaultNormalTexture;
    GLuint m_defaultOrmTexture;

    // Result of this frame's RenderShadowMap, consumed by UploadAndDraw right
    // afterwards.
    float m_lastLightViewProj[16];
    bool m_shadowActive;
    LightID m_shadowCasterIndex;

    // This frame's already-uploaded (and possibly overflow-clamped) 3D
    // vertex count -- set by UploadAndDraw before it calls RenderShadowMap,
    // which has no other way to know how much of m_vertexBuffer is actually
    // valid 3D data this frame.
    uint32_t m_frame3DVerticesUploaded;

    GLuint m_whiteTexture;
    GLuint m_textures[NXGL_MAX_RESIDENT_TEXTURES];

    StagedGeometry m_geometry;

    Color3 m_clearColor;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_cropWidth;
    uint32_t m_cropHeight;

    DrawStats m_frameStats;
    bool m_initialized;

    StagedGeometry m_imageGeometry;
    GLuint m_imageFbo;
    GLuint m_imageColorTex;
    GLuint m_imageDepthRb;
    int m_imageWidth;
    int m_imageHeight;
};
