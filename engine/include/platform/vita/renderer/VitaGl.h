#pragma once

#include "core/EngineCore.h"
#include "graphics/Renderer.h"
#include "graphics/StagedGeometry.h"

#define VITAGL_MAX_RESIDENT_TEXTURES 256

#define VITAGL_LEGACY_POOL_BYTES (1 * 1024 * 1024)

/// Fallback Vita backend: a fixed-function subset over sceGxm, provided by the
/// vendored vitaGL library. Requires the player-supplied shader compiler.
class VitaGlRenderer final : public Renderer
{
public:
    VitaGlRenderer() = delete;
    explicit VitaGlRenderer(const EngineConfig& config);
    ~VitaGlRenderer() override = default;

    VitaGlRenderer(const VitaGlRenderer&) = delete;
    VitaGlRenderer(VitaGlRenderer&&) = delete;
    VitaGlRenderer& operator=(const VitaGlRenderer&) = delete;
    VitaGlRenderer& operator=(VitaGlRenderer&&) = delete;

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

    /// Upload a cooked texture.
    /// @param upload Source texture; expanded to RGBA8.
    /// @return A handle, or 0 on failure.
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

private:
    bool CreateWhiteTexture();
    void BindVertexArrays(const StagedGeometry::Vertex* base);
    void DrawStagedGeometry();

    uint32_t m_whiteTexture;
    uint32_t m_textures[VITAGL_MAX_RESIDENT_TEXTURES];

    StagedGeometry m_geometry;

    Color3 m_clearColor;
    uint32_t m_width;
    uint32_t m_height;

    DrawStats m_frameStats;
    bool m_initialized;

    // RenderToImage3D's scratch target (Ui_Image3D): a separate StagedGeometry
    // so staging one preview object never discards whatever the ordinary
    // per-frame path (m_geometry) has already built, and one FBO (colour
    // texture + depth renderbuffer) reused and resized on demand. Plain
    // uint32_t rather than GLuint, matching m_textures/m_whiteTexture above,
    // so this header carries no vitaGL type dependency.
    StagedGeometry m_imageGeometry;
    uint32_t m_imageFbo;
    uint32_t m_imageColorTex;
    uint32_t m_imageDepthRb;
    int m_imageWidth;
    int m_imageHeight;

    bool EnsureImageTarget(int width, int height);
};
