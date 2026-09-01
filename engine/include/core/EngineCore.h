#pragma once

#include <cstddef>
#include <cstdint>

#include "../graphics/Renderer.h"
#include "EngineSubsystems.h"

// Named (not an anonymous typedef) so the platform layer can forward-declare it
// without pulling this header, and with it the whole renderer include tree.
struct EngineConfig
{
    const char* windowTitle;
    const char* resourceLocationToken;

    // Which subsystems to bring up. The game sets this in GameConfigure(),
    // before the engine starts. Leave null for the default set (everything).
    // See EngineSubsystems.h and docs/ENGINE.md.
    const EngineSubsystem* subsystems;
    uint32_t subsystemCount;
};

// Bring the engine up on an already-initialised platform and renderer, both
// selected by Engine_Main. The engine does not construct either: which
// backends exist is the platform's business.
class Platform;

// Reserve the engine memory map from the platform and bring the arenas and
// main pool up. MUST run before any renderer is constructed: every backend
// takes its geometry staging buffer from ARENA_RENDERER slot 0 in its
// constructor. Engine_Main calls this, then builds the renderer, then
// Engine_Init.
bool Engine_InitMemory(Platform* platform);

bool Engine_Init(EngineConfig config, Platform* platform, Renderer* renderer);
bool Engine_Is_GFX_Initialized();
Renderer* Engine_GetRenderer();

void Engine_Update();

void Engine_Close();

// Return the engine to the state it was in just after startup, without
// restarting it: the level, every loaded resource, the mounted archives, the
// config and level-data arenas, the main pool and the draw lists all go back
// to empty, and the subsystems that own them are brought up again.
//
// The renderer and its arena are deliberately untouched. See
// docs/subsystems/MEMORY.md for why that is not an omission.
void Engine_ResetRuntimeState();

const char* Engine_GetResourceLocationToken();

// Performance metrics
float Engine_GetDeltaTime();
float Engine_GetFPS();
float Engine_GetTotalTime();

// Frame profiling
void Engine_ReportFrameStats(float logicTime, float renderTime, float waitTime);
float Engine_GetLogicTime();
float Engine_GetRenderTime();
float Engine_GetWaitTime();

// Frame counter, incremented once per Engine_Update() call. Used by the
// heartbeat log and perf snapshot to report frame number / detect hangs.
uint32_t Engine_GetFrameCount();

// Combine the active resource location token with a relative path. The active
// platform owns the grammar - separators, and any device or version suffix.
// Returns false if any argument is NULL or bufSize is 0.
bool Engine_BuildPath(const char* token, const char* relativePath, char* outBuf, size_t bufSize);

// Canonicalise an asset path into the stable key used for resource dedup and
// archive lookup: strips any device token (everything up to the first ':') and
// ";N" version suffix, converts '\\' to '/', upper-cases, and drops leading
// slashes. e.g. "cdrom0:/RASSETS/BOX.PS2A;1" and the baked dependency string
// "RASSETS/BOX.PS2A" both canonicalise to "RASSETS/BOX.PS2A". out must hold at
// least IO_FILE_MAX_PATH bytes.
void Engine_Path_Canonical(const char* in, char* out);
