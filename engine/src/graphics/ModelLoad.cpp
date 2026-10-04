#include "../include/graphics/ModelFormat.h"

#include <cmath>
#include <cstdlib>
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
} // namespace

bool Model_LoadBaked(const void* data, size_t size, uint32_t materialCount, Model* outModel, ModelMaterialResolver resolver, void* resolverUser)
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
    if (hdr.version != BAKED_MODEL_VERSION)
    {
        Engine_LogError("Model: unsupported version %u (this build reads version %u only).", hdr.version, BAKED_MODEL_VERSION);
        return false;
    }
    if (hdr.meshCount == 0 || hdr.meshCount > 65535u)
    {
        Engine_LogError("Model: bad mesh count (%u).", hdr.meshCount);
        return false;
    }

    // Header + mesh table must fit inside the blob.
    const size_t meshTableOff = sizeof(BakedModelHeader);
    const size_t payloadOff = meshTableOff + static_cast<size_t>(hdr.meshCount) * sizeof(BakedMeshEntry);
    if (payloadOff > size)
    {
        Engine_LogError("Model: truncated mesh table.");
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

    // --- Materials: every dependency of this asset names a RES_MATERIAL. The
    // count comes from the caller (the owning entry's authoritative depCount),
    // never from the payload, which is not trusted for indexing.
    if (materialCount > 0)
    {
        outModel->materialCount = static_cast<int>(materialCount);
        outModel->materials = static_cast<int32_t*>(calloc(materialCount, sizeof(int32_t)));
        if (!outModel->materials)
        {
            Engine_LogError("Model: OOM (materials).");
            Model_FreeBaked(outModel);
            return false;
        }
        for (uint32_t m = 0; m < materialCount; ++m)
            outModel->materials[m] = resolver ? resolver(m, resolverUser) : -1;
    }

    // --- Meshes (copy separated arrays into aligned heap allocations) ---
    for (uint32_t i = 0; i < hdr.meshCount; ++i)
    {
        BakedMeshEntry me;
        std::memcpy(&me, base + meshTableOff + i * sizeof(BakedMeshEntry), sizeof(me));

        if (me.vertexCount == 0 || me.vertsOffset == 0)
        {
            Engine_LogError("Model: mesh %u has no vertices.", i);
            Model_FreeBaked(outModel);
            return false;
        }

        // Widen before multiplying: both fields come straight off disc, and the
        // product wraps a 32-bit size_t long before it exceeds the blob.
        const uint64_t vBytes = static_cast<uint64_t>(me.vertexCount) * 4 * sizeof(float);
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
        mesh.vertexComponents = 4;
        mesh.vertices = AllocFloats(static_cast<size_t>(me.vertexCount) * 4);
        if (!mesh.vertices)
        {
            Model_FreeBaked(outModel);
            return false;
        }
        std::memcpy(mesh.vertices, base + me.vertsOffset, static_cast<size_t>(vBytes));

        mesh.boundsCenter = Vector3{me.boundsCenter[0], me.boundsCenter[1], me.boundsCenter[2]};
        mesh.boundsRadius = me.boundsRadius;

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

        if (me.colorsOffset != 0)
        {
            const uint64_t cBytes = static_cast<uint64_t>(me.vertexCount) * 4 * sizeof(float);
            if (static_cast<uint64_t>(me.colorsOffset) + cBytes > size)
            {
                Engine_LogError("Model: mesh %u baked colours out of bounds.", i);
                Model_FreeBaked(outModel);
                return false;
            }
            mesh.colors = AllocFloats(static_cast<size_t>(me.vertexCount) * 4);
            if (!mesh.colors)
            {
                Model_FreeBaked(outModel);
                return false;
            }
            std::memcpy(mesh.colors, base + me.colorsOffset, static_cast<size_t>(cBytes));
        }

        // A material index outside the material table resolves to the first
        // material -- the one field a reader repairs rather than refuses,
        // because it selects a material rather than an address.
        uint32_t matIdx = me.materialIndex;
        if (materialCount == 0 || matIdx >= materialCount)
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
            Engine_PlatformFree(model->meshes[i].colors);
        }
        free(model->meshes);
    }
    free(model->materials);
    free(model->meshMaterial);
    std::memset(model, 0, sizeof(Model));
}
