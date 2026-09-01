#include "graphics/StagedGeometry.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "../../include/core/EngineDebug.h"
#include "../../include/level/EngineSector.h"
#include "../../include/resources/EngineResource.h"
#include "Macros.h"
#include "PlatformConstants.h"
#include "graphics/ModelFormat.h"
#include "graphics/Types.h"

namespace
{

    void MatIdentity(float m[16])
    {
        memset(m, 0, sizeof(float) * 16);
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    void MatMultiply(const float a[16], const float b[16], float out[16])
    {
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += a[k * 4 + r] * b[c * 4 + k];
                out[c * 4 + r] = sum;
            }
        }
    }

    Vector3 Normalize(const Vector3& v)
    {
        const float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len < 1e-6f)
            return Vector3{0.0f, 0.0f, 0.0f};
        return Vector3{v.x / len, v.y / len, v.z / len};
    }

    Vector3 Cross(const Vector3& a, const Vector3& b) { return Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

    float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

} // namespace

StagedGeometry::StagedGeometry()
    : m_verts3D(nullptr), m_count3D(0), m_capacity3D(0), m_verts2D(nullptr), m_count2D(0), m_capacity2D(0), m_runCount(0), m_runCount2D(0), m_droppedRuns2D(0), m_stats(nullptr),
      m_vertexBudget(0), m_budget3D(0), m_viewportWidth(0), m_viewportHeight(0), m_frustum(), m_frustumValid(false)
{
    memset(m_runs, 0, sizeof(m_runs));
    memset(m_runs2D, 0, sizeof(m_runs2D));
}

void StagedGeometry::SetFrameBudget(uint32_t maxVertices, uint32_t viewportWidth, uint32_t viewportHeight)
{
    m_vertexBudget = maxVertices;
    m_viewportWidth = viewportWidth;
    m_viewportHeight = viewportHeight;
}

bool StagedGeometry::Admits(uint32_t vertexCount) const { return !m_vertexBudget || (m_count3D + vertexCount <= m_budget3D); }

StagedGeometry::~StagedGeometry()
{
    free(m_verts3D);
    free(m_verts2D);
}

void StagedGeometry::BeginFrame()
{
    m_count3D = 0;
    m_runCount = 0;
}

void StagedGeometry::EndFrame()
{
    if (m_droppedRuns2D > 0)
    {
        Engine_LogError("StagedGeometry: dropped %u screen-space quad(s) this frame (run capacity %u).", m_droppedRuns2D, static_cast<unsigned>(GFX_MAX_2D_RUNS));
        m_droppedRuns2D = 0;
    }
    m_count2D = 0;
    m_runCount2D = 0;
}

bool StagedGeometry::Reserve(Vertex*& array, uint32_t& capacity, uint32_t used, uint32_t extra)
{
    if (used + extra <= capacity)
        return true;

    uint32_t needed = capacity ? capacity : 4096u;
    while (needed < used + extra)
        needed *= 2u;

    // Same contract as the PS2 backends: refuse loudly rather than overrun.
    const uint32_t kMaxVertices = 1u << 21;
    if (needed > kMaxVertices)
    {
        Engine_LogError("StagedGeometry: vertex budget exceeded (%u > %u); dropping geometry", needed, kMaxVertices);
        return false;
    }

    Vertex* grown = static_cast<Vertex*>(realloc(array, needed * sizeof(Vertex)));
    if (!grown)
    {
        Engine_LogError("StagedGeometry: out of memory growing the vertex staging buffer");
        return false;
    }
    array = grown;
    capacity = needed;
    return true;
}

uint32_t StagedGeometry::ResolveTexture(int32_t resourceId)
{
    if (resourceId < 0 || !Engine_Resource_IsReady(resourceId))
        return 0;
    const Texture2D* tex = static_cast<const Texture2D*>(Engine_Resource_Get(resourceId));
    return tex ? tex->id : 0u;
}

void StagedGeometry::PushRun(uint32_t firstVertex, uint32_t count, uint32_t texture)
{
    if (count == 0)
        return;

    // Extend the previous run when the texture matches, which is why the draw
    // lists are sorted by texture id before this runs.
    if (m_runCount > 0)
    {
        DrawRun& last = m_runs[m_runCount - 1];
        if (last.texture == texture && last.first + last.count == firstVertex)
        {
            last.count += count;
            return;
        }
    }

    if (m_runCount >= GFX_MAX_DRAW_RUNS)
    {
        Engine_LogError("StagedGeometry: draw-run budget exceeded (%d); dropping geometry", GFX_MAX_DRAW_RUNS);
        return;
    }

    m_runs[m_runCount].first = firstVertex;
    m_runs[m_runCount].count = count;
    m_runs[m_runCount].texture = texture;
    ++m_runCount;
    if (m_stats)
        ++m_stats->texBinds;
}

void StagedGeometry::AppendMesh(const float model[16], const float* verts, uint8_t components, const float* norms, const float* uvs, uint32_t vertexCount, uint8_t topology, Color3 color,
                                 uint32_t texture)
{
    if (!verts || vertexCount == 0)
        return;

    // A strip is expanded to a list here rather than switching pipeline topology
    // mid-pass: one triangle-list pipeline is what keeps batching by texture
    // possible at all.
    const uint32_t triangles = (topology == MESH_TOPOLOGY_STRIP) ? (vertexCount >= 3 ? vertexCount - 2 : 0) : vertexCount / 3;
    const uint32_t maxEmitted = triangles * 3;
    if (maxEmitted == 0)
        return;
    if (!Admits(maxEmitted))
    {
        if (m_stats)
            ++m_stats->entriesCulled;
        return;
    }
    if (!Reserve(m_verts3D, m_capacity3D, m_count3D, maxEmitted))
        return;

    const uint32_t runStart = m_count3D;

    for (uint32_t t = 0; t < triangles; ++t)
    {
        uint32_t idx[3];
        if (topology == MESH_TOPOLOGY_STRIP)
        {
            // Alternate winding so the expanded list keeps the strip's facing.
            idx[0] = t;
            idx[1] = (t & 1u) ? (t + 2) : (t + 1);
            idx[2] = (t & 1u) ? (t + 1) : (t + 2);
        }
        else
        {
            idx[0] = t * 3 + 0;
            idx[1] = t * 3 + 1;
            idx[2] = t * 3 + 2;
        }

        // Degenerate stitching triangles carry no area; skipping them keeps the
        // triangle counters honest.
        if (topology == MESH_TOPOLOGY_STRIP && (idx[0] == idx[1] || idx[1] == idx[2] || idx[0] == idx[2]))
            continue;

        for (int k = 0; k < 3; ++k)
        {
            const uint32_t i = idx[k];
            const float vx = verts[i * components + 0];
            const float vy = verts[i * components + 1];
            const float vz = verts[i * components + 2];

            Vertex& out = m_verts3D[m_count3D++];
            out.x = model[0] * vx + model[4] * vy + model[8] * vz + model[12];
            out.y = model[1] * vx + model[5] * vy + model[9] * vz + model[13];
            out.z = model[2] * vx + model[6] * vy + model[10] * vz + model[14];

            if (norms)
            {
                const float nx = norms[i * 3 + 0];
                const float ny = norms[i * 3 + 1];
                const float nz = norms[i * 3 + 2];
                out.nx = model[0] * nx + model[4] * ny + model[8] * nz;
                out.ny = model[1] * nx + model[5] * ny + model[9] * nz;
                out.nz = model[2] * nx + model[6] * ny + model[10] * nz;
            }
            else
            {
                out.nx = out.ny = out.nz = 0.0f;
            }

            out.u = uvs ? uvs[i * 2 + 0] : 0.0f;
            out.v = uvs ? uvs[i * 2 + 1] : 0.0f;
            out.r = color.r;
            out.g = color.g;
            out.b = color.b;
            out.a = 1.0f;
        }
    }

    const uint32_t emitted = m_count3D - runStart;
    PushRun(runStart, emitted, texture);
    if (m_stats)
    {
        m_stats->trisSubmitted += emitted / 3u;
        m_stats->vertsTransformed += emitted;
    }
}

void StagedGeometry::BuildModelMatrix(const Vector3& pos, const Vector3& rot, const Vector3& scale, float out[16])
{
    const float cx = cosf(rot.x), sx = sinf(rot.x);
    const float cy = cosf(rot.y), sy = sinf(rot.y);
    const float cz = cosf(rot.z), sz = sinf(rot.z);

    // ZYX rotation, matching Transform3D on the PS2 side.
    MatIdentity(out);
    out[0] = (cy * cz) * scale.x;
    out[1] = (sx * sy * cz + cx * sz) * scale.x;
    out[2] = (-cx * sy * cz + sx * sz) * scale.x;

    out[4] = (-cy * sz) * scale.y;
    out[5] = (-sx * sy * sz + cx * cz) * scale.y;
    out[6] = (cx * sy * sz + sx * cz) * scale.y;

    out[8] = (sy)*scale.z;
    out[9] = (-sx * cy) * scale.z;
    out[10] = (cx * cy) * scale.z;

    out[12] = pos.x;
    out[13] = pos.y;
    out[14] = pos.z;
}

void StagedGeometry::AppendPrimitive(const DrawLists& lists, const PrimitiveDrawEntry& entry)
{
    const PrimitiveArrays geo = lists.GetPrimitiveArrays(entry.type);
    if (!geo.verts || geo.vertexCount == 0)
        return;

    if (m_frustumValid)
    {
        Vector3 worldCenter;
        float worldRadius = 0.0f;
        Frustum_WorldSphere(entry.transform.GetPosition(), entry.transform.GetScale(), Vector3{0.0f, 0.0f, 0.0f}, lists.GetPrimitiveBaseRadius(entry.type), &worldCenter, &worldRadius);
        if (!Frustum_SphereVisible(&m_frustum, worldCenter, worldRadius))
        {
            if (m_stats)
                ++m_stats->entriesCulled;
            return;
        }
    }

    float model[16];
    BuildModelMatrix(entry.transform.GetPosition(), entry.transform.GetRotation(), entry.transform.GetScale(), model);
    AppendMesh(model, geo.verts, 3, geo.norms, geo.uvs, geo.vertexCount, MESH_TOPOLOGY_LIST, entry.color, ResolveTexture(entry.textureId));
}

void StagedGeometry::AppendModel(const ModelDrawEntry& entry)
{
    if (entry.resourceId < 0 || !Engine_Resource_IsReady(entry.resourceId))
        return;
    const Model* model = static_cast<const Model*>(Engine_Resource_Get(entry.resourceId));
    if (!model || !model->meshes)
        return;

    float matrix[16];
    BuildModelMatrix(entry.transform.GetPosition(), entry.transform.GetRotation(), entry.transform.GetScale(), matrix);

    for (int i = 0; i < model->meshCount; ++i)
    {
        const Mesh& mesh = model->meshes[i];
        if (!mesh.vertices || mesh.indices)
            continue; // indexed meshes are not produced by the baker

        uint32_t texture = 0;
        const int materialIndex = model->meshMaterial ? model->meshMaterial[i] : 0;
        if (model->materials && materialIndex < model->materialCount)
            texture = ResolveTexture(model->materials[materialIndex].maps[MATERIAL_MAP_DIFFUSE].textureResourceId);

        AppendMesh(matrix, mesh.vertices, mesh.vertexComponents ? mesh.vertexComponents : 3, mesh.normals, mesh.texcoords, static_cast<uint32_t>(mesh.vertexCount), mesh.topology,
                   Color3{1.0f, 1.0f, 1.0f}, texture);
        if (m_stats)
            ++m_stats->modelCount;
    }
}

bool StagedGeometry::BuildOne(const Renderable3D& what, const DrawLists& primitiveSource)
{
    m_count3D = 0;
    m_runCount = 0;

    float model[16];
    BuildModelMatrix(Vector3{0.0f, 0.0f, 0.0f}, what.rotation, what.scale, model);

    if (what.modelId >= 0)
    {
        if (!Engine_Resource_IsReady(what.modelId))
            return false;
        const Model* mdl = static_cast<const Model*>(Engine_Resource_Get(what.modelId));
        if (!mdl || !mdl->meshes)
            return false;

        for (int i = 0; i < mdl->meshCount; ++i)
        {
            const Mesh& mesh = mdl->meshes[i];
            if (!mesh.vertices || mesh.indices)
                continue; // indexed meshes are not produced by the baker

            uint32_t texture = 0;
            const int materialIndex = mdl->meshMaterial ? mdl->meshMaterial[i] : 0;
            if (mdl->materials && materialIndex < mdl->materialCount)
                texture = ResolveTexture(mdl->materials[materialIndex].maps[MATERIAL_MAP_DIFFUSE].textureResourceId);

            AppendMesh(model, mesh.vertices, mesh.vertexComponents ? mesh.vertexComponents : 3, mesh.normals, mesh.texcoords, static_cast<uint32_t>(mesh.vertexCount), mesh.topology,
                       Color3{1.0f, 1.0f, 1.0f}, texture);
        }
        return true;
    }

    const PrimitiveArrays geo = primitiveSource.GetPrimitiveArrays(what.primitive);
    if (!geo.verts || geo.vertexCount == 0)
        return false;

    AppendMesh(model, geo.verts, 3, geo.norms, geo.uvs, geo.vertexCount, MESH_TOPOLOGY_LIST, what.color, ResolveTexture(what.textureId));
    return true;
}

void StagedGeometry::AppendLevelSectors()
{
    uint32_t count = 0;
    const SectorResident* residents = Engine_Sector_GetResidents(&count);
    if (!residents)
        return;

    // Sector geometry is already in world space, so the model matrix is identity.
    float identity[16];
    MatIdentity(identity);

    for (uint32_t s = 0; s < count; ++s)
    {
        const SectorResident& sector = residents[s];
        if (sector.state != SECTOR_READY)
            continue;

        for (uint32_t m = 0; m < sector.meshCount && m < LEVEL_MAX_MESHES_PER_SECTOR; ++m)
        {
            const Mesh& mesh = sector.meshes[m];
            if (!mesh.vertices || mesh.vertexCount == 0)
                continue;

            AppendMesh(identity, mesh.vertices, mesh.vertexComponents ? mesh.vertexComponents : 3, mesh.normals, mesh.texcoords, static_cast<uint32_t>(mesh.vertexCount), mesh.topology,
                       Color3{1.0f, 1.0f, 1.0f}, ResolveTexture(sector.meshTexture[m]));
        }
    }
}

void StagedGeometry::BuildFrame(DrawLists& lists, DrawStats* stats)
{
    m_stats = stats;

    // Screen-space work was submitted during the game update, so its cost is
    // already known. Take it off the top: world geometry must never be able to
    // crowd out the interface.
    m_budget3D = 0;
    if (m_vertexBudget)
        m_budget3D = (m_vertexBudget > m_count2D) ? (m_vertexBudget - m_count2D) : 0u;

    m_frustumValid = false;
    if (m_viewportHeight > 0)
    {
        float proj[16], view[16], vp[16];
        const float aspect = static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight);
        Frustum_BuildPerspective(proj, lists.GetCamera3D().fovy, aspect, GFX_NEAR_PLANE, GFX_FAR_PLANE);
        Frustum_BuildLookAt(view, lists.GetCamera3D());
        Frustum_Mult4x4(vp, proj, view);
        Frustum_FromViewProj(&m_frustum, vp);
        m_frustumValid = true;
    }

    // Sorting by texture is what lets PushRun coalesce consecutive geometry into
    // a single draw call.
    lists.SortForSubmission();

    AppendLevelSectors();

    const PrimitiveDrawEntry* untextured = lists.GetUntexturedPrims();
    for (uint16_t i = 0; i < lists.GetUntexturedCount(); ++i)
        AppendPrimitive(lists, untextured[i]);

    const PrimitiveDrawEntry* textured = lists.GetTexturedPrims();
    for (uint16_t i = 0; i < lists.GetTexturedCount(); ++i)
        AppendPrimitive(lists, textured[i]);

    const ModelDrawEntry* models = lists.GetModels();
    for (uint16_t i = 0; i < lists.GetModelCount(); ++i)
        AppendModel(models[i]);

    if (stats)
        stats->primitiveCount = static_cast<uint16_t>(lists.GetUntexturedCount() + lists.GetTexturedCount());

    m_stats = nullptr;
}

void StagedGeometry::AddQuad2D(const Quad2D& quad)
{
    const bool opensRun = (m_runCount2D == 0) || (m_runs2D[m_runCount2D - 1].texture != quad.texture);
    if (opensRun && m_runCount2D >= GFX_MAX_2D_RUNS)
    {
        ++m_droppedRuns2D;
        return;
    }
    if (!Reserve(m_verts2D, m_capacity2D, m_count2D, 6))
        return;

    if (opensRun)
    {
        DrawRun& run = m_runs2D[m_runCount2D++];
        run.first = m_count2D;
        run.count = 0;
        run.texture = quad.texture;
    }
    m_runs2D[m_runCount2D - 1].count += 6;

    const float x0 = static_cast<float>(quad.x);
    const float y0 = static_cast<float>(quad.y);
    const float x1 = static_cast<float>(quad.x + quad.w);
    const float y1 = static_cast<float>(quad.y + quad.h);
    const float u0 = static_cast<float>(quad.u0) / 65535.0f;
    const float v0 = static_cast<float>(quad.v0) / 65535.0f;
    const float u1 = static_cast<float>(quad.u1) / 65535.0f;
    const float v1 = static_cast<float>(quad.v1) / 65535.0f;

    const float r = static_cast<float>(quad.r) / 255.0f;
    const float g = static_cast<float>(quad.g) / 255.0f;
    const float b = static_cast<float>(quad.b) / 255.0f;
    const float a = static_cast<float>(quad.a) / 255.0f;

    // Two triangles. Normals stay zero, which the shaders read as "unlit".
    const float corners[6][4] = {{x0, y0, u0, v0}, {x1, y0, u1, v0}, {x1, y1, u1, v1}, {x0, y0, u0, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1}};
    for (int i = 0; i < 6; ++i)
    {
        Vertex& v = m_verts2D[m_count2D++];
        v.x = corners[i][0];
        v.y = corners[i][1];
        v.z = 0.0f;
        v.nx = v.ny = v.nz = 0.0f;
        v.u = corners[i][2];
        v.v = corners[i][3];
        v.r = r;
        v.g = g;
        v.b = b;
        v.a = a;
    }
}

void StagedGeometry::BuildViewProjection(const Camera3D& camera, uint32_t width, uint32_t height, bool zeroToOneDepth, float out[16])
{
    const Vector3 forward = Normalize(Vector3{camera.target.x - camera.position.x, camera.target.y - camera.position.y, camera.target.z - camera.position.z});
    const Vector3 right = Normalize(Cross(forward, Vector3{0.0f, 1.0f, 0.0f}));
    const Vector3 up = Cross(right, forward);

    float view[16];
    MatIdentity(view);
    view[0] = right.x;
    view[4] = right.y;
    view[8] = right.z;
    view[1] = up.x;
    view[5] = up.y;
    view[9] = up.z;
    view[2] = -forward.x;
    view[6] = -forward.y;
    view[10] = -forward.z;
    view[12] = -Dot(right, camera.position);
    view[13] = -Dot(up, camera.position);
    view[14] = Dot(forward, camera.position);

    // Aspect comes from the live framebuffer, not a constant: a desktop window
    // is resizable, unlike the PS2 framebuffer.
    const float aspect = (height > 0) ? (static_cast<float>(width) / static_cast<float>(height)) : 1.0f;
    const float fovyRad = camera.fovy * 3.14159265358979f / 180.0f;
    const float f = 1.0f / tanf(fovyRad * 0.5f);
    const float nearZ = GFX_NEAR_PLANE;
    const float farZ = GFX_FAR_PLANE;

    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0] = f / aspect;
    proj[5] = f;
    proj[11] = -1.0f;

    // The one place the two APIs genuinely differ: WebGPU clips Z to [0,1],
    // OpenGL to [-1,1].
    if (zeroToOneDepth)
    {
        proj[10] = farZ / (nearZ - farZ);
        proj[14] = (nearZ * farZ) / (nearZ - farZ);
    }
    else
    {
        proj[10] = (farZ + nearZ) / (nearZ - farZ);
        proj[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
    }

    MatMultiply(proj, view, out);
}

void StagedGeometry::BuildOrtho2D(uint32_t width, uint32_t height, bool zeroToOneDepth, float out[16])
{
    // Pixel coordinates, origin top-left, matching DrawRect2D on the PS2 side.
    const float w = static_cast<float>(width ? width : 1u);
    const float h = static_cast<float>(height ? height : 1u);

    memset(out, 0, sizeof(float) * 16);
    out[0] = 2.0f / w;
    out[5] = -2.0f / h;
    out[10] = zeroToOneDepth ? 1.0f : -1.0f;
    out[12] = -1.0f;
    out[13] = 1.0f;
    out[15] = 1.0f;
}
