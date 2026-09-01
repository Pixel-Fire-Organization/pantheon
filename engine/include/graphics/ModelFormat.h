#pragma once
#include <cstddef>
#include <cstdint>

#include "Types.h"

// ---------------------------------------------------------------------------
// Baked model format (.bkm) produced by pack_assets.py.
//
// Raylib's runtime OBJ/glTF loader is gone; models are baked offline into
// separated, UNINDEXED triangle arrays with stride-0 vertex/normal/uv pointers.
// Baked unindexed because at least one supported backend cannot draw indexed
// geometry at all; every other backend consumes this layout directly.
//
// On-disk layout (little-endian, all sections 16-byte aligned):
//   BakedModelHeader
//   BakedMeshEntry   [meshCount]
//   BakedMaterialEntry [materialCount]
//   <geometry payload>   (referenced by absolute byte offset from file start)
//
// Version 2 changes vs. version 1:
//   * Positions are baked as vec4 (x, y, z, 1) — a 16-byte stride that vector
//     transform paths can consume in place, with no per-frame repack.
//   * Each mesh may be a triangle STRIP (degenerate-stitched, one strip/mesh)
//     or a triangle LIST (fallback), selected by `topology`.
//   * Each mesh carries a baked object-space bounding sphere for frustum cull.
// The loader still accepts version 1 (vec3 list geometry, bounds derived at load).
// ---------------------------------------------------------------------------

#define BAKED_MODEL_MAGIC 0x324D4B42u // "BKM2" little-endian
#define BAKED_MODEL_VERSION 2u
#define BAKED_MODEL_VERSION_LEGACY 1u

// Mesh primitive topology (matches MESH_TOPOLOGY_* in Types.h).
#define BAKED_TOPOLOGY_LIST 0u
#define BAKED_TOPOLOGY_STRIP 1u

struct BakedModelHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t meshCount;
    uint32_t materialCount;
};

// Version 1 mesh entry (24 bytes) — vec3 positions, triangle list only.
struct BakedMeshEntryV1
{
    uint32_t vertexCount; // triangleCount * 3 (unindexed)
    uint32_t materialIndex; // index into the material table (0 if none)
    uint32_t vertsOffset; // byte offset to 3*float*vertexCount (required)
    uint32_t normsOffset; // byte offset to 3*float*vertexCount (0 = none)
    uint32_t uvsOffset; // byte offset to 2*float*vertexCount (0 = none)
    uint32_t reserved;
};

// Version 2 mesh entry (48 bytes) — vec4 positions, strip-or-list + bounds.
struct BakedMeshEntry
{
    uint32_t vertexCount; // strip: total incl. degenerates; list: triangleCount*3
    uint32_t materialIndex; // index into the material table (0 if none)
    uint32_t vertsOffset; // byte offset to 4*float*vertexCount (x,y,z,1) (required)
    uint32_t normsOffset; // byte offset to 3*float*vertexCount (0 = none)
    uint32_t uvsOffset; // byte offset to 2*float*vertexCount (0 = none)
    uint32_t topology; // BAKED_TOPOLOGY_LIST or BAKED_TOPOLOGY_STRIP
    float boundsCenter[3]; // object-space bounding-sphere center
    float boundsRadius; // object-space bounding-sphere radius
    uint32_t reserved[2];
};

struct BakedMaterialEntry
{
    // Reference to the diffuse texture: an index into the owning .ps2a's
    // dependency list, resolved to a Texture2D at load time. 0xFFFFFFFF = none.
    uint32_t diffuseTexRef;
    uint32_t reserved;
};

#define BAKED_MODEL_TEXREF_NONE 0xFFFFFFFFu

// Resolve a material's diffuse texture reference (an index into the owning
// .ps2a's dependency list) to a resource handle. Returns the handle (>= 0) or
// -1 when unavailable. The handle is stored in the material and resolved to a
// live Texture2D at draw time (texture deps stream in asynchronously).
typedef int32_t (*ModelTextureResolver)(uint32_t diffuseTexRef, void* user);

// Parse a baked-model blob into `outModel`. Geometry arrays are copied into
// fresh 16-byte-aligned heap allocations (so `data` may be freed afterwards).
// `resolver` may be null (all materials become untextured). Returns true on
// success; call Model_FreeBaked to release everything it allocated.
bool Model_LoadBaked(const void* data, size_t size, Model* outModel, ModelTextureResolver resolver, void* resolverUser);

// Free all heap allocations made by Model_LoadBaked and zero the struct.
// Does NOT release textures (those are owned by the resource manager).
void Model_FreeBaked(Model* model);
