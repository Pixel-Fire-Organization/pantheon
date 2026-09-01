#pragma once

#include <cstdint>

#include "graphics/DrawList.h"
#include "graphics/Frustum.h"

#define GFX_MAX_DRAW_RUNS 1024
#define GFX_MAX_2D_RUNS 256

/// Processor-side geometry staging for backends that rebuild their vertex data
/// each frame and upload it once. Not used by the PS2 backends.
class StagedGeometry final
{
public:
    struct Vertex
    {
        float x, y, z;
        float nx, ny, nz;
        float u, v;
        float r, g, b, a;
    };

    // A contiguous span of vertices sharing one texture: the unit of a draw call.
    struct DrawRun
    {
        uint32_t first;
        uint32_t count;
        uint32_t texture; // backend texture handle; 0 = untextured
    };

    StagedGeometry();
    ~StagedGeometry();

    StagedGeometry(const StagedGeometry&) = delete;
    StagedGeometry(StagedGeometry&&) = delete;
    StagedGeometry& operator=(const StagedGeometry&) = delete;
    StagedGeometry& operator=(StagedGeometry&&) = delete;

    // Clear the 3D staging for a new frame. 2D is NOT cleared here: DrawQuad2D
    // runs during GameUpdate, before the renderer's BeginFrame, so wiping it at
    // frame start would discard what the game just submitted.
    void BeginFrame();

    // Clear the 2D staging and report any run overflow. Call after the frame
    // has been submitted.
    void EndFrame();

    /// Declare what the backend can accept this frame, before it is built.
    /// @param maxVertices Total vertex ceiling for the frame; 0 means unlimited.
    /// @param viewportWidth Framebuffer width, for the cull frustum's aspect.
    /// @param viewportHeight Framebuffer height.
    void SetFrameBudget(uint32_t maxVertices, uint32_t viewportWidth, uint32_t viewportHeight);

    // Build this frame's 3D geometry from the draw lists plus the resident level
    // sectors. `lists` is sorted by texture first, which is what lets runs
    // coalesce. Entries outside the frustum, and entries that would not fit the
    // budget, are rejected here rather than being built and then discarded.
    void BuildFrame(DrawLists& lists, DrawStats* stats);

    /// Stage exactly one model or primitive for Renderer::RenderToImage3D: no
    /// texture-run sorting, frustum culling or vertex-budget accounting,
    /// unlike BuildFrame's general path -- there is nothing to sort or cull
    /// when the whole frame is one object. Resets this instance's 3D staging
    /// first, so call this on a StagedGeometry instance dedicated to
    /// image-target rendering, never the one BuildFrame uses for the ordinary
    /// per-frame draw list: sharing one would discard whichever call ran
    /// first, since both reset the same 3D staging.
    /// @param what The model or primitive to draw, always at the origin.
    /// @param primitiveSource Read only for its primitive geometry tables
    ///        (cube/sphere/cylinder), immutable once a backend initialises
    ///        them -- ordinarily the backend's own per-frame DrawLists,
    ///        borrowed rather than duplicated.
    /// @return False when what.modelId names a model that is not ready, or a
    ///         primitive whose geometry is not initialised; nothing is
    ///         staged in that case.
    bool BuildOne(const Renderable3D& what, const DrawLists& primitiveSource);

    /// Stage one screen-space quad, coalescing it into the open 2D run when it
    /// shares that run's texture.
    /// @param quad The quad to stage, in framebuffer pixels.
    void AddQuad2D(const Quad2D& quad);

    const Vertex* Vertices3D() const { return m_verts3D; }
    uint32_t Count3D() const { return m_count3D; }
    const Vertex* Vertices2D() const { return m_verts2D; }
    uint32_t Count2D() const { return m_count2D; }
    const DrawRun* Runs() const { return m_runs; }
    uint32_t RunCount() const { return m_runCount; }

    /// @return This frame's screen-space runs, in draw order. Offsets are into
    ///         Vertices2D(), so a backend adds its own 3D span base.
    const DrawRun* Runs2D() const { return m_runs2D; }
    uint32_t RunCount2D() const { return m_runCount2D; }

    // Column-major, matching the PS2 path's convention.
    static void BuildModelMatrix(const Vector3& pos, const Vector3& rot, const Vector3& scale, float out[16]);
    static void BuildViewProjection(const Camera3D& camera, uint32_t width, uint32_t height, bool zeroToOneDepth, float out[16]);
    static void BuildOrtho2D(uint32_t width, uint32_t height, bool zeroToOneDepth, float out[16]);

private:
    static bool Reserve(Vertex*& array, uint32_t& capacity, uint32_t used, uint32_t extra);

    // Whether an entry of this many vertices still fits what is left of the
    // 3D budget. Whole entries only: a partial one would slice an object.
    bool Admits(uint32_t vertexCount) const;

    void AppendMesh(const float model[16], const float* verts, uint8_t components, const float* norms, const float* uvs, uint32_t vertexCount, uint8_t topology, Color3 color, uint32_t texture);
    void AppendPrimitive(const DrawLists& lists, const PrimitiveDrawEntry& entry);
    void AppendModel(const ModelDrawEntry& entry);
    void AppendLevelSectors();
    void PushRun(uint32_t firstVertex, uint32_t count, uint32_t texture);

    // Resolve a resource handle to the backend texture handle the resource
    // manager recorded at upload time (0 when not resident).
    static uint32_t ResolveTexture(int32_t resourceId);

    Vertex* m_verts3D;
    uint32_t m_count3D;
    uint32_t m_capacity3D;

    Vertex* m_verts2D;
    uint32_t m_count2D;
    uint32_t m_capacity2D;

    DrawRun m_runs[GFX_MAX_DRAW_RUNS];
    uint32_t m_runCount;

    DrawRun m_runs2D[GFX_MAX_2D_RUNS];
    uint32_t m_runCount2D;
    uint32_t m_droppedRuns2D;

    DrawStats* m_stats; // borrowed for the duration of BuildFrame

    uint32_t m_vertexBudget; // 0 = unlimited
    uint32_t m_budget3D; // what is left for world geometry this frame
    uint32_t m_viewportWidth;
    uint32_t m_viewportHeight;
    FrustumPlanes m_frustum;
    bool m_frustumValid;
};
