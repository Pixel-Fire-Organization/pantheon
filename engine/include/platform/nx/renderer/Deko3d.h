#pragma once

#include "PlatformConstants.h"
#include "core/EngineCore.h"
#include "graphics/Renderer.h"
#include "graphics/StagedGeometry.h"

#include <deko3d.h>

#define DEKO3D_MAX_RESIDENT_TEXTURES 256

/// The default nx backend: deko3d command lists against the graphics processor, with the shared
/// shader source compiled at build time. See docs/nx/renderers/DEKO3D.md.
class Deko3dRenderer final : public Renderer
{
public:
    Deko3dRenderer() = delete;
    explicit Deko3dRenderer(const EngineConfig& config);
    ~Deko3dRenderer() override = default;

    Deko3dRenderer(const Deko3dRenderer&) = delete;
    Deko3dRenderer(Deko3dRenderer&&) = delete;
    Deko3dRenderer& operator=(const Deko3dRenderer&) = delete;
    Deko3dRenderer& operator=(Deko3dRenderer&&) = delete;

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

    /// Expand to RGBA8, stage in processor-visible memory and transfer into a texture block; waits for the queue.
    uint32_t UploadTexture(const TextureUpload& upload) override;

    /// Drain the queue, then free the texture's block.
    void ReleaseTexture(uint32_t handle) override;

    /// Record and submit one object into the offscreen target, waiting for the queue before returning.
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
    struct Texture
    {
        DkImage image;
        DkMemBlock memory;
        uint32_t sampler;
        bool used;
    };

    enum class PassKind : uint8_t
    {
        World,
        Screen
    };

    bool CreateDevice();
    bool CreateDisplay();
    bool CreateShaders();
    bool CreateBuffers();
    bool CreateSamplers();
    bool CreateWhiteTexture();

    /// Release every block and object in reverse order of creation, swapchain first. Safe on a partial construction.
    void Destroy();

    /// @return A mapped block of at least `size` bytes, rounded to the driver's block alignment, or null.
    DkMemBlock CreateBlock(uint32_t size, uint32_t flags);

    /// @return The shader, loaded from an embedded binary into the shared code block at `codeOffset`.
    bool LoadShader(const uint8_t* blob, uint32_t blobSize, uint32_t codeOffset, DkShader* outShader);

    /// Point the transfer command buffer at its memory, fresh.
    void BeginTransfer();

    /// Submit what the transfer command buffer recorded and wait for the queue.
    void FinishTransfer();

    /// Record writing one image descriptor slot into the descriptor set.
    void WriteImageDescriptor(DkCmdBuf cmdbuf, uint32_t slot, const DkImage& image);

    /// @return A free texture slot index, or -1.
    int FindFreeTextureSlot() const;

    void UploadVertices(uint32_t slice);

    /// Record the render-target binding, viewport, scissor, clear and the state every pass shares.
    void BeginPass(DkCmdBuf cmdbuf, const DkImageView& color, const DkImageView& depth, uint32_t width, uint32_t height, const Color3& clearColor, uint32_t slice);

    /// Record the depth, blend and uniform state for one pass kind.
    void BindPassState(DkCmdBuf cmdbuf, PassKind kind, const float matrix[16]);

    /// Record one draw per run, clamped to the vertices actually uploaded.
    void DrawRuns(DkCmdBuf cmdbuf, const StagedGeometry::DrawRun* runs, uint32_t runCount, uint32_t base, uint32_t uploaded);

    bool EnsureImageTarget(int width, int height);
    void DestroyImageTarget();

    DkDevice m_device;
    DkQueue m_queue;
    DkCmdBuf m_frameCmdbuf;
    DkCmdBuf m_transferCmdbuf;
    DkMemBlock m_frameCommandMemory;
    DkMemBlock m_transferCommandMemory;
    DkFence m_sliceFences[GFX_NX_FRAME_SLICES];
    uint32_t m_frameSlice;

    DkImage m_displayImages[GFX_NX_DISPLAY_BUFFERS];
    DkMemBlock m_displayMemory[GFX_NX_DISPLAY_BUFFERS];
    DkImage m_depthImage;
    DkMemBlock m_depthMemory;
    DkSwapchain m_swapchain;
    uint32_t m_cropWidth;
    uint32_t m_cropHeight;

    DkMemBlock m_shaderMemory;
    DkShader m_vertexShader;
    DkShader m_fragmentShader;

    DkMemBlock m_vertexMemory[GFX_NX_FRAME_SLICES];
    DkMemBlock m_uniformMemory;
    DkMemBlock m_descriptorMemory;

    Texture m_textures[DEKO3D_MAX_RESIDENT_TEXTURES];
    uint32_t m_whiteTexture;

    StagedGeometry m_geometry;

    Color3 m_clearColor;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_frame3DVertices;
    uint32_t m_frame2DVertices;
    uint32_t m_reportedOverflow;

    DrawStats m_frameStats;
    bool m_initialized;

    StagedGeometry m_imageGeometry;
    DkImage m_imageColor;
    DkImage m_imageDepth;
    DkMemBlock m_imageColorMemory;
    DkMemBlock m_imageDepthMemory;
    int m_imageWidth;
    int m_imageHeight;
    int m_imageTextureSlot;
};
