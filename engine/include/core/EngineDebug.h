#pragma once

#include <cstddef>

#include "../platform/PlatformKeys.h"

// Initialize debug logging and on-screen debug overlay
void Engine_InitDebug();

// Log a message
void Engine_LogInfo(const char* text, ...);

void Engine_LogError(const char* text, ...);

// Draw the debug overlay (FPS, memory usage, etc.)
// Assumes the active renderer's BeginFrame() has been called.
void Engine_DrawDebugOverlay();

// Draw a printable ASCII table (codepoints 33–127) for font/glyph diagnostics.

// Stop the engine. Never returns: the active platform decides what a panic
// looks like, and every platform terminates. See docs/subsystems/DEBUG.md.
[[noreturn]] void Engine_Panic(const char* message);

// ---------------------------------------------------------------------------
// Performance Logger
// Dumps a per-frame snapshot (draw counts, VRAM, RAM, pads, BIOS) to console.
// Only active when enablePerfLogger was set in EngineConfig.
// ---------------------------------------------------------------------------
void Engine_PerfLogger_Init(bool enabled);
void Engine_PerfLogger_Tick();

// ---------------------------------------------------------------------------
// Debug chords
// ---------------------------------------------------------------------------

/// @param chord Which debug action to test.
/// @return True while every button in this platform's chord for that intent is
///         held on port 0; false when the platform offers no chord for it.
bool Engine_Debug_IsChordHeld(DebugChord chord);

/// Rising edge of a chord: every button held now, and at least one of them went
/// down this frame. Holds no state, so every caller in a frame gets the same
/// answer whatever order they ask in.
/// @param chord Which debug action to test.
/// @return True on the frame the chord completes.
bool Engine_Debug_WasChordPressed(DebugChord chord);

/// Render this platform's chord for an intent as text, in pad-reading order.
/// @param chord Which debug action to describe.
/// @param buf Receives the description.
/// @param bufSize Capacity of buf.
/// @return buf; reads "unavailable" when this platform has no such chord.
const char* Engine_Debug_DescribeChord(DebugChord chord, char* buf, size_t bufSize);
