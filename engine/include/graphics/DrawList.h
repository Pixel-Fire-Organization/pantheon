#pragma once
#include <cstdint>

#include "EngineGraphics.h"

struct DrawStats
{
    uint16_t primitiveCount; // primitives rendered last frame
    uint16_t modelCount; // model mesh draw calls last frame (one per unindexed mesh)
    uint16_t entriesCulled; // draw entries rejected before submission (frustum cull)
    uint16_t texBinds; // texture binds issued last frame
    uint32_t trisSubmitted; // triangles actually emitted by the backend
    uint32_t trisCulled; // triangles dropped (near-plane, frustum, backface)
    uint32_t vertsTransformed; // vertices run through the transform path
    uint32_t submitBufferUsedBytes; // backend submission buffer fill; 0 = not reported
    uint32_t submitBufferCapacityBytes; // what the above is measured against; 0 = N/A
    float presentWaitMs; // time blocked waiting on presentation inside EndFrame
    float geometryBuildMs; // CPU time staging this frame's geometry; 0 = not reported
    float geometryUploadMs; // CPU time copying it into GPU memory; 0 = not reported
};

struct PrimitiveDrawEntry
{
    Transform3D transform;
    Color3 color;
    Primitive3D type;
    int32_t textureId = -1;
};

struct ModelDrawEntry
{
    int32_t resourceId;
    Transform3D transform;
};

// Separated (stride-0) geometry arrays for one primitive shape. Every backend
// consumes these; what each does with them is in its own renderer spec.
struct PrimitiveArrays
{
    const float* verts; // 3 floats per vertex
    const float* norms; // 3 floats per vertex
    const float* uvs; // 2 floats per vertex
    uint32_t vertexCount;
};

class DrawLists
{
    PrimitiveDrawEntry untexturedPrims[GFX_MAX_DRAW_LIST_LENGTH];
    PrimitiveDrawEntry texturedPrims[GFX_MAX_DRAW_LIST_LENGTH];
    ModelDrawEntry models[GFX_MAX_DRAW_LIST_LENGTH];
    uint16_t untexturedCount = 0;
    uint16_t texturedCount = 0;
    uint16_t modelCount = 0;
    int32_t skyboxResourceId = -1;

    // Fixed 3D camera slots; exactly one is active (rendered) per frame.
    Camera3D cameras3D[GFX_MAX_CAMERAS_3D]{};
    uint8_t activeCamera3D = 0;
    // Single 2D / UI camera.
    Camera2D camera2D{};

    DrawStats m_lastStats{};

    // Separated vertex / normal / UV arrays extracted from MODEL_* at init.
    // Pointers into the renderer arena buffer; persistent for the primitive
    // geometry lifetime. Backend-neutral — no device handles live here.
    float* m_cubeVerts = nullptr;
    float* m_cubeNorms = nullptr;
    float* m_cubeUVs = nullptr;
    float* m_sphereVerts = nullptr;
    float* m_sphereNorms = nullptr;
    float* m_sphereUVs = nullptr;
    float* m_cylVerts = nullptr;
    float* m_cylNorms = nullptr;
    float* m_cylUVs = nullptr;

    // Object-space bounding-sphere radius (max |v|) per primitive shape,
    // computed from the MODEL_* tables at Init. Used for frustum culling.
    float m_primBaseRadius[3] = {0.0f, 0.0f, 0.0f}; // cube, sphere, cylinder

    // De-interleave MODEL_* (stride-8: xyz|nxyz|uv) into the separated arrays
    // sub-allocated from the given renderer arena buffer.
    void ExtractPrimitiveGeometry(float* megaBatch);

public:
    DrawLists();

    DrawLists(const DrawLists&) = delete;
    DrawLists(DrawLists&&) = delete;
    DrawLists& operator=(const DrawLists&) = delete;
    DrawLists& operator=(DrawLists&&) = delete;

    // Populate the separated primitive geometry arrays from the given
    // pre-allocated arena buffer. Backend-neutral: performs no graphics calls.
    // Call once before the renderer compiles/uploads primitive geometry.
    void Init(float* megaBatch);

    // No GPU resources are owned here; kept for symmetry with the renderer.
    void Shutdown();

    bool AddPrimitive(const PrimitiveDrawEntry& entry);
    bool AddModel(const ModelDrawEntry& entry);
    bool SetSkyboxTexture(int32_t skyboxTextureId);

    // Camera control (fixed-slot model).
    void SetCamera3D(CameraID id, const Camera3D& camera); // write one slot
    void SetActiveCamera3D(CameraID id); // choose the active slot
    void SetActiveCamera2D(const Camera2D& camera);
    CameraID GetActiveCamera3DIndex() const { return static_cast<CameraID>(activeCamera3D); }

    // Sort textured primitives by texture id and models by resource id so the
    // backends bind each texture once per run instead of per draw. Safe for the
    // opaque + Z-tested pass; translucency (when added) needs its own ordering.
    void SortForSubmission();

    void Reset(bool resetSkybox = false);

    // Separated geometry accessor for the given primitive type.
    PrimitiveArrays GetPrimitiveArrays(Primitive3D type) const;

    // Object-space bounding-sphere radius for the given primitive type
    // (centered at the object origin; scale it by the entry's max scale).
    float GetPrimitiveBaseRadius(Primitive3D type) const;

    // Getters for Renderer
    const PrimitiveDrawEntry* GetUntexturedPrims() const { return untexturedPrims; }
    uint16_t GetUntexturedCount() const { return untexturedCount; }

    const PrimitiveDrawEntry* GetTexturedPrims() const { return texturedPrims; }
    uint16_t GetTexturedCount() const { return texturedCount; }

    const ModelDrawEntry* GetModels() const { return models; }
    uint16_t GetModelCount() const { return modelCount; }


    int32_t GetSkyboxResourceId() const { return skyboxResourceId; }
    const Camera3D& GetCamera3D() const { return cameras3D[activeCamera3D]; } // active slot
    const Camera2D& GetCamera2D() const { return camera2D; }

    DrawStats GetLastStats() const { return m_lastStats; }
    void SetLastStats(const DrawStats& stats) { m_lastStats = stats; }
};
