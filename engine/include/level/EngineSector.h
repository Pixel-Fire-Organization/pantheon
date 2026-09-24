#pragma once

#include <cstdint>

#include "../graphics/Frustum.h"
#include "../graphics/Types.h"
#include "EngineLevel.h"
#include "PlatformConstants.h"

typedef enum
{
    SECTOR_EMPTY = 0,
    SECTOR_LOADING,
    SECTOR_READY
} SectorState;

typedef struct
{
    int16_t cellX; // grid cell, or -1 when unused
    int16_t cellZ;
    uint8_t arenaSlot; // ARENA_LEVEL_DATA (or ARENA_LEVEL_LOD1) slot holding this sector's PSEC blob
    uint8_t state; // SectorState
    uint8_t generation; // incremented on eviction; async callbacks compare against this
    bool isLod1; // streamed from the LOD1 ring rather than the LOD0 one
    uint8_t _pad;
    uint32_t meshCount;
    Mesh meshes[LEVEL_MAX_MESHES_PER_SECTOR]; // views into the slot geometry
    int32_t meshMaterial[LEVEL_MAX_MESHES_PER_SECTOR]; // resolved RES_MATERIAL handles
    Aabb3 bounds; // world-space sector AABB (from the PSEC header)
} SectorResident;

// Begin/end a level's sector residency. Begin resets the ring; End frees slots.
bool Engine_Sector_Begin(const Level* level);
void Engine_Sector_End();

// Recenter the ring on a world-space streaming centre (with hysteresis).
void Engine_Sector_Update(float worldX, float worldZ);

// The resident sector array (fixed capacity). `outCount` receives the number of
// entries whose state != SECTOR_EMPTY need not be contiguous — iterate all and
// skip SECTOR_EMPTY.
const SectorResident* Engine_Sector_GetResidents(uint32_t* outCount);
/// The LOD1 resident array (fixed capacity), iterated the same way as
/// Engine_Sector_GetResidents.
/// @param outCount Receives the array's capacity.
/// @return The LOD1 residents.
const SectorResident* Engine_Sector_GetLod1Residents(uint32_t* outCount);

/// Whether a ready resident can be drawn without a texture popping in later:
/// every mesh's material and that material's albedo texture are resident.
/// @param res The resident to test.
/// @return Whether every mesh is fully resolvable.
bool Engine_Sector_IsResidentReady(const SectorResident* res);

/// Whether the LOD0 sector for a cell is resident and fully resolvable, so the
/// LOD1 sector covering the same cell may fade out.
/// @param cellX Grid cell X.
/// @param cellZ Grid cell Z.
/// @return Whether LOD0 can take over from LOD1 for that cell.
bool Engine_Sector_IsLod0Ready(int16_t cellX, int16_t cellZ);

/// How opaque a LOD1 resident should be drawn. Fully opaque while the
/// full-detail sector for its cell is not yet fully resolvable, so a slow read
/// shows the coarse version rather than a hole. Once it is, a backend that
/// fades gets a distance-based opacity across the fade band, and one that does
/// not gets zero, so the two tiers are never both drawn opaque over one cell.
/// @param res The LOD1 resident.
/// @param cameraX Camera world X.
/// @param cameraZ Camera world Z.
/// @param fade Whether the caller can draw translucent 3D geometry.
/// @return Opacity in [0, 1]; zero means do not draw.
float Engine_Sector_Lod1Opacity(const SectorResident* res, float cameraX, float cameraZ, bool fade);
