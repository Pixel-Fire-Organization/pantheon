#pragma once

// ---------------------------------------------------------------------------
// The system's request that the title exit.
//
// Alone among the consoles here, this one asks rather than terminating: the
// player presses the hardware menu button and the system delivers a callback on
// a thread of its own. See docs/psp/PLATFORM.md.
// ---------------------------------------------------------------------------

/// Register the exit callback and start the thread that services it. Called
/// once from the process entry point, before the engine starts.
void PspExit_Install();

/// @return True once the system has asked the title to exit.
bool PspExit_Requested();

/// Hand control back to the system. Called after the engine has shut down.
/// @param code Process exit code, reported to the system.
[[noreturn]] void PspExit_Finish(int code);
