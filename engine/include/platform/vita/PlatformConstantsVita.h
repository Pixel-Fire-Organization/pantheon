#pragma once

#define MEM_LIMIT_TOTAL_BUDGET (128 * 1024 * 1024)

#define MEM_BLOCK_CONFIG_SIZE (1 * 1024 * 1024)
#define MEM_BLOCK_CONFIG_SLOTS 4

#define MEM_BLOCK_LEVEL_DATA_SIZE (32 * 1024 * 1024)
#define MEM_BLOCK_LEVEL_DATA_SLOTS 16

#define MEM_BLOCK_RENDERER_SIZE (16 * 1024 * 1024)
#define MEM_BLOCK_RENDERER_SLOTS 1

#define MEM_ARENA_MAX_SLOTS 32
#define MEM_ARENA_SLOT_ALIGNMENT (16 * 1024)

#define MEM_POOL_MAIN_SIZE (4 * 1024 * 1024)
#define MEM_POOL_CHUNK_SIZE 256

#define MEM_HEAP_SIZE (32 * 1024 * 1024)
#define MEM_SYSTEM_BLOCK_GRANULARITY (4 * 1024)

#define IO_ASYNC_MAX_REQUESTS 32
#define IO_THREAD_SLEEP_USEC 1000
#define IO_DRAIN_MAX_SPINS 10000
#define IO_THREAD_STACK_SIZE (64 * 1024)
#define IO_READ_BUFFER_SIZE (4 * 1024 * 1024)

#define THREAD_DEFAULT_PRIORITY 0x10000100
#define THREAD_DEFAULT_AFFINITY 0

#define LOG_STRING_MAX_SIZE 512
#define PANIC_UI_PADDING 40

#define INPUT_ANALOG_RAW_CENTER 128
#define INPUT_ANALOG_RAW_SCALE 127.0f
#define INPUT_ANALOG_DEADZONE 0.25f

#define INPUT_TOUCH_MAX_CONTACTS 6
#define INPUT_TOUCH_FRONT_RAW_WIDTH 1920
#define INPUT_TOUCH_FRONT_RAW_HEIGHT 1088
#define INPUT_TOUCH_REAR_RAW_WIDTH 1920
#define INPUT_TOUCH_REAR_RAW_HEIGHT 890

#define ARCH_MAX_MOUNTED 4
#define RES_MAX_ENTRIES 256

// Resources one game::Scene may declare via GetResources().
#define SCENE_MAX_RESOURCES 16

#define LEVEL_FARFIELD_MAX_DRAWN 1024
#define LEVEL_SECTOR_HYSTERESIS 0.15f
#define LEVEL_RESIDENT_SECTORS 9
#define LEVEL_CORE_SLOTS 2
#define LEVEL_SECTOR_SLOT_BASE LEVEL_CORE_SLOTS

#define GFX_SCREEN_WIDTH 960
#define GFX_SCREEN_HEIGHT 544

#define GFX_DISPLAY_ASPECT (16.0f / 9.0f)
#define GFX_DISPLAY_ASPECT_X 16
#define GFX_DISPLAY_ASPECT_Y 9

#define PLATFORM_TARGET_FRAME_MICROS 16667

#define GFX_MAX_TEXTURE_WIDTH 1024
#define GFX_MAX_TEXTURE_HEIGHT 1024
#define GFX_TEXTURE_BUDGET_BYTES (64 * 1024 * 1024)

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
#define GFX_GXM_MAX_FRAME_VERTICES 65536
#define GFX_GXM_DISPLAY_BUFFERS 2

#define GFX_NEAR_PLANE 0.1f
#define GFX_FAR_PLANE 1000.0f

#define GFX_MAX_CAMERAS_3D 4

#define GFX_MAX_MODEL_MESH_COUNT 32
#define GFX_MAX_CACHED_MODELS 128
