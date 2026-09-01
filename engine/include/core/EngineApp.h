#pragma once

#include <cstdint>

#include "EngineDebug.h"

class Platform;
class Renderer;
struct EngineConfig;

#ifdef __cplusplus
extern "C" {
#endif

// Start the engine on the platform and renderer Engine_Main selected, then call
// the game module's GameInit(). Returns false if the engine failed to initialise.
bool EngineStart(const EngineConfig& config, Platform* platform, Renderer* renderer);

// Advance one frame: calls GameUpdate(dt), renders, pumps IO and resource systems.
// Must be called inside the main loop while !EngineExited().
void EngineUpdate(void);

// Returns true when the engine should stop (window close or game::Exit()).
bool EngineExited(void);

// Ask the frame loop to stop after this frame. Declared here rather than
// forward-declared at each call site, which is how the two copies of this
// prototype drifted apart before.
void EngineApp_OnExitRequested();

// Shut down all engine subsystems and release resources. Call after the main loop.
void EngineStop(void);

#ifdef __cplusplus
}
#endif
