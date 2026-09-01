#pragma once

// The debug testbed. Every entry point below has an empty implementation in a
// release build, where the scenes are not compiled at all. See
// docs/subsystems/TESTBED.md.

/// Turn on the subsystems the testbed needs, whatever the game asked for.
///
/// Called after the game's subsystem list is recorded and before it is
/// validated. The testbed must not need the game's cooperation to exist, and in
/// a release build it needs nothing at all.
void Engine_Testbed_RequestSubsystems();

/// @return False when the platform offers no chord to open it, which makes the
///         testbed unreachable rather than broken.
bool Engine_Testbed_Init();

void Engine_Testbed_Shutdown();

/// Read the chord, then draw the menu or run the active scene.
/// @param dt Seconds since the previous frame.
void Engine_Testbed_Update(float dt);

/// @return Whether the testbed owns this frame, in which case the game must not
///         run: a scene has the frame to itself.
bool Engine_Testbed_IsOpen();
