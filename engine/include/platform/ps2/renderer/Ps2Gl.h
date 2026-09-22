#pragma once

#include "core/EngineCore.h"
#include "graphics/DrawList.h"
#include "graphics/Frustum.h"
#include "graphics/Renderer.h"

// PS2GL renderer — renders through the ps2gl library (an OpenGL 1.1 subset that
// targets the GS via VU1 microcode + DMA chains). Owns full GS init (video mode,
// GS memory slots/areas, display + draw buffers), the double-buffer swap
// sequencing, primitive display-list compilation, and texture upload.
class Ps2GlRenderer final : public Renderer
{
    bool m_initialized = false;
    // True between BeginFrame() and EndFrame() — i.e. inside the ps2gl geometry
    // block. Guards ClearFrame(): a clear may only be recorded while a block is open.
    bool m_inFrame = false;
    // Set until the first frame has been dispatched. pglFinishRenderingGeometry
    // waits on a completion semaphore that only a prior pglRenderGeometry signals,
    // so it must be skipped on the very first EndFrame or it would block forever.
    bool m_firstFrame = true;

    float* m_megaBatch = nullptr;

    // Deferred clear color — ClearFrame() is invoked during the scripting phase
    // (before BeginFrame), so the actual glClear must happen inside the frame's
    // geometry block. We stash the color here and apply it in BeginFrame().
    Color3 m_clearColor{0.0f, 0.0f, 0.0f};

    // ps2gl primitive display lists (compiled once from DrawLists' separated
    // geometry arrays after the GL context is ready). Moved here from DrawLists
    // because display lists are a ps2gl-only concept.
    unsigned int m_dlCube = 0;
    unsigned int m_dlSphere = 0;
    unsigned int m_dlCylinder = 0;

    static constexpr uint16_t GL_MAX_2D_QUADS = UI_MAX_QUADS + GFX_MAX_2D_EXTRA;
    Quad2D m_quads2D[GL_MAX_2D_QUADS];
    uint16_t m_quad2DCount = 0;
    uint16_t m_droppedQuads2D = 0;

    uint16_t m_frameDrawCallsUsed = 0;

    // Per-frame throughput counters; snapshotted into DrawLists at EndFrame so
    // the PerfLogger sees a complete frame (including the measured GS wait).
    DrawStats m_frameStats{};

    // CPU-side view frustum for this frame, rebuilt in Render() from the same
    // fovy/near/far fed to glFrustum so culling matches what the GS draws.
    FrustumPlanes m_frustum{};

    // Whether ApplyDynamicLights turned GL_LIGHTING on this frame (at least
    // one active light or nonzero ambient). RenderSkybox forces lighting off
    // for its own draw regardless (the sky is never scene-lit) and needs this
    // to know what to restore afterwards.
    mutable bool m_lightingActive = false;

    // Scratch buffer for RenderLevel's per-vertex lit-colour computation
    // (VertexLighting_Compute), sized to GFX_GIFTAG_MAX_VERTS -- shared with
    // the sibling PS2 backend's own per-frame vertex ceiling, since sector
    // meshes are drawn one at a time regardless of which backend draws them.
    // Sector meshes are lit this way, on the EE, rather than through real
    // GL_LIGHTING: see the comment on RenderLevel for why a baked per-vertex
    // colour cannot safely drive fixed-function GL's own multiplicative
    // per-vertex material tracking.
    float* m_litColorScratch = nullptr;

    struct ModelDListEntry
    {
        int32_t resourceId = -1;
        unsigned int handles[GFX_MAX_MODEL_MESH_COUNT];
        int vertexCounts[GFX_MAX_MODEL_MESH_COUNT]; // for triangle throughput stats
        uint8_t topologies[GFX_MAX_MODEL_MESH_COUNT]; // MESH_TOPOLOGY_* per mesh
        uint8_t meshCount = 0;
    };

    ModelDListEntry m_modelDListCache[GFX_MAX_CACHED_MODELS]{};
    uint8_t m_modelDListCacheCount = 0;

    // Retained copies of uploaded texture pixels. ps2gl keeps the caller's
    // glTexImage2D pointer and re-reads it when a texture is evicted from GS
    // VRAM, but the resource loader hands us pixels in a shared, recycled IO
    // buffer — so we copy them into our own allocation and free it on release.
    static constexpr uint16_t GL_MAX_TEXTURES = 64;
    struct GLTexEntry
    {
        unsigned int name; // GL texture name (0 = free slot)
        void* pixels; // owned copy handed to glTexImage2D
    };
    GLTexEntry m_texRegistry[GL_MAX_TEXTURES]{};

    ModelDListEntry* FindOrCompileModelDLists(const Model* model, int32_t resourceId);
    void ClearModelDListCache();

    void InitGsMemory(bool pal); // replaces raylib's initGsMemoryForRaylib
    void CompilePrimitiveDLists(); // build DLists from DrawLists' arrays
    unsigned int GetListForType(Primitive3D type) const;
    void FlushQuads2D(); // draw queued 2D rects (ortho) inside the frame block

    void ApplyProjection(const Camera3D& camera) const;
    void ApplyCameraTransform(const Camera3D& camera) const;
    /// Push this frame's DrawLists lights/ambient into ps2gl's fixed-function
    /// GL_LIGHTING state. Must run while the modelview matrix is still the
    /// view-only transform (no per-object model matrix pushed yet) -- see the
    /// call site in Render().
    void ApplyDynamicLights() const;
    /// Set the current material's ambient/diffuse to `rgba` (a material's
    /// baseColorFactor, or a primitive's own flat colour) -- the tint used
    /// wherever GL_COLOR_ARRAY is not also bound for that draw.
    static void ApplyMaterialTint(const float rgba[4]);
    static Vector3 Normalize(const Vector3& value);
    static Vector3 Subtract(const Vector3& a, const Vector3& b);
    static Vector3 Cross(const Vector3& a, const Vector3& b);
    static float Dot(const Vector3& a, const Vector3& b);

public:
    Ps2GlRenderer() = delete;
    explicit Ps2GlRenderer(const EngineConfig& config);

    Ps2GlRenderer(const Ps2GlRenderer&) = delete;
    Ps2GlRenderer(Ps2GlRenderer&&) = delete;

    Ps2GlRenderer& operator=(const Ps2GlRenderer&) = delete;
    Ps2GlRenderer& operator=(Ps2GlRenderer&&) = delete;

    ~Ps2GlRenderer() override = default;

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

    // Draw the resident level sectors (queried from the sector manager, not the
    // draw lists). Not part of the Renderer interface — GL-backend specific.
    void RenderLevel();
};
