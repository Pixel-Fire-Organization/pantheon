#pragma once

#include "core/EngineCore.h"
#include "graphics/Renderer.h"
#include "graphics/StagedGeometry.h"

#define PSPGL_MAX_RESIDENT_TEXTURES 128

/// Fallback PSP backend: a fixed-function subset over the graphics hardware,
/// provided by the toolchain's own pspgl library. Textures live in video
/// memory, so its texture ceiling is far below the platform figure.
class PspGlRenderer final : public Renderer
{
public:
    PspGlRenderer() = delete;
    explicit PspGlRenderer(const EngineConfig& config);
    ~PspGlRenderer() override = default;

    PspGlRenderer(const PspGlRenderer&) = delete;
    PspGlRenderer(PspGlRenderer&&) = delete;
    PspGlRenderer& operator=(const PspGlRenderer&) = delete;
    PspGlRenderer& operator=(PspGlRenderer&&) = delete;

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
    /// @param upload Source texture; expanded to RGBA8, this model having no
    ///        palettised path.
    /// @return A handle, or 0 on failure.
    uint32_t UploadTexture(const TextureUpload& upload) override;
    void ReleaseTexture(uint32_t handle) override;

    /// @return What video memory leaves after the frame and depth buffers, not
    ///         the platform's main-memory figure.
    uint32_t GetTextureBudgetBytes() const override;

    bool IsInitialized() const override;
    void Shutdown() override;

    DrawStats GetLastStats() const override;
    Camera3D GetActiveCamera3D() const override;

protected:
    void RenderSkybox(const DrawLists& lists) override;
    void RenderPrimitives(DrawLists& lists) override;
    void RenderModels(const DrawLists& lists) override;

private:
    bool CreateContext();
    bool CreateWhiteTexture();
    void BindVertexArrays(const StagedGeometry::Vertex* base);
    void DrawStagedGeometry();

    void* m_display;
    void* m_surface;
    void* m_context;

    uint32_t m_whiteTexture;
    uint32_t m_textures[PSPGL_MAX_RESIDENT_TEXTURES];

    StagedGeometry m_geometry;

    Color3 m_clearColor;
    uint32_t m_width;
    uint32_t m_height;

    DrawStats m_frameStats;
    bool m_initialized;
};
