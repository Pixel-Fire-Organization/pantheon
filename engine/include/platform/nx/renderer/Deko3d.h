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

    void SetLight3D(LightID id, const Light3D& light) override;
    void SetAmbientLight(const Color3& color) override;
    void SetShadowCasterLight(LightID id) override;
    bool SupportsPbrShading() const override { return true; }

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
    void RenderShadowMap(const DrawLists& lists) override;

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

    /// Upload the flat-normal / neutral-ORM 1x1 textures a material with no
    /// normal/ORM map of its own samples. Ordinary resident-texture-registry
    /// entries (via UploadTexture), so Destroy()'s existing texture loop
    /// frees them like any other texture.
    bool CreateDefaultMaterialTextures();

    /// Allocate the fixed-size (GFX_SHADOW_MAP_SIZE) depth-only render target
    /// the real-time shadow pass renders into, and register its colour image
    /// as a resident texture slot so the PBR pass can sample it. Called once
    /// at construction, unlike EnsureImageTarget's lazy re-creation -- this
    /// target's size never changes.
    bool CreateShadowTarget();

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

    /// Record the depth/blend state for one pass kind, without touching
    /// shaders or uniforms -- split out of BindPassState so the PBR world
    /// pass, which uses its own frame/material uniform buffers instead of
    /// the flat shader's single matrix, can still share this part.
    void ApplyDepthBlendState(DkCmdBuf cmdbuf, PassKind kind);

    /// Record the depth, blend and uniform state for one pass kind (flat program only).
    void BindPassState(DkCmdBuf cmdbuf, PassKind kind, const float matrix[16]);

    /// Record one draw per run, clamped to the vertices actually uploaded.
    void DrawRuns(DkCmdBuf cmdbuf, const StagedGeometry::DrawRun* runs, uint32_t runCount, uint32_t base, uint32_t uploaded);

    /// Record one draw per run against the PBR program: a per-run material
    /// uniform write plus albedo/normal/ORM texture binds, clamped to the
    /// vertices actually uploaded. Always drawn at base 0 -- static sector
    /// and dynamic model/primitive runs share the one PBR pass; only the
    /// real-time shadow pass itself distinguishes dynamic from static.
    void DrawPbrRuns(DkCmdBuf cmdbuf, const StagedGeometry::DrawRun* runs, uint32_t runCount, uint32_t uploaded);

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

    // PBR program: the main 3D scene pass. Compiled into the same
    // m_shaderMemory block as the flat program above -- see CreateShaders().
    DkShader m_pbrVertexShader;
    DkShader m_pbrFragmentShader;
    DkMemBlock m_pbrFrameUniformMemory; // binding 0 (vertex+fragment) -- FrameUniforms
    DkMemBlock m_pbrMaterialUniformMemory; // binding 1 (fragment); one aligned slot per draw run this frame

    // Depth-only shadow program: dynamic (model/primitive) geometry only.
    DkShader m_shadowVertexShader;
    DkShader m_shadowFragmentShader;
    DkMemBlock m_shadowUniformMemory; // binding 0 (vertex) -- mat4 lightViewProj
    DkImage m_shadowColorImage; // depth written into the R channel -- see scene_shadow.frag.glsl
    DkImage m_shadowDepthImage; // the shadow pass's own depth test; never sampled
    DkMemBlock m_shadowColorMemory;
    DkMemBlock m_shadowDepthMemory;
    int m_shadowTextureSlot; // resident texture slot the PBR pass samples; never releasable, like m_imageTextureSlot

    // PBR defaults for a material with no normal/ORM map of its own: flat
    // tangent-space normal, and occlusion=1/roughness=1/metallic=0.
    uint32_t m_defaultNormalTexture;
    uint32_t m_defaultOrmTexture;

    // Result of this frame's RenderShadowMap, consumed by EndFrame's main pass right afterwards.
    float m_lastLightViewProj[16];
    bool m_shadowActive;
    LightID m_shadowCasterIndex;

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
