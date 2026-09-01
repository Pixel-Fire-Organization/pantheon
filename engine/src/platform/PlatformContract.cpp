// ---------------------------------------------------------------------------
// The platform constants contract.
//
// Every platform must define the constants below in its PlatformConstants.h.
// This translation unit is compiled into every engine library, so a platform
// that omits one fails the build here with a message naming it - rather than
// somewhere far away with "undeclared identifier", or silently as 0 inside an
// #if.
//
// The static_asserts below check the relationships between them. A platform can
// define every constant and still be wrong: a resident sector ring that does not
// fit its arena writes outside its slots.
//
// PS2-only constants (GS pages, GIF packet sizes) are deliberately absent - they
// are that platform's business and must not be required of a new one.
//
// See docs/guidelines/NEW_PLATFORM.md.
// ---------------------------------------------------------------------------

#include "PlatformConstants.h"
#include "level/EngineLevelFormat.h"

// --- Display -------------------------------------------------------------
#ifndef GFX_SCREEN_WIDTH
    #error "platform must define GFX_SCREEN_WIDTH in its PlatformConstants.h"
#endif
#ifndef GFX_SCREEN_HEIGHT
    #error "platform must define GFX_SCREEN_HEIGHT in its PlatformConstants.h"
#endif
#ifndef GFX_SCREEN_REGION_STR
    #error "platform must define GFX_SCREEN_REGION_STR in its PlatformConstants.h"
#endif
#ifndef GFX_DISPLAY_ASPECT
    #error "platform must define GFX_DISPLAY_ASPECT in its PlatformConstants.h"
#endif
#ifndef GFX_DISPLAY_ASPECT_X
    #error "platform must define GFX_DISPLAY_ASPECT_X in its PlatformConstants.h"
#endif
#ifndef GFX_DISPLAY_ASPECT_Y
    #error "platform must define GFX_DISPLAY_ASPECT_Y in its PlatformConstants.h"
#endif
#ifndef PLATFORM_TARGET_FRAME_MICROS
    #error "platform must define PLATFORM_TARGET_FRAME_MICROS in its PlatformConstants.h"
#endif

// --- Interface -----------------------------------------------------------
#ifndef UI_MAX_QUADS
    #error "platform must define UI_MAX_QUADS in its PlatformConstants.h"
#endif
#ifndef UI_MAX_FOCUSABLES
    #error "platform must define UI_MAX_FOCUSABLES in its PlatformConstants.h"
#endif
#ifndef UI_METRIC_SCALE_PERCENT
    #error "platform must define UI_METRIC_SCALE_PERCENT in its PlatformConstants.h"
#endif

// --- Memory map ----------------------------------------------------------
#ifndef MEM_LIMIT_TOTAL_BUDGET
    #error "platform must define MEM_LIMIT_TOTAL_BUDGET in its PlatformConstants.h"
#endif
#ifndef MEM_BLOCK_CONFIG_SIZE
    #error "platform must define MEM_BLOCK_CONFIG_SIZE in its PlatformConstants.h"
#endif
#ifndef MEM_BLOCK_CONFIG_SLOTS
    #error "platform must define MEM_BLOCK_CONFIG_SLOTS in its PlatformConstants.h"
#endif
#ifndef MEM_BLOCK_LEVEL_DATA_SIZE
    #error "platform must define MEM_BLOCK_LEVEL_DATA_SIZE in its PlatformConstants.h"
#endif
#ifndef MEM_BLOCK_LEVEL_DATA_SLOTS
    #error "platform must define MEM_BLOCK_LEVEL_DATA_SLOTS in its PlatformConstants.h"
#endif
#ifndef MEM_BLOCK_RENDERER_SIZE
    #error "platform must define MEM_BLOCK_RENDERER_SIZE in its PlatformConstants.h"
#endif
#ifndef MEM_BLOCK_RENDERER_SLOTS
    #error "platform must define MEM_BLOCK_RENDERER_SLOTS in its PlatformConstants.h"
#endif
#ifndef MEM_ARENA_MAX_SLOTS
    #error "platform must define MEM_ARENA_MAX_SLOTS in its PlatformConstants.h"
#endif
#ifndef MEM_ARENA_SLOT_ALIGNMENT
    #error "platform must define MEM_ARENA_SLOT_ALIGNMENT in its PlatformConstants.h"
#endif
#ifndef MEM_POOL_MAIN_SIZE
    #error "platform must define MEM_POOL_MAIN_SIZE in its PlatformConstants.h"
#endif
#ifndef MEM_POOL_CHUNK_SIZE
    #error "platform must define MEM_POOL_CHUNK_SIZE in its PlatformConstants.h"
#endif

// --- Asynchronous IO -----------------------------------------------------
#ifndef IO_ASYNC_MAX_REQUESTS
    #error "platform must define IO_ASYNC_MAX_REQUESTS in its PlatformConstants.h"
#endif
#ifndef IO_READ_BUFFER_SIZE
    #error "platform must define IO_READ_BUFFER_SIZE in its PlatformConstants.h"
#endif
#ifndef IO_THREAD_SLEEP_USEC
    #error "platform must define IO_THREAD_SLEEP_USEC in its PlatformConstants.h"
#endif
#ifndef IO_DRAIN_MAX_SPINS
    #error "platform must define IO_DRAIN_MAX_SPINS in its PlatformConstants.h"
#endif
#ifndef IO_THREAD_STACK_SIZE
    #error "platform must define IO_THREAD_STACK_SIZE in its PlatformConstants.h"
#endif

// --- Logging and panic ---------------------------------------------------
#ifndef LOG_STRING_MAX_SIZE
    #error "platform must define LOG_STRING_MAX_SIZE in its PlatformConstants.h"
#endif
#ifndef PANIC_UI_PADDING
    #error "platform must define PANIC_UI_PADDING in its PlatformConstants.h"
#endif

// --- Input ---------------------------------------------------------------
#ifndef MAX_GAME_PAD_PORTS
    #error "platform must define MAX_GAME_PAD_PORTS in its PlatformConstants.h"
#endif
#ifndef INPUT_ANALOG_RAW_CENTER
    #error "platform must define INPUT_ANALOG_RAW_CENTER in its PlatformConstants.h"
#endif
#ifndef INPUT_ANALOG_RAW_SCALE
    #error "platform must define INPUT_ANALOG_RAW_SCALE in its PlatformConstants.h"
#endif
#ifndef INPUT_ANALOG_DEADZONE
    #error "platform must define INPUT_ANALOG_DEADZONE in its PlatformConstants.h"
#endif

// --- Archives and resources ----------------------------------------------
#ifndef ARCH_MAX_MOUNTED
    #error "platform must define ARCH_MAX_MOUNTED in its PlatformConstants.h"
#endif
#ifndef RES_MAX_ENTRIES
    #error "platform must define RES_MAX_ENTRIES in its PlatformConstants.h"
#endif
#ifndef SCENE_MAX_RESOURCES
    #error "platform must define SCENE_MAX_RESOURCES in its PlatformConstants.h"
#endif

// --- Level budgets -------------------------------------------------------
#ifndef LEVEL_CORE_SLOTS
    #error "platform must define LEVEL_CORE_SLOTS in its PlatformConstants.h"
#endif
#ifndef LEVEL_SECTOR_SLOT_BASE
    #error "platform must define LEVEL_SECTOR_SLOT_BASE in its PlatformConstants.h"
#endif
#ifndef LEVEL_RESIDENT_SECTORS
    #error "platform must define LEVEL_RESIDENT_SECTORS in its PlatformConstants.h"
#endif
#ifndef LEVEL_SECTOR_HYSTERESIS
    #error "platform must define LEVEL_SECTOR_HYSTERESIS in its PlatformConstants.h"
#endif
#ifndef LEVEL_FARFIELD_MAX_DRAWN
    #error "platform must define LEVEL_FARFIELD_MAX_DRAWN in its PlatformConstants.h"
#endif

// --- Graphics budgets ----------------------------------------------------
#ifndef GFX_MAX_DRAW_LIST_LENGTH
    #error "platform must define GFX_MAX_DRAW_LIST_LENGTH in its PlatformConstants.h"
#endif
#ifndef GFX_MAX_CAMERAS_3D
    #error "platform must define GFX_MAX_CAMERAS_3D in its PlatformConstants.h"
#endif
#ifndef GFX_NEAR_PLANE
    #error "platform must define GFX_NEAR_PLANE in its PlatformConstants.h"
#endif
#ifndef GFX_FAR_PLANE
    #error "platform must define GFX_FAR_PLANE in its PlatformConstants.h"
#endif
#ifndef GFX_MAX_TEXTURE_WIDTH
    #error "platform must define GFX_MAX_TEXTURE_WIDTH in its PlatformConstants.h"
#endif
#ifndef GFX_MAX_TEXTURE_HEIGHT
    #error "platform must define GFX_MAX_TEXTURE_HEIGHT in its PlatformConstants.h"
#endif
#ifndef GFX_MAX_MODEL_MESH_COUNT
    #error "platform must define GFX_MAX_MODEL_MESH_COUNT in its PlatformConstants.h"
#endif
#ifndef GFX_MAX_CACHED_MODELS
    #error "platform must define GFX_MAX_CACHED_MODELS in its PlatformConstants.h"
#endif

// --- Invariants ------------------------------------------------------------
// Relationships a platform can get wrong while defining every constant.

// A zero-length fixed array is not a valid table, and every one of these sizes a
// table the engine indexes.
static_assert(MEM_ARENA_MAX_SLOTS > 0, "MEM_ARENA_MAX_SLOTS must be positive");
static_assert(MAX_GAME_PAD_PORTS > 0, "MAX_GAME_PAD_PORTS must be positive - a platform with no pads still needs one slot");
static_assert(ARCH_MAX_MOUNTED > 0, "ARCH_MAX_MOUNTED must be positive");
static_assert(RES_MAX_ENTRIES > 0, "RES_MAX_ENTRIES must be positive");
static_assert(SCENE_MAX_RESOURCES > 0, "SCENE_MAX_RESOURCES must be positive");
static_assert(LOG_STRING_MAX_SIZE >= 64, "LOG_STRING_MAX_SIZE is too small to carry a useful message");
static_assert(UI_METRIC_SCALE_PERCENT > 0, "UI_METRIC_SCALE_PERCENT must be positive - a zero-scale interface draws nothing");

// Slot arithmetic divides by these.
static_assert(MEM_BLOCK_CONFIG_SLOTS > 0, "MEM_BLOCK_CONFIG_SLOTS must be positive");
static_assert(MEM_BLOCK_LEVEL_DATA_SLOTS > 0, "MEM_BLOCK_LEVEL_DATA_SLOTS must be positive");
static_assert(MEM_BLOCK_RENDERER_SLOTS > 0, "MEM_BLOCK_RENDERER_SLOTS must be positive");
static_assert(MEM_POOL_CHUNK_SIZE > 0, "MEM_POOL_CHUNK_SIZE must be positive");

// Every arena slot start is aligned by rounding down to this, so a non-power-of-two
// silently produces misaligned slots.
static_assert((MEM_ARENA_SLOT_ALIGNMENT & (MEM_ARENA_SLOT_ALIGNMENT - 1)) == 0, "MEM_ARENA_SLOT_ALIGNMENT must be a power of two");
static_assert(MEM_ARENA_SLOT_ALIGNMENT >= 16, "MEM_ARENA_SLOT_ALIGNMENT must be at least a quadword");

// A segment must hold at least one aligned slot, or slot capacity rounds to zero.
static_assert(MEM_BLOCK_CONFIG_SIZE / MEM_BLOCK_CONFIG_SLOTS >= MEM_ARENA_SLOT_ALIGNMENT, "config arena slots are smaller than the slot alignment");
static_assert(MEM_BLOCK_LEVEL_DATA_SIZE / MEM_BLOCK_LEVEL_DATA_SLOTS >= MEM_ARENA_SLOT_ALIGNMENT, "level-data arena slots are smaller than the slot alignment");
static_assert(MEM_BLOCK_RENDERER_SIZE / MEM_BLOCK_RENDERER_SLOTS >= MEM_ARENA_SLOT_ALIGNMENT, "renderer arena slots are smaller than the slot alignment");

// The whole map has to fit the budget the platform claims to enforce.
static_assert(MEM_BLOCK_CONFIG_SIZE + MEM_BLOCK_LEVEL_DATA_SIZE + MEM_BLOCK_RENDERER_SIZE + MEM_POOL_MAIN_SIZE <= MEM_LIMIT_TOTAL_BUDGET,
              "arenas plus pool exceed MEM_LIMIT_TOTAL_BUDGET - the reservation would be refused at startup");

// A compiled sector payload must fit one level-data slot, or it can never load.
static_assert(MEM_BLOCK_LEVEL_DATA_SIZE / MEM_BLOCK_LEVEL_DATA_SLOTS >= LEVEL_SECTOR_MAX_BYTES, "level-data slot is smaller than LEVEL_SECTOR_MAX_BYTES - a compiled sector could not be loaded");

// Core slots plus the resident ring must fit the segment, or sectors stream into
// slots that do not exist.
static_assert(LEVEL_SECTOR_SLOT_BASE >= LEVEL_CORE_SLOTS, "sector slots would overlap the resident level core");
static_assert(LEVEL_SECTOR_SLOT_BASE + LEVEL_RESIDENT_SECTORS <= MEM_BLOCK_LEVEL_DATA_SLOTS, "core slots plus the resident sector ring exceed MEM_BLOCK_LEVEL_DATA_SLOTS");
static_assert(MEM_BLOCK_LEVEL_DATA_SLOTS <= MEM_ARENA_MAX_SLOTS, "level-data segment declares more slots than MEM_ARENA_MAX_SLOTS allows");

// A read that cannot hold a sector cannot stream a level.
static_assert(IO_READ_BUFFER_SIZE >= LEVEL_SECTOR_MAX_BYTES, "IO_READ_BUFFER_SIZE is smaller than LEVEL_SECTOR_MAX_BYTES - sectors could never be read");
static_assert(IO_ASYNC_MAX_REQUESTS > 0, "IO_ASYNC_MAX_REQUESTS must be positive");

// Display and projection.
static_assert(GFX_SCREEN_WIDTH > 0 && GFX_SCREEN_HEIGHT > 0, "screen dimensions must be positive");
static_assert(GFX_DISPLAY_ASPECT_X > 0 && GFX_DISPLAY_ASPECT_Y > 0, "display aspect must be positive");
static_assert(PLATFORM_TARGET_FRAME_MICROS > 0, "PLATFORM_TARGET_FRAME_MICROS must be positive");
static_assert(GFX_MAX_CAMERAS_3D > 0, "GFX_MAX_CAMERAS_3D must be positive");
static_assert(GFX_MAX_DRAW_LIST_LENGTH > 0, "GFX_MAX_DRAW_LIST_LENGTH must be positive");
static_assert(GFX_MAX_TEXTURE_WIDTH > 0 && GFX_MAX_TEXTURE_HEIGHT > 0, "texture dimension caps must be positive");
