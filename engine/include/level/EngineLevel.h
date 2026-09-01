#pragma once

#include <cstdint>

#include "EngineLevelFormat.h"
#include "PlatformConstants.h"

// Runtime level descriptor. Filled by Engine_Level_Load by resolving a
// compiled level's entries (produced by tools/compile_level.py) out of the
// master archive, alongside every other asset — see docs/subsystems/LEVEL.md.
// The chunk pointers are views into the resident level-core arena slot(s); the
// sector geometry streams separately (see EngineSector.h).
typedef struct
{
    char name[64]; // set by the caller; names the level's entry keys (e.g. "TEST")

    const LevelInfoChunk* info; // views into ARENA_LEVEL_DATA core slot
    const LevelMaterialEntry* materials;
    const LevelGridCell* grid;
    const uint8_t* entsChunk; // raw ENTS chunk (count/records/props/strings)
    const uint8_t* farfieldChunk; // raw FARF chunk, or null

    int32_t materialTex[LEVEL_MAX_MATERIALS]; // pinned texture resource handles
} Level;

// Load a compiled level: read its core from the master archive into resident
// slots, pin material textures, spawn its entities via the game's spawn
// handler, and prime the resident sector ring. Blocking (call from a load
// screen). `level->name` must be set; other fields are filled in. Returns
// false on error.
bool Engine_Level_Load(Level* level);

// Unload: release the sector ring, unpin textures (unless keepPinned), and
// clear the level-data arena. The master archive stays mounted.
void Engine_Level_Unload(Level* level, bool keepPinned);

// Update the streaming centre (world X/Z). Recenters the resident sector ring
// with hysteresis. Call each frame with the camera/player position.
void Engine_Level_SetStreamingCenter(float worldX, float worldZ);

// The currently loaded level, or null. Used by the renderer to draw sectors.
const Level* Engine_Level_Current();

// Unload whatever level is current, exactly as Engine_Level_Unload(level, false)
// would, without the caller needing to still hold the Level it was loaded
// into. A no-op when nothing is loaded. For a runtime reset, which owns no
// game-side state of its own but must still stop the renderer from drawing a
// level nothing asked for any more.
void Engine_Level_ForgetCurrent();
