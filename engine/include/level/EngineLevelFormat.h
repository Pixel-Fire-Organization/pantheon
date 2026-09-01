#pragma once

#include <cstdint>

#include "../graphics/ModelFormat.h"
#include "PlatformConstants.h"

// ---------------------------------------------------------------------------
// Compiled level format (.ps2l v2) — produced by tools/compile_level.py.
//
// A level is split into a fixed 2D grid of sectors. The .ps2l "core" (this file's
// chunks) is always resident once loaded; the per-sector geometry (PSEC blobs)
// streams in and out of ARENA_LEVEL_DATA slots as the camera crosses cells. The
// far field (FARF) is camera-facing billboard impostors for cells outside the
// resident 3x3 ring.
//
// On-disc: LevelFileHeaderV2, then LevelChunkEntry[chunkCount], then the chunk
// payloads. All little-endian; 16-byte alignment for anything a transfer path
// touches.
//
// BSP (indoor) is deferred: LEVEL_CHUNK_BSP and SectorHeader.bvhOffset are
// reserved so an indoor tree can be added later without a format break.
// ---------------------------------------------------------------------------

// --- Format constants ----------------------------------------------------
// Shared with tools/compile_level.py; identical on every platform.

// Every chunk payload and every sector geometry array starts on this boundary.
// Mirrored by tools/ps2lib/levelfmt.py as CHUNK_ALIGN.
#define LEVEL_CHUNK_ALIGN 16u

#define LEVEL_FILE_MAGIC 0x4C325350u /* "PS2L" in little-endian */
#define LEVEL_FILE_VERSION 2u /* v2: chunked, sectorized */
#define LEVEL_FILE_EXT ".ps2l"

// Materials (textures) referenced by a level's geometry.
#define LEVEL_MAX_MATERIALS 64

// Meshes per streamed sector (one per material present in the sector's cell).
#define LEVEL_MAX_MESHES_PER_SECTOR 32

// A streamed sector payload (PSEC) must fit one ARENA_LEVEL_DATA slot. The
// level compiler hard-fails any sector larger than this; each platform
// static_asserts that its own slot capacity is at least this big.
#define LEVEL_SECTOR_MAX_BYTES (512 * 1024)

// Far-field impostor azimuth views baked per cluster (N/E/S/W). Data-driven at
// runtime via FarfieldHeader.azimuthCount; this is the compiler default.
#define LEVEL_FARFIELD_AZIMUTHS 4

// Impostor atlases a far field may sample; sizes FarfieldHeader.atlasMaterial.
// Mirrored by tools/ps2lib/levelfmt.py as FARFIELD_MAX_ATLASES.
#define LEVEL_FARFIELD_MAX_ATLASES 4

// Max key/value properties read from one entity spawn record.
#define LEVEL_MAX_ENTITY_PROPS 32

typedef struct
{
    uint32_t magic; // LEVEL_FILE_MAGIC "PS2L"
    uint32_t version; // LEVEL_FILE_VERSION (2)
    uint32_t chunkCount;
    uint32_t totalSize;
} LevelFileHeaderV2;

typedef struct
{
    uint32_t type; // LEVEL_CHUNK_*
    uint32_t offset; // byte offset from file start
    uint32_t size; // chunk payload size
    uint32_t reserved;
} LevelChunkEntry;

#define LEVEL_CHUNK_INFO 0x4F464E49u /* "INFO" */
#define LEVEL_CHUNK_MATERIALS 0x4C54414Du /* "MATL" */
#define LEVEL_CHUNK_GRID 0x44524753u /* "SGRD" */
#define LEVEL_CHUNK_ENTITIES 0x53544E45u /* "ENTS" */
#define LEVEL_CHUNK_FARFIELD 0x46524146u /* "FARF" */
#define LEVEL_CHUNK_BSP 0x54505342u /* "BSPT" — reserved, never emitted in v1 */

// INFO — one per level.
typedef struct
{
    char name[64];
    float gridOriginX; // world-space min corner of cell (0,0)
    float gridOriginZ;
    float cellSize; // world units per cell (square, on the X/Z plane)
    uint16_t cellsX;
    uint16_t cellsZ;
    uint16_t materialCount;
    uint16_t entityCount;
    uint32_t reserved[2];
} LevelInfoChunk;

// MATL — materialCount entries: the canonical archive key of each TIM2 .PS2A.
typedef struct
{
    char assetKey[64];
} LevelMaterialEntry;

// SGRD — cellsX * cellsZ entries, row-major (index = z * cellsX + x).
typedef struct
{
    uint32_t sectorBytes; // PSEC payload size; 0 = empty cell (no sector entry)
    float aabbMin[3];
    float aabbMax[3];
    uint16_t entityFirst; // index into ENTS records (hooks only in v1)
    uint16_t entityCount;
} LevelGridCell;

// ENTS — { uint32 count; uint32 stringsOffset; uint32 stringsSize } then
// LevelEntityRecord[count], LevelEntityProp[...], then the string table.
typedef struct
{
    uint32_t classnameOffset; // into the ENTS string table
    float origin[3];
    uint16_t propFirst; // index into the LevelEntityProp array
    uint16_t propCount;
} LevelEntityRecord;

typedef struct
{
    uint32_t keyOffset; // into the ENTS string table
    uint32_t valueOffset;
} LevelEntityProp;

// --- Per-sector streamed payload (PSEC) -------------------------------------
// A separate archive entry per non-empty cell, named "<LEVEL>/S%03u_%03u.SEC".
// Layout: SectorHeader, BakedMeshEntry[meshCount] (materialIndex = MATL index),
// then 16-byte-aligned vec4/vec3/vec2 geometry arrays referenced by the entries'
// absolute byte offsets. The runtime builds Mesh views straight into the arena
// slot — zero-copy into the existing render path.
#define LEVEL_SECTOR_MAGIC 0x43455350u /* "PSEC" */
#define LEVEL_SECTOR_VERSION 1u

typedef struct
{
    uint32_t magic; // LEVEL_SECTOR_MAGIC
    uint32_t version; // LEVEL_SECTOR_VERSION
    uint32_t meshCount;
    uint32_t bvhOffset; // 0 in v1 — reserved per-sector BVH hook
    float aabbMin[3];
    float aabbMax[3];
    uint32_t reserved[2];
} SectorHeader;

// --- Far field (FARF) -------------------------------------------------------
// One cluster per non-empty cell; azimuthCount frames per cluster (billboard
// impostor views). atlasMaterial[] index into the level MATL table.
typedef struct
{
    uint32_t clusterCount;
    uint32_t atlasCount;
    uint32_t atlasMaterial[LEVEL_FARFIELD_MAX_ATLASES]; // MATL indices of the impostor atlases
    uint32_t azimuthCount;
    uint32_t groundOffset; // reserved (coarse ground quad); 0 = none
} FarfieldHeader;

typedef struct
{
    float center[3];
    float halfWidth;
    float halfHeight;
    uint16_t atlasIndex; // which atlasMaterial[] this cluster samples
    uint16_t firstFrame; // index of its first FarfieldFrame (azimuthCount frames)
} FarfieldCluster;

typedef struct
{
    float u0, v0, u1, v1; // atlas UV rect for one azimuth view
} FarfieldFrame;

// On-disc sizes are contractual with tools/ps2lib/levelfmt.py — lock them so a
// field reorder that introduces padding fails the build instead of the game.
static_assert(sizeof(LevelInfoChunk) == 92, "LevelInfoChunk size");
static_assert(sizeof(LevelGridCell) == 32, "LevelGridCell size");
static_assert(sizeof(LevelEntityRecord) == 20, "LevelEntityRecord size");
static_assert(sizeof(LevelEntityProp) == 8, "LevelEntityProp size");
static_assert(sizeof(SectorHeader) == 48, "SectorHeader size");
static_assert(sizeof(FarfieldHeader) == 32, "FarfieldHeader size");
static_assert(sizeof(FarfieldCluster) == 24, "FarfieldCluster size");
static_assert(sizeof(FarfieldFrame) == 16, "FarfieldFrame size");
