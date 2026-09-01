#pragma once

#include <cstddef>
#include <cstdint>

#include "../core/CommandLine.h"
#include "graphics/Types.h" // Vector2, for TouchContact

// ---------------------------------------------------------------------------
// Plain data types exchanged across the Platform interface.
//
// Handles are incomplete types used only through pointers: the engine never
// needs their layout, and each platform defines its own behind the pointer
// (a PS2 kernel thread id, a Win32 HANDLE, a FILE*).
// ---------------------------------------------------------------------------

struct PlatformFile;
struct PlatformThread;
struct PlatformSemaphore;

typedef PlatformFile* FileHandle;

// Entry point for Platform::ThreadCreate. Runs until it returns; the engine's
// only consumer is the async IO worker.
typedef void (*ThreadEntry)(void* userData);

// --- Startup ----------------------------------------------------------------
// What the process was started with. Handed to Platform::Init and retained by
// the platform for its lifetime, so a backend can consult its own flags long
// after startup (e.g. --gl-version when a renderer is recreated).
struct StartupArgs
{
    const CommandLine* commandLine; // parsed argv; never null after Engine_Main
    char** argv; // the original array, still owned by main()
    int argc;
};

// --- Memory -----------------------------------------------------------------
// Upper bound on arena kinds a platform may describe. Kept independent of
// ArenaType in EngineMemory.h so this header does not depend on that one.
#define PLATFORM_MAX_ARENAS 4

struct EngineArenaDesc
{
    size_t size; // bytes reserved for this arena
    uint32_t slots; // fixed slot count carved out of `size`
};

// The complete engine memory map, produced by Platform::ReserveEngineMemory.
// The platform allocates the backing blocks and enforces its own ceiling before
// returning, so the engine never needs to know a hardware RAM limit.
struct EngineMemoryMap
{
    void* arenaBlock; // one contiguous, slotAlignment-aligned block for all arenas
    void* poolBlock;
    EngineArenaDesc arenas[PLATFORM_MAX_ARENAS];
    size_t arenaBlockSize; // sum of arenas[0..arenaCount).size
    size_t poolSize;
    size_t poolChunkSize;
    size_t slotAlignment;
    uint32_t arenaCount;
};

struct HeapStats
{
    size_t totalBytes; // the platform's ceiling, not the OS total
    size_t usedBytes;
    size_t freeBytes;
};

/// One finger on one touch surface. Position is normalised to [0,1] over the
/// surface with origin top-left; id is stable while the finger stays down.
struct TouchContact
{
    Vector2 position;
    float force;
    uint8_t id;
};

// --- Window -----------------------------------------------------------------
// Requested window state. A platform with a fixed framebuffer (PS2) ignores
// every field but `title` and reports its real size from GetFramebufferSize.
struct WindowDesc
{
    const char* title;
    uint32_t width;
    uint32_t height;
    bool resizable;
    bool vsync;
};

// --- System dialogs -----------------------------------------------------
// One host facility serves a message box and a text field alike: both are a
// modal request the platform may own outright. The contract is shaped for the
// stricter of the two hosts that implement it -- a Vita dialog renders into
// the title's own frame and only advances while the title keeps presenting,
// so Dialog_Open must never block. A host that blocks internally (Win32's
// MessageBox) simply returns having already decided the result, and the first
// Dialog_Poll reports it.

enum class DialogKind : uint8_t
{
    Message = 0, // one dismiss action; Accepted is the only non-Cancelled result
    Confirm, // accept or cancel
    TextInput, // edits request.textBuffer in place

    Count
};

enum class DialogStatus : uint8_t
{
    Idle = 0, // nothing open; also what a platform with no dialog facility always answers
    Pending, // still open, keep presenting frames and polling
    Accepted,
    Cancelled,

    Count
};

// What Dialog_Open is asked to show. For TextInput, textBuffer is read for the
// value shown on open and, on an Accepted poll, holds what the player entered;
// on Cancelled it is left exactly as it was passed in, so a caller never has
// to keep its own copy to restore.
struct DialogRequest
{
    DialogKind kind;
    const char* title;
    const char* body; // Message, Confirm
    char* textBuffer; // TextInput; null for the other kinds
    size_t textBufferSize;
};
