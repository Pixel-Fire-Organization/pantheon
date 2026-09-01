#pragma once

#include <stdint.h>

#include "../core/EngineIO.h"
#include "PlatformConstants.h"

// --- .ps2a on-disc format (written by tools/pack_assets.py) --------------
// FORMAT CONSTANTS - shared with the packer, identical on every platform.
#define RES_ASSET_MAGIC 0x50533241 /* "PS2A" in little-endian */
#define RES_MAX_DEPENDENCIES 8

typedef enum
{
    RES_TEXTURE,
    RES_MODEL,
    RES_SOUND,
    RES_FONT,
    RES_THEME
} ResourceType;

// Lifecycle state of a resource entry
typedef enum
{
    RES_STATE_EMPTY,
    RES_STATE_LOADING,
    RES_STATE_READY
} ResourceState;

// .ps2a file header (binary, written by pack_assets.py, read at runtime)
// Total size: 4+4+1+3+16+(8*256)+4 = 2080 bytes
typedef struct
{
    uint32_t magic; // RES_ASSET_MAGIC
    uint32_t type; // ResourceType
    uint8_t depCount; // Number of dependencies
    uint8_t reserved[3]; // Padding
    char ext[16]; // Source file extension e.g. ".jpg", ".png"
    char deps[RES_MAX_DEPENDENCIES][IO_FILE_MAX_PATH]; // Dependency asset paths
    uint32_t dataSize; // Size of payload after header
} AssetFileHeader;

// The payload starts immediately after the header, so the header's size is what
// carries the read buffer's 16-byte alignment through to a cooked payload that
// a transfer path consumes in place (TIM2 pixels, baked geometry).
static_assert(sizeof(AssetFileHeader) % 16 == 0, "AssetFileHeader must not break the payload's transfer alignment");

// Initialize the resource manager (call once during Engine_Init)
bool Engine_Resource_Init();

// Shut down and release all resources
void Engine_Resource_Shutdown();

// Load a resource from a .ps2a file on disc. All types stream via
// Engine_IO_ReadAsync and decode from memory: cooked textures (uploaded by the
// VRAM by the active renderer) and the baked blob for models. Dependencies
// declared in the .ps2a header are loaded first.
// Returns a handle >= 0 on success, or -1 on failure.
int32_t Engine_Resource_Load(ResourceType type, const char* path);

// Load a resource whose type is inferred from the .ps2a header's type field.
// Use this instead of Engine_Resource_Load when the caller does not know the
// type a priori (e.g. level required-resource lists that may contain any type).
// Returns a handle >= 0 on success, or -1 if the file cannot be opened, the
// header is invalid, or the underlying load fails.
int32_t Engine_Resource_LoadAuto(const char* path);

// Retrieve a pointer to the underlying engine resource.
// Returns NULL if the resource is not yet ready or the handle is invalid.
// Caller casts to the matching engine type (Texture2D* or Model*).
void* Engine_Resource_Get(int32_t handle);

// Check if a resource has finished loading.
bool Engine_Resource_IsReady(int32_t handle);

// Pin a resource: mark it as content the caller cannot tolerate losing. Nothing
// is ever reclaimed automatically, pinned or not; the flag records intent and
// keeps the entry out of the candidate a full-table error names.
void Engine_Resource_Pin(int32_t handle);

// Unpin a resource, letting a full-table error name it as releasable.
void Engine_Resource_Unpin(int32_t handle);

// Explicitly unload a single resource and decrement refCounts on its deps.
void Engine_Resource_Unload(int32_t handle);

// Force-unload all resources (including pinned). Used during shutdown.
void Engine_Resource_UnloadAll();

// Called once per frame (from Engine_Update) to advance the current frame counter.
void Engine_Resource_Update();

// --- Introspection ---
// Read-only, for diagnostics. Nothing here loads, unloads or touches use order.

// What one slot of the resource table holds.
typedef struct
{
    const char* key; // canonical key; valid while the slot holds this asset
    ResourceType type;
    ResourceState state;
    uint32_t refCount;
    uint32_t textureBytes; // RES_TEXTURE only; zero otherwise
    int32_t width; // RES_TEXTURE only
    int32_t height; // RES_TEXTURE only
    uint8_t depCount;
    bool pinned;
} ResourceInfo;

// Describe one slot. Returns false when the slot is empty or out of range,
// which is how a caller walks the whole table.
bool Engine_Resource_GetInfo(int32_t handle, ResourceInfo* outInfo);

// How many slots the table has, empty ones included.
uint32_t Engine_Resource_GetCapacity();

uint32_t Engine_Resource_GetTextureBudgetUsed();

// The active platform's total texture budget, in bytes.
uint32_t Engine_Resource_GetTextureBudget();
