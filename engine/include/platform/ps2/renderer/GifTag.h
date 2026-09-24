#pragma once

#include "core/EngineCore.h"
#include "graphics/DrawList.h"
#include "graphics/Frustum.h"
#include "graphics/Renderer.h"

extern "C" {
#include <draw.h>
#include <graph.h>
#include <packet2.h>
}

class GifTagRenderer final : public Renderer
{
    bool m_initialized = false;
    bool m_inFrame = false;

    Color3 m_clearColor{0.0f, 0.0f, 0.0f};

    framebuffer_t m_frame[2];
    zbuffer_t m_z;
    int m_drawBuffer = 0;
    int m_displayBuffer = 0;

    packet2_t* m_geomBuf[GFX_GIFTAG_PACKET_BUFFERS] = {nullptr, nullptr};
    packet2_t* m_geom = nullptr;
    packet2_t* m_env = nullptr;
    uint8_t m_geomIndex = 0;
    bool m_framePending = false;
    bool m_fogEnabled = true;
    bool m_lodFadeEnabled = true;

    xyz_t* m_xyz = nullptr;
    uint32_t* m_srcIdx = nullptr;
    float* m_q = nullptr;
    // Per-source-vertex lit RGBA (4 floats/vertex), computed by
    // ComputeLitVertexColors before a draw call and consumed by
    // DrawTriangles/DrawStrip exactly like m_q/m_xyz -- a scratch buffer,
    // not accumulated across the frame.
    float* m_litColors = nullptr;

    bool m_useVu0 = false;
    void* m_clipBatch = nullptr;
    void* m_vecBatch = nullptr;

    /// Decide whether the coprocessor batch transform reproduces the scalar
    /// one, and enable it only if it does.
    void SelfTestVu0Transform();
    /// Transform one strip into the vertex scratch, in GS coordinates.
    /// @param mvp The combined model-to-clip matrix.
    /// @param components Floats per position: 3, or 4 for baked geometry.
    /// @param count Vertices to transform.
    void TransformStrip(const float mvp[16], const float* verts, int components, uint32_t count);

    uint32_t m_texHeapBase = 0;
    uint32_t m_texHeapWords = 0;
    static constexpr int TAG_MAX_VRAM_EXTENTS = 128;
    struct VramExtent
    {
        uint32_t addr;
        uint32_t words;
        bool used;
    };
    VramExtent m_vramExtents[TAG_MAX_VRAM_EXTENTS]{};
    int m_vramExtentCount = 0;

    /// Claim an extent of the texture heap.
    /// @param words Size in GS words, rounded up to the hardware alignment.
    /// @return Its GS word address, or zero when the heap cannot satisfy it.
    uint32_t VramAlloc(uint32_t words);

    /// Return an extent to the heap and coalesce it with free neighbours.
    /// @param addr An address previously returned by VramAlloc.
    void VramFree(uint32_t addr);

    static constexpr uint16_t TAG_MAX_TEXTURES = 64;
    struct TexEntry
    {
        bool inUse;
        uint32_t gsAddr;
        uint32_t mipAddr[TEX_MAX_MIP_LEVELS];
        uint8_t mipCount;
        uint32_t clutAddr;
        uint32_t vramBase;
        int width;
        int height;
        int psm;
        TextureFilter filter;
    };
    TexEntry m_textures[TAG_MAX_TEXTURES]{};

    static constexpr uint16_t TAG_MAX_2D_QUADS = UI_MAX_QUADS + GFX_MAX_2D_EXTRA;
    Quad2D m_quads2D[TAG_MAX_2D_QUADS];
    uint16_t m_quad2DCount = 0;
    uint16_t m_droppedQuads2D = 0;
    uint32_t m_reserved2DQw = 0;

    uint16_t m_frameVertsUsed = 0;
    uint16_t m_frameDroppedObjects = 0;

    bool m_backfaceCull = true;

    uint32_t m_lastBoundTex = 0;

    DrawStats m_frameStats{};

    /// Compose a model matrix from a position, an Euler rotation and a scale.
    /// @param out Receives the column-major result.
    static void BuildModelMatrix(float out[16], const Vector3& pos, const Vector3& rot, const Vector3& scl);

    /// Transform and emit one unindexed triangle list.
    /// @param mvp The combined model-to-clip matrix.
    /// @param components Floats per position: 3, or 4 for baked geometry.
    /// @param uvs Two floats per vertex, or null when untextured.
    /// @param colors Four floats (RGBA) per vertex, or null to use `color`
    ///        for every vertex -- see ComputeLitVertexColors, which is what
    ///        callers use to fill this in.
    /// @param textureId A backend texture handle, or zero when untextured.
    /// @param alpha Whole-batch opacity; below one the batch is alpha-blended.
    void DrawTriangles(const float mvp[16], const float* verts, int components, const float* uvs, const float* colors, uint32_t vertexCount, Color3 color, uint32_t textureId, float alpha = 1.0f);

    /// Transform and emit one triangle strip, split into runs of visible
    /// vertices.
    /// @param mvp The combined model-to-clip matrix.
    /// @param components Floats per position: 3, or 4 for baked geometry.
    /// @param uvs Two floats per vertex, or null when untextured.
    /// @param colors Four floats (RGBA) per vertex, or null to use `color`
    ///        for every vertex.
    /// @param textureId A backend texture handle, or zero when untextured.
    /// @param alpha Whole-batch opacity; below one the batch is alpha-blended.
    void DrawStrip(const float mvp[16], const float* verts, int components, const float* uvs, const float* colors, uint32_t vertexCount, Color3 color, uint32_t textureId, float alpha = 1.0f);

    /// Compute ambient + dynamic-light Lambertian shading per vertex and
    /// write RGBA into `outColors` (4 floats/vertex, sized for at least
    /// `vertexCount`). Two things combine, exactly like every PBR-tier
    /// backend's own vertex-colour handling: a per-vertex lighting baseline
    /// (`baseColors`, RGBA -- the level compiler's baked result for sector
    /// geometry -- or full white when null, meaning nothing is baked for
    /// this geometry yet, e.g. a model or primitive), which ambient and every
    /// active light are ADDED to (never multiplied into a sum that starts at
    /// zero -- that would erase a bake, or leave unlit dynamic geometry black
    /// whenever no light happens to be configured); and a flat per-mesh tint
    /// (`flatColorRgba` -- a material's baseColorFactor, or a primitive's own
    /// colour) multiplied in once at the end. See
    /// docs/subsystems/RENDERER.md.
    /// @param verts Object-space (or world-space, see worldMatrix) positions.
    /// @param normals Object-space (or world-space) normals, or null; a
    ///        vertex with no normal data gets no light contribution (the
    ///        baseline passes through unlit) rather than being forced to any
    ///        particular brightness.
    /// @param baseColors Four floats (RGBA) per vertex, or null for a full-
    ///        white baseline (dynamic geometry).
    /// @param flatColorRgba The flat tint multiplied into every vertex.
    /// @param worldMatrix Transforms verts/normals into world space before
    ///        lighting; null means they are already world-space (level
    ///        sectors, baked that way by the compiler).
    void ComputeLitVertexColors(const float* verts, int components, const float* normals, const float* baseColors, const float flatColorRgba[4], const float* worldMatrix, uint32_t vertexCount,
                                 float* outColors);
    /// Write the sampling and buffer registers for a texture, unless that same
    /// texture is already bound this frame.
    void BindTexture(uint32_t textureId);

    /// Emit the queued screen-space quads and empty the queue.
    void FlushQuads2D();
    /// Name the primitive the following batch draws, and its attributes.
    void EmitPrim(uint32_t prim);

    /// Emit the framebuffer and depth clear for the current back buffer.
    void EmitClear(const Color3& color);
    /// Draw the resident level sectors, culled whole against the view frustum.
    /// @param vp The composed view-projection; sector geometry is world-space.
    /// @param frustum The same frustum the rest of the frame culled against.
    void RenderLevel(const float vp[16], const FrustumPlanes& frustum);
    /// @return Packet cost of binding `textureId`, or zero if already bound.
    uint32_t BindCostQwords(uint32_t textureId) const;

    /// @param qwNeeded Quadwords the caller is about to write.
    /// @return True when they fit alongside the reserved screen-space work and
    ///         the packet termination headroom.
    bool PacketHasSpace(uint32_t qwNeeded) const;

public:
    GifTagRenderer() = delete;
    explicit GifTagRenderer(const EngineConfig& config);

    GifTagRenderer(const GifTagRenderer&) = delete;
    GifTagRenderer(GifTagRenderer&&) = delete;
    GifTagRenderer& operator=(const GifTagRenderer&) = delete;
    GifTagRenderer& operator=(GifTagRenderer&&) = delete;

    ~GifTagRenderer() override = default;

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
    uint32_t GetTextureBudgetBytes() const override;

    uint32_t RenderToImage3D(const Renderable3D& what, const Camera3D& camera, int width, int height, const Color3& clearColor) override;

    bool IsInitialized() const override;
    void Shutdown() override;

    void SetFogEnabled(bool enabled) override { m_fogEnabled = enabled; }
    void SetLodFadeEnabled(bool enabled) override { m_lodFadeEnabled = enabled; }

    DrawStats GetLastStats() const override;
    Camera3D GetActiveCamera3D() const override;

protected:
    void RenderSkybox(const DrawLists& lists) override;
    void RenderPrimitives(DrawLists& lists) override;
    void RenderModels(const DrawLists& lists) override;

private:
    // RenderToImage3D's scratch target: a square colour+depth surface in GS
    // VRAM, reserved once at construction (see GFX_PS2_IMAGE_TARGET_SIZE) and
    // never freed or resized. GS words, in the same address space UploadTexture
    // places ordinary textures in -- the same "framebuffer memory is texture
    // memory" trick as every other PS2-generation renderer.
    uint32_t m_imageColorAddr = 0;
    uint32_t m_imageDepthAddr = 0;
    int m_imageTextureSlot = -1; // index into m_textures[], or -1 before first use
};
