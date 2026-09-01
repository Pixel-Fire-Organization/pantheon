#pragma once

/*******************************/
/** MEMORY                    **/
/*******************************/

// A desktop budget, generous next to the console. Still enforced: the engine
// contract is that an over-budget load fails loudly rather than being silently
// swapped out, and that only means anything if there is a budget.
#define MEM_LIMIT_TOTAL_BUDGET (512 * 1024 * 1024) // 512 MB - a policy ceiling, not hardware

#define MEM_BLOCK_CONFIG_SIZE (1 * 1024 * 1024) // 1 MB
#define MEM_BLOCK_CONFIG_SLOTS 4

// Room for a larger resident ring than the console can hold. Slot capacity must
// still be at least LEVEL_SECTOR_MAX_BYTES; Memory.cpp static_asserts it.
#define MEM_BLOCK_LEVEL_DATA_SIZE (32 * 1024 * 1024) // 32 MB
#define MEM_BLOCK_LEVEL_DATA_SLOTS 16

#define MEM_BLOCK_RENDERER_SIZE (16 * 1024 * 1024) // 16 MB
#define MEM_BLOCK_RENDERER_SLOTS 1

#define MEM_ARENA_MAX_SLOTS 32

// Kept at 16 KB to match the PS2 so slot arithmetic behaves identically on both
// platforms; desktop has no DMA alignment requirement of its own.
#define MEM_ARENA_SLOT_ALIGNMENT (16 * 1024)

#define MEM_POOL_MAIN_SIZE (4 * 1024 * 1024) // 4 MB
#define MEM_POOL_CHUNK_SIZE 256

/*******************************/
/** ASYNC IO                  **/
/*******************************/

#define IO_ASYNC_MAX_REQUESTS 32
#define IO_THREAD_SLEEP_USEC 1000
#define IO_DRAIN_MAX_SPINS 10000
#define IO_THREAD_STACK_SIZE (64 * 1024)

// Larger than the console: no TLB coverage window to stay inside here.
#define IO_READ_BUFFER_SIZE (4 * 1024 * 1024) // 4 MB

/*******************************/
/** LOGGING & PANIC           **/
/*******************************/

#define LOG_STRING_MAX_SIZE 512

#define PANIC_UI_PADDING 40

/*******************************/
/** INPUT                     **/
/*******************************/

// XInput supports four controllers.
#define MAX_GAME_PAD_PORTS 4

// XInput already reports sticks as signed shorts, but the engine speaks the PS2
// byte convention, so the backend converts and these keep the same meaning.
#define INPUT_ANALOG_RAW_CENTER 128
#define INPUT_ANALOG_RAW_SCALE 127.0f
#define INPUT_ANALOG_DEADZONE 0.25f

/*******************************/
/** ARCHIVES & RESOURCES      **/
/*******************************/

#define ARCH_MAX_MOUNTED 4
#define RES_MAX_ENTRIES 256

// Resources one game::Scene may declare via GetResources().
#define SCENE_MAX_RESOURCES 16

/*******************************/
/** LEVEL BUDGETS             **/
/*******************************/

#define LEVEL_FARFIELD_MAX_DRAWN 1024
#define LEVEL_SECTOR_HYSTERESIS 0.15f
#define LEVEL_RESIDENT_SECTORS 9
#define LEVEL_CORE_SLOTS 2
#define LEVEL_SECTOR_SLOT_BASE LEVEL_CORE_SLOTS

/*******************************/
/** GRAPHICS                  **/
/*******************************/

// Default window size. Not a hardware constraint - the window is resizable, and
// renderers must query Platform::GetFramebufferSize() each frame rather than
// baking these in.
#define GFX_SCREEN_WIDTH 1280
#define GFX_SCREEN_HEIGHT 720
#define GFX_SCREEN_REGION_STR "WIN32"

// Square pixels, so the display aspect is simply the framebuffer aspect.
#define GFX_DISPLAY_ASPECT (16.0f / 9.0f)
#define GFX_DISPLAY_ASPECT_X 16
#define GFX_DISPLAY_ASPECT_Y 9

// 60 Hz.
#define PLATFORM_TARGET_FRAME_MICROS 16667

#define GFX_MAX_TEXTURE_WIDTH 4096
#define GFX_MAX_TEXTURE_HEIGHT 4096

// Texture budget in bytes. The PS2 expresses this as GS pages; on desktop the
// number is simply larger, and the same no-silent-eviction rule applies.
#define GFX_TEXTURE_BUDGET_BYTES (256 * 1024 * 1024) // 256 MB

#define GFX_MAX_DRAW_LIST_LENGTH 4096

// How large the interface draws on this display, as a percentage of the metrics
// the theme declares. The declaration is authored against the console reference
// framebuffer; a platform whose screen is materially smaller says so here rather
// than every theme carrying a second set of numbers.
#define UI_METRIC_SCALE_PERCENT 100

#define UI_MAX_QUADS 16384
#define UI_MAX_FOCUSABLES 256

// How deeply containers may nest their clip rectangles. Panel, scroll region,
// column, tree and modal is five; eight leaves headroom without being a budget
// anyone has to think about.
#define UI_MAX_CLIP_DEPTH 8

// Widgets that remember something between frames: scroll positions, open flags,
// repeat timers. Only a minority of widgets need one, so this sits well above
// the focusable count without being a budget anyone has to think about.
#define UI_MAX_STATES 512

// Content that must draw above the interface - a modal and its backdrop, a
// notification, the cursor. Its own buffer, so a modal cannot starve the screen
// underneath it, and appended at submission so draw order is still one list.
#define UI_MAX_OVERLAY_QUADS 2048

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

#define GFX_NEAR_PLANE 0.1f
#define GFX_FAR_PLANE 1000.0f

#define GFX_MAX_CAMERAS_3D 4

#define GFX_MAX_MODEL_MESH_COUNT 32
#define GFX_MAX_CACHED_MODELS 128
