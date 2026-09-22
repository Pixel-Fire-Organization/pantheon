#pragma once
#include <cstddef>
#include <cstdint>

#include "Types.h"

// ---------------------------------------------------------------------------
// Baked material format (.mtl2), cooked as a RES_MATERIAL .ps2a payload by
// tools/cook_assets.py.
//
// A material's on-disc payload is exactly one fixed-size header -- there is
// no variable-length trailer, unlike a model or a font. Its texture slots
// name dependencies the same way a baked model's diffuse reference already
// does: an index into the owning .ps2a's own dependency list, resolved to a
// resource handle at load time.
// ---------------------------------------------------------------------------

#define BAKED_MATERIAL_MAGIC 0x324C544Du // "MTL2" little-endian
#define BAKED_MATERIAL_VERSION 1u

#define MATERIAL_TEXREF_NONE 0xFFFFFFFFu

struct MaterialAssetHeader
{
    uint32_t magic; // BAKED_MATERIAL_MAGIC
    uint32_t version; // BAKED_MATERIAL_VERSION
    uint8_t shaderType; // MaterialShaderType (Types.h)
    uint8_t flags; // MATERIAL_FLAG_* bitmask
    uint8_t reserved[2];
    float floatParams[MATERIAL_MAX_FLOAT_PARAMS];
    float colorParams[MATERIAL_MAX_COLOR_PARAMS][4]; // RGBA
    // Index into this asset's own dependency list; MATERIAL_TEXREF_NONE = none.
    uint32_t textureRefs[MATERIAL_MAX_TEXTURE_SLOTS];
};

// On-disc size is contractual with tools/cook_assets.py — locked so a field
// reorder that introduces padding fails the build instead of the game.
static_assert(sizeof(MaterialAssetHeader) == 12 + MATERIAL_MAX_FLOAT_PARAMS * 4 + MATERIAL_MAX_COLOR_PARAMS * 16 + MATERIAL_MAX_TEXTURE_SLOTS * 4, "MaterialAssetHeader size");

// Resolve a texture reference (an index into the owning .ps2a's dependency
// list) to a resource handle. Mirrors ModelMaterialResolver (ModelFormat.h).
typedef int32_t (*MaterialTextureResolver)(uint32_t texRef, void* user);

// Parse a baked-material blob into `outMaterial`. `resolver` may be null (all
// texture slots become unbound, -1). Returns true on success; a payload that
// fails any check is refused whole and reported, never repaired in place.
bool Material_LoadBaked(const void* data, size_t size, Material* outMaterial, MaterialTextureResolver resolver, void* resolverUser);
