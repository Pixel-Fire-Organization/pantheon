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
    uint8_t arenaSlot; // ARENA_LEVEL_DATA slot holding this sector's PSEC blob
    uint8_t state; // SectorState
    uint32_t meshCount;
    Mesh meshes[LEVEL_MAX_MESHES_PER_SECTOR]; // views into the slot geometry
    int32_t meshTexture[LEVEL_MAX_MESHES_PER_SECTOR]; // resolved texture handles
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
