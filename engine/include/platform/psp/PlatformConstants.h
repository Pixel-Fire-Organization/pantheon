#pragma once

/*******************************/
/** MEMORY                    **/
/*******************************/

#define MEM_LIMIT_TOTAL_BUDGET (16 * 1024 * 1024)

#define MEM_BLOCK_CONFIG_SIZE (256 * 1024)
#define MEM_BLOCK_CONFIG_SLOTS 4

#define MEM_BLOCK_LEVEL_DATA_SIZE (6 * 1024 * 1024)
#define MEM_BLOCK_LEVEL_DATA_SLOTS 12

#define MEM_BLOCK_RENDERER_SIZE (512 * 1024)
#define MEM_BLOCK_RENDERER_SLOTS 1

#define MEM_ARENA_MAX_SLOTS 32
#define MEM_ARENA_SLOT_ALIGNMENT (16 * 1024)

#define MEM_POOL_MAIN_SIZE (1 * 1024 * 1024)
#define MEM_POOL_CHUNK_SIZE 256

/// Newlib heap reserved by Entry.cpp. It and the engine map are carved out of
/// the same user partition, so the two are sized together: this serves texture
/// storage, the graphics-engine buffers and small aligned allocations.
#define MEM_HEAP_SIZE_KB 6144

/// Granularity the system rounds a partition allocation up to.
#define MEM_SYSTEM_BLOCK_GRANULARITY 256

/*******************************/
/** ASYNC IO                  **/
/*******************************/

#define IO_ASYNC_MAX_REQUESTS 16
#define IO_THREAD_SLEEP_USEC 1000
#define IO_DRAIN_MAX_SPINS 10000
#define IO_THREAD_STACK_SIZE (32 * 1024)
#define IO_READ_BUFFER_SIZE (512 * 1024)

/*******************************/
/** THREADS                   **/
/*******************************/

// Lower is scheduled first. Workers sit above the main thread; see
// docs/psp/PLATFORM.md.
#define PLATFORM_MAIN_THREAD_PRIORITY 32
#define PLATFORM_WORKER_THREAD_PRIORITY 16

/*******************************/
/** LOGGING & PANIC           **/
/*******************************/

#define LOG_STRING_MAX_SIZE 256
#define PANIC_UI_PADDING 40

/*******************************/
/** INPUT                     **/
/*******************************/

#define MAX_GAME_PAD_PORTS 1

#define INPUT_ANALOG_RAW_CENTER 128
#define INPUT_ANALOG_RAW_SCALE 127.0f
#define INPUT_ANALOG_DEADZONE 0.25f

/*******************************/
/** ARCHIVES & RESOURCES      **/
/*******************************/

// Slot 0 is the always-resident master archive (RASSETS.PS2R); slot 1 is
// spare headroom, unused in normal play - see docs/subsystems/ARCHIVE.md.
#define ARCH_MAX_MOUNTED 2
#define RES_MAX_ENTRIES 64

// Resources one game::Scene may declare via GetResources().
#define SCENE_MAX_RESOURCES 8

/*******************************/
/** LEVEL BUDGETS             **/
/*******************************/

#define LEVEL_FARFIELD_MAX_DRAWN 256
#define LEVEL_SECTOR_HYSTERESIS 0.15f
#define LEVEL_RESIDENT_SECTORS 9
#define LEVEL_CORE_SLOTS 2
#define LEVEL_SECTOR_SLOT_BASE LEVEL_CORE_SLOTS

/*******************************/
/** GRAPHICS - DISPLAY        **/
/*******************************/

#define GFX_SCREEN_WIDTH 480
#define GFX_SCREEN_HEIGHT 272
#define GFX_SCREEN_REGION_STR "PSP"

#define GFX_DISPLAY_ASPECT (16.0f / 9.0f)
#define GFX_DISPLAY_ASPECT_X 16
#define GFX_DISPLAY_ASPECT_Y 9

#define PLATFORM_TARGET_FRAME_MICROS 16667

/// Framebuffer line stride in pixels, which the display hardware requires to be
/// a multiple of 64 rather than equal to the visible width.
#define GFX_PSP_BUFFER_STRIDE 512

/*******************************/
/** GRAPHICS - VIDEO MEMORY   **/
/*******************************/

#define GFX_PSP_VRAM_BYTES (2 * 1024 * 1024)
#define GFX_PSP_DISPLAY_BUFFERS 2

/// Bytes per pixel in the display buffers (32-bit colour) and in the depth
/// buffer (16-bit).
#define GFX_PSP_DISPLAY_BPP 4
#define GFX_PSP_DEPTH_BPP 2

/// Per-frame graphics-engine command list, double buffered.
#define GFX_PSP_DISPLAY_LIST_BYTES (128 * 1024)
#define GFX_PSP_DISPLAY_LIST_BUFFERS 2

/*******************************/
/** GRAPHICS - BUDGETS        **/
/*******************************/

#define GFX_MAX_TEXTURE_WIDTH 512
#define GFX_MAX_TEXTURE_HEIGHT 512
#define GFX_TEXTURE_BUDGET_BYTES (2 * 1024 * 1024)

#define GFX_MAX_DRAW_LIST_LENGTH 1024

// How large the interface draws on this display, as a percentage of the metrics
// the theme declares. The declaration is authored against the console reference
// framebuffer; a platform whose screen is materially smaller says so here rather
// than every theme carrying a second set of numbers.
#define UI_METRIC_SCALE_PERCENT 50

#define UI_MAX_QUADS 2048
#define UI_MAX_FOCUSABLES 96

// How deeply containers may nest their clip rectangles. Panel, scroll region,
// column, tree and modal is five; eight leaves headroom without being a budget
// anyone has to think about.
#define UI_MAX_CLIP_DEPTH 8

// Widgets that remember something between frames: scroll positions, open flags,
// repeat timers. Only a minority of widgets need one, so this sits well above
// the focusable count without being a budget anyone has to think about.
#define UI_MAX_STATES 128

// Content that must draw above the interface - a modal and its backdrop, a
// notification, the cursor. Its own buffer, so a modal cannot starve the screen
// underneath it, and appended at submission so draw order is still one list.
#define UI_MAX_OVERLAY_QUADS 512

// How many containers may open a navigation group, and how deep an identity
// scope may nest.
#define UI_MAX_FOCUS_GROUPS 8
#define UI_MAX_ID_DEPTH 8

// Queued notifications.
#define UI_MAX_TOASTS 4

// The longest formatted string a widget will build. Deliberately the same on
// every platform: a smaller console value would let a message fit on desktop and
// truncate on the console, which is the worst place to discover it.
#define UI_TEXT_MAX 192

// The most a Ui_TextInput/Ui_TextDialog field will ever hold open for editing
// at once -- bounds the snapshot the interface keeps to restore on cancel.
// Deliberately the same on every platform, for the reason UI_TEXT_MAX already
// carries: a value that fits on desktop and overflows on the console is the
// worst place to discover it.
#define UI_TEXT_INPUT_MAX 64

// Headroom above the interface budget in each backend's screen-space queue, for
// the game's own screen-space primitive and the panic display.
#define GFX_MAX_2D_EXTRA 256

// The depth buffer is 16-bit, and the other platforms' 0.1 near plane spends
// almost all of that range in the first few units. See docs/psp/PLATFORM.md.
#define GFX_NEAR_PLANE 1.0f
#define GFX_FAR_PLANE 1000.0f

#define GFX_MAX_CAMERAS_3D 4

#define GFX_MAX_MODEL_MESH_COUNT 32
#define GFX_MAX_CACHED_MODELS 64

/// Per-frame world-geometry vertex ceiling shared by both backends. The default
/// backend sizes its hardware vertex buffer from this, taken from the C heap;
/// the renderer arena holds only the built-in primitive geometry.
///
/// Whole entries are rejected once this fills, so a value set too low does not
/// degrade gracefully - it drops different objects from frame to frame as the
/// camera reorders them, which reads as the world flickering rather than as a
/// budget being hit.
#define GFX_PSP_MAX_FRAME_VERTICES 24576

/// Per-frame screen-space vertex ceiling. Derived from the interface budget
/// rather than guessed: six vertices per quad, for every quad the interface and
/// the game between them may submit. A smaller value silently truncates a busy
/// screen.
#define GFX_PSP_MAX_2D_VERTICES ((UI_MAX_QUADS + GFX_MAX_2D_EXTRA) * 6)
