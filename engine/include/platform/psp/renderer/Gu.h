#pragma once

#include "core/EngineCore.h"
#include "graphics/Renderer.h"
#include "graphics/StagedGeometry.h"

#define GU_MAX_RESIDENT_TEXTURES 128

/// Default PSP backend: the native graphics interface, driven directly. The
/// graphics engine consumes a command list built each frame, and samples
/// textures from main memory, including palettised ones without expansion.
class GuRenderer final : public Renderer
{
public:
    GuRenderer() = delete;
    explicit GuRenderer(const EngineConfig& config);
    ~GuRenderer() override = default;

    GuRenderer(const GuRenderer&) = delete;
    GuRenderer(GuRenderer&&) = delete;
    GuRenderer& operator=(const GuRenderer&) = delete;
    GuRenderer& operator=(GuRenderer&&) = delete;

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

    /// Upload a cooked texture into main memory, keeping a palettised source
    /// palettised.
    /// @param upload Source texture.
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
    /// One vertex in the order the graphics engine reads it: texture, colour,
    /// position. The order is fixed by the hardware, not chosen. No normal:
    /// lighting is off, and this is the most bandwidth-bound platform here.
    struct GuVertex
    {
        float u, v;
        uint32_t color;
        float x, y, z;
    };

    struct TextureRecord
    {
        void* pixels;
        void* clut;
        uint16_t width;
        uint16_t height;
        uint8_t format;
        uint8_t filter;
        bool used;
    };

    bool AllocateBuffers();
    bool CreateWhiteTexture();
    void BindTexture(uint32_t handle);

    /// Convert a staged span into the hardware vertex order.
    /// @return How many vertices were written, which is capped by the buffer.
    uint32_t ConvertSpan(const StagedGeometry::Vertex* src, uint32_t count, GuVertex* dst, uint32_t dstCapacity) const;

    void DrawStagedGeometry();

    void* m_drawBuffer;
    void* m_dispBuffer;
    void* m_depthBuffer;
    void* m_displayList[GFX_PSP_DISPLAY_LIST_BUFFERS];
    uint32_t m_listIndex;

    GuVertex* m_vertices;
    uint32_t m_vertexCapacity;
    uint32_t m_count3D;
    uint32_t m_count2D;

    TextureRecord m_textures[GU_MAX_RESIDENT_TEXTURES];
    uint32_t m_whiteTexture;

    StagedGeometry m_geometry;

    Color3 m_clearColor;
    uint32_t m_width;
    uint32_t m_height;

    DrawStats m_frameStats;
    bool m_initialized;

    // RenderToImage3D's scratch target: a separate StagedGeometry so staging
    // one preview object never discards whatever the ordinary per-frame path
    // (m_geometry) has already built, and a dedicated display list rather
    // than m_displayList[m_listIndex], which EndFrame() may not have finished
    // with yet. Colour and depth are VRAM offsets in the same address space
    // GuRenderer's own draw/depth buffers use -- the same "framebuffer memory
    // is texture memory" trick as the PS2 GS backends -- placed once by
    // bumping s_VramOffset past what the constructor already claimed, and
    // never freed or resized: a second request at a different size is
    // refused, since nothing here tracks whether the first size's bytes are
    // still referenced by a display list that has not retired yet.
    StagedGeometry m_imageGeometry;
    void* m_imageDisplayList;
    void* m_imageColorBuffer;
    void* m_imageDepthBuffer;
    int m_imageWidth;
    int m_imageHeight;
    int m_imageTextureSlot; // index into m_textures[], or -1 before first use

    bool EnsureImageTarget(int width, int height);
};
