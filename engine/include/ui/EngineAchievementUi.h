#pragma once

#include <cstdint>

/// Queue an unlock for the player to see. Dropped silently once the queue is
/// full: an unlock is recorded whether or not it can be shown, and delaying a
/// hundred of them would be worse than not showing them.
/// @param id The achievement that was earned.
void Engine_AchievementUi_Notify(uint32_t id);

/// Forget anything queued and close the screen. For a runtime reset, which
/// clears what the engine is showing without touching what the player earned.
void Engine_AchievementUi_Reset();
