#pragma once

// A full-screen message shown before the game runs, for a condition the player
// can act on but the game did not cause. Present in every configuration: the
// person who needs to read it is the player, not the developer. See
// docs/subsystems/DEBUG.md.

class Platform;

/// Turn on what a notice needs to draw itself, whatever the game asked for.
///
/// Called after the game's subsystem list is recorded and before it is
/// validated, while it is still only knowable that a notice *might* be raised.
/// @param platform The platform being brought up.
void Engine_Notice_RequestSubsystems(Platform* platform);

/// Decide whether anything needs saying, once every subsystem is up and the
/// answers are known. Raises at most one notice.
void Engine_Notice_Evaluate();

/// @return Whether a notice is waiting to be read, and therefore owns the frame.
bool Engine_Notice_IsPending();

/// Draw the pending notice and let it be dismissed.
/// @param dt Seconds since the previous frame.
/// @return True while the notice still owns the frame, so the caller runs
///         neither the game nor anything else this frame.
bool Engine_Notice_Update(float dt);
