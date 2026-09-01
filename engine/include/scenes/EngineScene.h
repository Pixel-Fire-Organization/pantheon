#pragma once

#include "../GameAPI.h"

/// Initialize the Scene subsystem. Call once during Engine_Init.
bool Engine_Scene_Init();

/// Stop whatever scene is active and forget it. Does not itself release any
/// resource; the runtime reset already run on every transition does that.
void Engine_Scene_Shutdown();

/// Drive the active scene for one frame: the loading screen while its
/// declared resources are outstanding, otherwise its OnUpdate. A no-op if no
/// scene has been registered yet.
void Engine_Scene_Update(float dt);

/// @return Whether a scene has been registered via SetMain/Switch.
bool Engine_Scene_HasActive();

// --- Real implementations behind the game::-namespaced entry points --------
// declared in GameAPI.h, forwarded to from GameAPI.cpp.

void Engine_Scene_SetMain(game::Scene* scene);
void Engine_Scene_Switch(game::Scene* scene);
void Engine_Scene_Reload();
bool Engine_Scene_IsLoading();
float Engine_Scene_GetLoadProgress();
