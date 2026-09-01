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

/// Granularity the graphics driver maps memory in; a texture is charged at least this.
#define MEM_GPU_PAGE_SIZE (4 * 1024)

#define IO_ASYNC_MAX_REQUESTS 32
#define IO_THREAD_SLEEP_USEC 1000
#define IO_DRAIN_MAX_SPINS 10000
#define IO_THREAD_STACK_SIZE (64 * 1024)
#define IO_READ_BUFFER_SIZE (8 * 1024 * 1024)

/// Kernel priorities: lower runs first. The worker sits above the main thread.
#define PLATFORM_MAIN_THREAD_PRIORITY 0x2C
#define PLATFORM_WORKER_THREAD_PRIORITY 0x2B

#define LOG_STRING_MAX_SIZE 512
#define PANIC_UI_PADDING 40

#define MAX_GAME_PAD_PORTS 4

#define INPUT_ANALOG_RAW_CENTER 128
#define INPUT_ANALOG_RAW_SCALE 127.0f
#define INPUT_ANALOG_DEADZONE 0.25f

/// Full-scale magnitude of a stick axis as the controller service reports it.
#define INPUT_STICK_RAW_MAX 32767

#define INPUT_TOUCH_MAX_CONTACTS 10
#define INPUT_TOUCH_RAW_WIDTH 1280
#define INPUT_TOUCH_RAW_HEIGHT 720

#define ARCH_MAX_MOUNTED 4
#define RES_MAX_ENTRIES 256

/// Resources one game::Scene may declare via GetResources().
#define SCENE_MAX_RESOURCES 16

#define LEVEL_FARFIELD_MAX_DRAWN 1024
#define LEVEL_SECTOR_HYSTERESIS 0.15f
#define LEVEL_RESIDENT_SECTORS 9
#define LEVEL_CORE_SLOTS 2
#define LEVEL_SECTOR_SLOT_BASE LEVEL_CORE_SLOTS

/// The handheld framebuffer, which is also the reference size before the first frame.
#define GFX_SCREEN_WIDTH 1280
#define GFX_SCREEN_HEIGHT 720
#define GFX_SCREEN_REGION_STR "NX"

/// The docked framebuffer, and the size both renderers allocate their display buffers at.
#define GFX_NX_DOCKED_WIDTH 1920
#define GFX_NX_DOCKED_HEIGHT 1080

#define GFX_DISPLAY_ASPECT (16.0f / 9.0f)
#define GFX_DISPLAY_ASPECT_X 16
#define GFX_DISPLAY_ASPECT_Y 9

#define PLATFORM_TARGET_FRAME_MICROS 16667

#define GFX_MAX_TEXTURE_WIDTH 1024
#define GFX_MAX_TEXTURE_HEIGHT 1024
#define GFX_TEXTURE_BUDGET_BYTES (128 * 1024 * 1024)

#define GFX_MAX_DRAW_LIST_LENGTH 4096

#define UI_METRIC_SCALE_PERCENT 100

#define UI_MAX_QUADS 16384
#define UI_MAX_FOCUSABLES 256
#define UI_MAX_CLIP_DEPTH 8
#define UI_MAX_STATES 512
#define UI_MAX_OVERLAY_QUADS 2048
#define UI_MAX_FOCUS_GROUPS 8
#define UI_MAX_ID_DEPTH 8
#define UI_MAX_TOASTS 4
#define UI_TEXT_MAX 192
#define UI_TEXT_INPUT_MAX 64

/// Per-frame vertex ceiling shared by both renderers, world and screen space together.
#define GFX_NX_MAX_FRAME_VERTICES 262144

/// Display buffers attached to the system window by the default renderer.
#define GFX_NX_DISPLAY_BUFFERS 3

/// Frames whose vertex buffer and command memory the default renderer keeps apart, so the one the
/// graphics processor may still be reading is never rewritten.
#define GFX_NX_FRAME_SLICES 2

/// Command memory per frame slice in the default renderer.
#define GFX_NX_COMMAND_BYTES (512 * 1024)

/// Command memory the default renderer records texture transfers and offscreen renders into.
#define GFX_NX_TRANSFER_COMMAND_BYTES (64 * 1024)

#define GFX_NEAR_PLANE 0.1f
#define GFX_FAR_PLANE 1000.0f

#define GFX_MAX_CAMERAS_3D 4

#define GFX_MAX_MODEL_MESH_COUNT 32
#define GFX_MAX_CACHED_MODELS 128
