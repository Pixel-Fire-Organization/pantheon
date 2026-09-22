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
//   <geometry payload>   (referenced by absolute byte offset from file start)
//
// Version history: version 1 baked vec3-list-only geometry with bounds
// derived at load time; version 2 added vec4 positions, strip topology and
// baked bounds; version 3 (current) removed the embedded per-model material
// table (materials are now dependencies -- see below) and added an optional
// per-vertex baked colour. Versions before 3 are no longer accepted: cook
// tooling and the loader change together, so old content is rebuilt, not
// migrated.
//
// Version 3 specifics:
//   * A mesh's `materialIndex` is an index into the owning .ps2a's own
//     dependency list, each of which names a RES_MATERIAL asset — see
//     MaterialFormat.h. A model's "MATERIALS" are therefore just its
//     dependencies; the generic dependency list (every asset already
//     carries one) is what names them.
//   * Each mesh entry carries an optional `colorsOffset`, a baked per-vertex
//     RGBA colour. Only PSEC (level sector) meshes populate it, from the
//     level compiler's static lighting bake — a placed, reusable model has no
//     single correct baked lighting, so BKM2 models leave it 0 (none) and
//     stay fully dynamically lit.
// ---------------------------------------------------------------------------

#define BAKED_MODEL_MAGIC 0x324D4B42u // "BKM2" little-endian
#define BAKED_MODEL_VERSION 3u

// Mesh primitive topology (matches MESH_TOPOLOGY_* in Types.h).
#define BAKED_TOPOLOGY_LIST 0u
#define BAKED_TOPOLOGY_STRIP 1u

struct BakedModelHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t meshCount;
    uint32_t reserved;
};

struct BakedMeshEntry
{
    uint32_t vertexCount; // strip: total incl. degenerates; list: triangleCount*3
    uint32_t materialIndex; // index into the owning asset's own deps[] (0 if none)
    uint32_t vertsOffset; // byte offset to 4*float*vertexCount (x,y,z,1) (required)
    uint32_t normsOffset; // byte offset to 3*float*vertexCount (0 = none)
    uint32_t uvsOffset; // byte offset to 2*float*vertexCount (0 = none)
    uint32_t colorsOffset; // byte offset to 4*float*vertexCount RGBA (0 = none)
    uint32_t topology; // BAKED_TOPOLOGY_LIST or BAKED_TOPOLOGY_STRIP
    float boundsCenter[3]; // object-space bounding-sphere center
    float boundsRadius; // object-space bounding-sphere radius
    uint32_t reserved[1];
};

// On-disc sizes are contractual with tools/ps2lib/mesh.py and
// tools/ps2lib/levelfmt.py — lock them so a field reorder that introduces
// padding fails the build instead of the game.
static_assert(sizeof(BakedModelHeader) == 16, "BakedModelHeader size");
static_assert(sizeof(BakedMeshEntry) == 48, "BakedMeshEntry size");

// Resolve a material slot (an index into the owning .ps2a's own dependency
// list) to the resource handle of the RES_MATERIAL asset named there. Returns
// the handle (>= 0) or -1 when unavailable; the handle is resolved to a live
// Material at draw time, exactly like a texture dependency streams in.
typedef int32_t (*ModelMaterialResolver)(uint32_t materialSlot, void* user);

// Parse a baked-model blob into `outModel`. Geometry arrays are copied into
// fresh 16-byte-aligned heap allocations (so `data` may be freed afterwards).
// `materialCount` is the owning asset's authoritative dependency count (every
// dependency of a model names a material); the payload's own copy, if any, is
// never trusted for indexing. `resolver` may be null (every material becomes
// unbound). Returns true on success; call Model_FreeBaked to release
// everything it allocated.
bool Model_LoadBaked(const void* data, size_t size, uint32_t materialCount, Model* outModel, ModelMaterialResolver resolver, void* resolverUser);

// Free all heap allocations made by Model_LoadBaked and zero the struct.
// Does NOT release textures (those are owned by the resource manager).
void Model_FreeBaked(Model* model);
