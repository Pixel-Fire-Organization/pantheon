#include "../include/graphics/ModelFormat.h"

#include <cmath>
#include <cstring>

#include "../../include/core/EngineDebug.h"
#include "../../include/core/EngineMemory.h"
#include "platform/Platform.h"

namespace
{
    float* AllocFloats(size_t count)
    {
        // 16-byte aligned so the arrays are safe for qword DMA / cache ops.
        return static_cast<float*>(Engine_PlatformAlloc(count * sizeof(float), 16));
    }

    // Bounding sphere of a strided vertex array: AABB midpoint as center, exact
    // max distance as radius. Load-time only (v1 blobs carry no baked bounds).
    void ComputeMeshBounds(const float* verts, uint32_t vertexCount, int stride, Vector3* outCenter, float* outRadius)
    {
        Vector3 mn{verts[0], verts[1], verts[2]};
        Vector3 mx = mn;
        for (uint32_t i = 1; i < vertexCount; ++i)
        {
            const float* v = verts + i * stride;
            if (v[0] < mn.x)
                mn.x = v[0];
            if (v[1] < mn.y)
                mn.y = v[1];
            if (v[2] < mn.z)
                mn.z = v[2];
            if (v[0] > mx.x)
                mx.x = v[0];
            if (v[1] > mx.y)
                mx.y = v[1];
            if (v[2] > mx.z)
                mx.z = v[2];
        }
        const Vector3 c{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};

        float best = 0.0f;
        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            const float* v = verts + i * stride;
            const float dx = v[0] - c.x, dy = v[1] - c.y, dz = v[2] - c.z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > best)
                best = d2;
        }
        *outCenter = c;
        *outRadius = std::sqrt(best);
    }
} // namespace

bool Model_LoadBaked(const void* data, size_t size, Model* outModel, ModelTextureResolver resolver, void* resolverUser)
{
    if (!data || !outModel || size < sizeof(BakedModelHeader))
    {
        Engine_LogError("Model: null/short blob (%zu bytes).", size);
        return false;
    }

    std::memset(outModel, 0, sizeof(Model));

    const uint8_t* base = static_cast<const uint8_t*>(data);
    BakedModelHeader hdr;
    std::memcpy(&hdr, base, sizeof(hdr));

    if (hdr.magic != BAKED_MODEL_MAGIC)
    {
        Engine_LogError("Model: bad magic.");
        return false;
    }
    if (hdr.version != BAKED_MODEL_VERSION && hdr.version != BAKED_MODEL_VERSION_LEGACY)
    {
        Engine_LogError("Model: unsupported version %u.", hdr.version);
        return false;
    }
    if (hdr.meshCount == 0 || hdr.meshCount > 65535u || hdr.materialCount > 65535u)
    {
        Engine_LogError("Model: bad counts (meshes=%u, materials=%u).", hdr.meshCount, hdr.materialCount);
        return false;
    }

    // v1 positions are vec3 + a 24-byte mesh entry; v2 positions are vec4
    // (16-byte stride) + a 48-byte mesh entry carrying topology + bounds.
    const bool isV2 = (hdr.version == BAKED_MODEL_VERSION);
    const int posComponents = isV2 ? 4 : 3;
    const size_t meshEntrySize = isV2 ? sizeof(BakedMeshEntry) : sizeof(BakedMeshEntryV1);

    // Header + mesh table + material table must all fit inside the blob.
    const size_t meshTableOff = sizeof(BakedModelHeader);
    const size_t matTableOff = meshTableOff + static_cast<size_t>(hdr.meshCount) * meshEntrySize;
    const size_t payloadOff = matTableOff + static_cast<size_t>(hdr.materialCount) * sizeof(BakedMaterialEntry);
    if (payloadOff > size)
    {
        Engine_LogError("Model: truncated tables.");
        return false;
    }

    outModel->meshCount = static_cast<int>(hdr.meshCount);
    outModel->meshes = static_cast<Mesh*>(calloc(hdr.meshCount, sizeof(Mesh)));
    outModel->meshMaterial = static_cast<int*>(calloc(hdr.meshCount, sizeof(int)));
    if (!outModel->meshes || !outModel->meshMaterial)
    {
        Engine_LogError("Model: OOM (mesh tables).");
        Model_FreeBaked(outModel);
        return false;
    }

    // --- Materials (resolve diffuse textures via the caller's resolver) ---
    if (hdr.materialCount > 0)
    {
        outModel->materialCount = static_cast<int>(hdr.materialCount);
        outModel->materials = static_cast<Material*>(calloc(hdr.materialCount, sizeof(Material)));
        if (!outModel->materials)
        {
            Engine_LogError("Model: OOM (materials).");
            Model_FreeBaked(outModel);
            return false;
        }

        for (uint32_t m = 0; m < hdr.materialCount; ++m)
        {
            BakedMaterialEntry me;
            std::memcpy(&me, base + matTableOff + m * sizeof(BakedMaterialEntry), sizeof(me));

            MaterialMap& map = outModel->materials[m].maps[MATERIAL_MAP_DIFFUSE];
            std::memset(&map.texture, 0, sizeof(map.texture));
            map.textureResourceId = -1;
            if (resolver && me.diffuseTexRef != BAKED_MODEL_TEXREF_NONE)
                map.textureResourceId = resolver(me.diffuseTexRef, resolverUser);
        }
    }

    // --- Meshes (copy separated arrays into aligned heap allocations) ---
    for (uint32_t i = 0; i < hdr.meshCount; ++i)
    {
        // Read the common fields plus (v2) topology / bounds. The two on-disk
        // layouts share the first five u32 fields.
        BakedMeshEntry me{};
        if (isV2)
        {
            std::memcpy(&me, base + meshTableOff + i * meshEntrySize, sizeof(BakedMeshEntry));
        }
        else
        {
            BakedMeshEntryV1 v1;
            std::memcpy(&v1, base + meshTableOff + i * meshEntrySize, sizeof(v1));
            me.vertexCount = v1.vertexCount;
            me.materialIndex = v1.materialIndex;
            me.vertsOffset = v1.vertsOffset;
            me.normsOffset = v1.normsOffset;
            me.uvsOffset = v1.uvsOffset;
            me.topology = BAKED_TOPOLOGY_LIST;
        }

        if (me.vertexCount == 0 || me.vertsOffset == 0)
        {
            Engine_LogError("Model: mesh %u has no vertices.", i);
            Model_FreeBaked(outModel);
            return false;
        }

        // Widen before multiplying: both fields come straight off disc, and the
        // product wraps a 32-bit size_t long before it exceeds the blob.
        const uint64_t vBytes = static_cast<uint64_t>(me.vertexCount) * posComponents * sizeof(float);
        if (static_cast<uint64_t>(me.vertsOffset) + vBytes > size)
        {
            Engine_LogError("Model: mesh %u vertices out of bounds.", i);
            Model_FreeBaked(outModel);
            return false;
        }

        Mesh& mesh = outModel->meshes[i];
        mesh.vertexCount = static_cast<int>(me.vertexCount);
        mesh.indices = nullptr;
        mesh.topology = (me.topology == BAKED_TOPOLOGY_STRIP) ? MESH_TOPOLOGY_STRIP : MESH_TOPOLOGY_LIST;
        mesh.vertexComponents = static_cast<unsigned char>(posComponents);
        mesh.vertices = AllocFloats(static_cast<size_t>(me.vertexCount) * posComponents);
        if (!mesh.vertices)
        {
            Model_FreeBaked(outModel);
            return false;
        }
        std::memcpy(mesh.vertices, base + me.vertsOffset, static_cast<size_t>(vBytes));

        if (isV2)
        {
            mesh.boundsCenter = Vector3{me.boundsCenter[0], me.boundsCenter[1], me.boundsCenter[2]};
            mesh.boundsRadius = me.boundsRadius;
        }
        else
        {
            // v1 blobs carry no baked bounds — derive them from the vertices.
            ComputeMeshBounds(mesh.vertices, me.vertexCount, posComponents, &mesh.boundsCenter, &mesh.boundsRadius);
        }

        if (me.normsOffset != 0)
        {
            const uint64_t nBytes = static_cast<uint64_t>(me.vertexCount) * 3 * sizeof(float);
            if (static_cast<uint64_t>(me.normsOffset) + nBytes > size)
            {
                Engine_LogError("Model: mesh %u normals out of bounds.", i);
                Model_FreeBaked(outModel);
                return false;
            }
            mesh.normals = AllocFloats(static_cast<size_t>(me.vertexCount) * 3);
            if (!mesh.normals)
            {
                Model_FreeBaked(outModel);
                return false;
            }
            std::memcpy(mesh.normals, base + me.normsOffset, static_cast<size_t>(nBytes));
        }

        if (me.uvsOffset != 0)
        {
            const uint64_t uBytes = static_cast<uint64_t>(me.vertexCount) * 2 * sizeof(float);
            if (static_cast<uint64_t>(me.uvsOffset) + uBytes > size)
            {
                Engine_LogError("Model: mesh %u texcoords out of bounds.", i);
                Model_FreeBaked(outModel);
                return false;
            }
            mesh.texcoords = AllocFloats(static_cast<size_t>(me.vertexCount) * 2);
            if (!mesh.texcoords)
            {
                Model_FreeBaked(outModel);
                return false;
            }
            std::memcpy(mesh.texcoords, base + me.uvsOffset, static_cast<size_t>(uBytes));
        }

        uint32_t matIdx = me.materialIndex;
        if (hdr.materialCount == 0 || matIdx >= hdr.materialCount)
            matIdx = 0;
        outModel->meshMaterial[i] = static_cast<int>(matIdx);
    }

    // Model bounds = sphere enclosing every mesh sphere (conservative merge).
    {
        Vector3 c{0.0f, 0.0f, 0.0f};
        float r = 0.0f;
        for (int i = 0; i < outModel->meshCount; ++i)
        {
            const Mesh& mesh = outModel->meshes[i];
            if (i == 0)
            {
                c = mesh.boundsCenter;
                r = mesh.boundsRadius;
                continue;
            }
            const float dx = mesh.boundsCenter.x - c.x;
            const float dy = mesh.boundsCenter.y - c.y;
            const float dz = mesh.boundsCenter.z - c.z;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d + mesh.boundsRadius <= r)
                continue; // mesh sphere already inside
            if (d + r <= mesh.boundsRadius)
            {
                c = mesh.boundsCenter; // current sphere inside mesh sphere
                r = mesh.boundsRadius;
                continue;
            }
            const float newR = (r + d + mesh.boundsRadius) * 0.5f;
            if (d > 0.0f)
            {
                const float t = (newR - r) / d;
                c.x += dx * t;
                c.y += dy * t;
                c.z += dz * t;
            }
            r = newR;
        }
        outModel->boundsCenter = c;
        outModel->boundsRadius = r;
    }

    Engine_LogInfo("Model: baked load OK (%d meshes, %d materials).", outModel->meshCount, outModel->materialCount);
    return true;
}

void Model_FreeBaked(Model* model)
{
    if (!model)
        return;

    if (model->meshes)
    {
        for (int i = 0; i < model->meshCount; ++i)
        {
            // Mesh pointers stay raw: renderers consume them as views, and a
            // sector's Mesh points into an arena slot instead. Ownership is
            // expressed by pairing these with Engine_PlatformAlloc.
            Engine_PlatformFree(model->meshes[i].vertices);
            Engine_PlatformFree(model->meshes[i].normals);
            Engine_PlatformFree(model->meshes[i].texcoords);
        }
        free(model->meshes);
    }
    free(model->materials);
    free(model->meshMaterial);
    std::memset(model, 0, sizeof(Model));
}
