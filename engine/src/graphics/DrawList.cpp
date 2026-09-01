#include "../include/graphics/DrawList.h"

#include <algorithm>
#include <cmath>

#include "../../include/core/EngineDebug.h"
#include "../include/graphics/PrimitiveGeometry.h"

DrawLists::DrawLists() : untexturedPrims{}, texturedPrims{}, models{}
{
    // Initialise every 3D camera slot to the same sane default so an unset
    // active slot still renders a valid view.
    for (uint8_t i = 0; i < GFX_MAX_CAMERAS_3D; ++i)
    {
        cameras3D[i].position = Vector3{0.0f, 10.0f, 20.0f};
        cameras3D[i].target = Vector3{0.0f, 0.0f, 0.0f};
        cameras3D[i].up = Vector3{0.0f, 1.0f, 0.0f};
        cameras3D[i].fovy = 45.0f;
        cameras3D[i].projection = CAMERA_PERSPECTIVE;
    }
    activeCamera3D = 0;

    camera2D.offset = Vector2{0.0f, 0.0f};
    camera2D.target = Vector2{0.0f, 0.0f};
    camera2D.rotation = 0.0f;
    camera2D.zoom = 1.0f;
}

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------

void DrawLists::Init(float* megaBatch) { ExtractPrimitiveGeometry(megaBatch); }

void DrawLists::Shutdown()
{
    // No GPU resources owned here — the renderer owns display lists / GS packets.
    m_cubeVerts = m_cubeNorms = m_cubeUVs = nullptr;
    m_sphereVerts = m_sphereNorms = m_sphereUVs = nullptr;
    m_cylVerts = m_cylNorms = m_cylUVs = nullptr;
}

// ---------------------------------------------------------------------------
// ExtractPrimitiveGeometry
// ---------------------------------------------------------------------------
// De-interleaves the stride-8 MODEL_* tables (xyz|nxyz|uv) into separated
// vertex / normal / UV arrays sub-allocated from the renderer arena buffer.
// ps2gl requires stride=0 arrays; the GIFTAG builder also consumes these.
// This routine is backend-neutral (no GL/GS calls) — the active renderer turns
// these arrays into display lists or GS packets after its context is ready.
// ---------------------------------------------------------------------------
void DrawLists::ExtractPrimitiveGeometry(float* megaBatch)
{
    // Helper: de-interleave MODEL_* (stride-8: xyz|nxyz|uv) into separate arrays.
    auto extract = [](const float* src, uint32_t count, float* vOut, float* nOut, float* uvOut)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const float* p = src + i * 8;
            vOut[i * 3 + 0] = p[0];
            vOut[i * 3 + 1] = p[1];
            vOut[i * 3 + 2] = p[2];
            nOut[i * 3 + 0] = p[3];
            nOut[i * 3 + 1] = p[4];
            nOut[i * 3 + 2] = p[5];
            uvOut[i * 2 + 0] = p[6];
            uvOut[i * 2 + 1] = p[7];
        }
    };

    // Sub-allocate separated arrays from the renderer arena (megaBatch).
    float* cur = megaBatch;

    m_cubeVerts = cur;
    cur += PRIMITIVE_CUBE_VERTEX_COUNT * 3;
    m_cubeNorms = cur;
    cur += PRIMITIVE_CUBE_VERTEX_COUNT * 3;
    m_cubeUVs = cur;
    cur += PRIMITIVE_CUBE_VERTEX_COUNT * 2;

    m_sphereVerts = cur;
    cur += PRIMITIVE_SPHERE_VERTEX_COUNT * 3;
    m_sphereNorms = cur;
    cur += PRIMITIVE_SPHERE_VERTEX_COUNT * 3;
    m_sphereUVs = cur;
    cur += PRIMITIVE_SPHERE_VERTEX_COUNT * 2;

    m_cylVerts = cur;
    cur += PRIMITIVE_CYLINDER_VERTEX_COUNT * 3;
    m_cylNorms = cur;
    cur += PRIMITIVE_CYLINDER_VERTEX_COUNT * 3;
    m_cylUVs = cur;
    cur += PRIMITIVE_CYLINDER_VERTEX_COUNT * 2;

    extract(MODEL_CUBE, PRIMITIVE_CUBE_VERTEX_COUNT, m_cubeVerts, m_cubeNorms, m_cubeUVs);
    extract(MODEL_SPHERE, PRIMITIVE_SPHERE_VERTEX_COUNT, m_sphereVerts, m_sphereNorms, m_sphereUVs);
    extract(MODEL_CYLINDER, PRIMITIVE_CYLINDER_VERTEX_COUNT, m_cylVerts, m_cylNorms, m_cylUVs);

    // Bounding-sphere radius per shape (max |v| — primitives are origin-centered).
    auto maxRadius = [](const float* verts, uint32_t count)
    {
        float best = 0.0f;
        for (uint32_t i = 0; i < count; ++i)
        {
            const float* v = verts + i * 3;
            const float l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
            if (l2 > best)
                best = l2;
        }
        return std::sqrt(best);
    };
    m_primBaseRadius[0] = maxRadius(m_cubeVerts, PRIMITIVE_CUBE_VERTEX_COUNT);
    m_primBaseRadius[1] = maxRadius(m_sphereVerts, PRIMITIVE_SPHERE_VERTEX_COUNT);
    m_primBaseRadius[2] = maxRadius(m_cylVerts, PRIMITIVE_CYLINDER_VERTEX_COUNT);

    Engine_LogInfo("DrawLists: Separated primitive geometry extracted (cube/sphere/cylinder).");
}

float DrawLists::GetPrimitiveBaseRadius(Primitive3D type) const
{
    switch (type)
    {
    case Primitive3D::Sphere:
        return m_primBaseRadius[1];
    case Primitive3D::Cylinder:
        return m_primBaseRadius[2];
    default:
        return m_primBaseRadius[0];
    }
}

PrimitiveArrays DrawLists::GetPrimitiveArrays(Primitive3D type) const
{
    switch (type)
    {
    case Primitive3D::Sphere:
        return PrimitiveArrays{m_sphereVerts, m_sphereNorms, m_sphereUVs, PRIMITIVE_SPHERE_VERTEX_COUNT};
    case Primitive3D::Cylinder:
        return PrimitiveArrays{m_cylVerts, m_cylNorms, m_cylUVs, PRIMITIVE_CYLINDER_VERTEX_COUNT};
    default:
        return PrimitiveArrays{m_cubeVerts, m_cubeNorms, m_cubeUVs, PRIMITIVE_CUBE_VERTEX_COUNT};
    }
}

bool DrawLists::AddPrimitive(const PrimitiveDrawEntry& entry)
{
    if (entry.textureId == -1)
    {
        if (untexturedCount >= GFX_MAX_DRAW_LIST_LENGTH)
        {
            Engine_LogError("DrawLists: Max untextured primitives reached (%d). Rejecting.", GFX_MAX_DRAW_LIST_LENGTH);
            return false;
        }
        untexturedPrims[untexturedCount++] = entry;
    }
    else
    {
        if (texturedCount >= GFX_MAX_DRAW_LIST_LENGTH)
        {
            Engine_LogError("DrawLists: Max textured primitives reached (%d). Rejecting.", GFX_MAX_DRAW_LIST_LENGTH);
            return false;
        }
        texturedPrims[texturedCount++] = entry;
    }
    return true;
}

bool DrawLists::AddModel(const ModelDrawEntry& entry)
{
    if (modelCount >= GFX_MAX_DRAW_LIST_LENGTH)
    {
        Engine_LogError("DrawLists: Max models reached (%d). Rejecting.", GFX_MAX_DRAW_LIST_LENGTH);
        return false;
    }

    models[modelCount++] = entry;
    return true;
}

bool DrawLists::SetSkyboxTexture(int32_t skyboxTextureId)
{
    if (skyboxTextureId > 0)
    {
        this->skyboxResourceId = skyboxTextureId;
        return true;
    }

    return false;
}

void DrawLists::SetCamera3D(CameraID id, const Camera3D& camera)
{
    if (id < 0 || id >= GFX_MAX_CAMERAS_3D)
    {
        Engine_LogError("DrawLists: SetCamera3D invalid slot %d (max %d).", static_cast<int>(id), GFX_MAX_CAMERAS_3D);
        return;
    }
    cameras3D[id] = camera;
}

void DrawLists::SetActiveCamera3D(CameraID id)
{
    if (id < 0 || id >= GFX_MAX_CAMERAS_3D)
    {
        Engine_LogError("DrawLists: SetActiveCamera3D invalid slot %d (max %d).", static_cast<int>(id), GFX_MAX_CAMERAS_3D);
        return;
    }
    activeCamera3D = static_cast<uint8_t>(id);
}

void DrawLists::SetActiveCamera2D(const Camera2D& camera) { camera2D = camera; }

void DrawLists::SortForSubmission()
{
    if (texturedCount > 1)
    {
        std::sort(texturedPrims, texturedPrims + texturedCount, [](const PrimitiveDrawEntry& a, const PrimitiveDrawEntry& b) { return a.textureId < b.textureId; });
    }
    if (modelCount > 1)
    {
        std::sort(models, models + modelCount, [](const ModelDrawEntry& a, const ModelDrawEntry& b) { return a.resourceId < b.resourceId; });
    }
}

void DrawLists::Reset(const bool resetSkybox)
{
    untexturedCount = 0;
    texturedCount = 0;
    modelCount = 0;

    if (resetSkybox)
        skyboxResourceId = -1;
}
