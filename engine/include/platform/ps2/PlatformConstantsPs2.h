#pragma once

/*******************************/
/** MEMORY                    **/
/*******************************/

// Hard ceiling for engine-managed memory. The console has 32MB of EE RAM; the
// engine refuses to map more than this and panics rather than over-committing.
#define MEM_LIMIT_TOTAL_BUDGET (31 * 1024 * 1024) // 31 MB of the console's 32

#define MEM_BLOCK_CONFIG_SIZE (256 * 1024) // 256 KB
#define MEM_BLOCK_CONFIG_SLOTS 4

// 4 MB / 16 slots => 256 KB per 16KB-aligned slot. Slots 0-1 hold the resident
// level core (INFO/MATL/SGRD/ENTS/FARF); 2-10 the nine streamed sectors; 11-15
// prefetch/spare. One sector payload (LEVEL_SECTOR_MAX_BYTES) fits one slot -
// asserted at compile time in the platform's Memory.cpp.
#define MEM_BLOCK_LEVEL_DATA_SIZE (8 * 1024 * 1024) // 8 MB
#define MEM_BLOCK_LEVEL_DATA_SLOTS 16

#define MEM_BLOCK_RENDERER_SIZE (3 * 1024 * 1024) // 3 MB
#define MEM_BLOCK_RENDERER_SLOTS 1

#define MEM_ARENA_MAX_SLOTS 32

// Slots are 16KB aligned so every slot start is quadword-aligned for DMA/VIF.
#define MEM_ARENA_SLOT_ALIGNMENT (16 * 1024) // 16 KB

#define MEM_POOL_MAIN_SIZE (1 * 1024 * 1024) // 1 MB
#define MEM_POOL_CHUNK_SIZE 256 // bytes per chunk

/*******************************/
/** ASYNC IO                  **/
/*******************************/

#define IO_ASYNC_MAX_REQUESTS 16
#define IO_THREAD_SLEEP_USEC 1000

// Thread priorities, lowest number scheduled first. The kernel does not
// time-slice between different priorities, so a worker below the main thread
// only runs when the main thread happens to block - which a frame loop that
// spins on the display hardware almost never does. Workers therefore sit ABOVE
// the main thread: they are blocked on a semaphore or asleep nearly always, and
// preempt only to service work that has actually arrived.
#define PLATFORM_MAIN_THREAD_PRIORITY 48
#define PLATFORM_WORKER_THREAD_PRIORITY 24
#define IO_DRAIN_MAX_SPINS 10000
#define IO_THREAD_STACK_SIZE (32 * 1024)

// Maximum file size for a single async IO read. One shared buffer of this size
// is used rather than one per slot: per-slot buffers pushed .bss past the EE TLB
// coverage window and caused a store-TLB-miss cascade in the crt0 BSS clear.
#define IO_READ_BUFFER_SIZE (512 * 1024)

/*******************************/
/** LOGGING & PANIC           **/
/*******************************/

#define LOG_STRING_MAX_SIZE 256

#define PANIC_UI_PADDING 40

/*******************************/
/** INPUT                     **/
/*******************************/

#define MAX_GAME_PAD_PORTS 2

// PS2 analog sticks report a raw byte in [0, 255] with 128 as center. Values
// whose absolute magnitude falls below the deadzone are clamped to 0 so idle
// sticks that drift off-center (common on real hardware) produce no movement.
#define INPUT_ANALOG_RAW_CENTER 128
#define INPUT_ANALOG_RAW_SCALE 127.0f
#define INPUT_ANALOG_DEADZONE 0.25f

/*******************************/
/** ARCHIVES & RESOURCES      **/
/*******************************/

// Mounted archive slots. Slot 0 is the always-resident master archive
// (RASSETS.PS2R) - every rasset and every compiled level ships in it, and it
// is never unmounted or replaced for a level switch (see docs/subsystems/
// ARCHIVE.md, docs/subsystems/LEVEL.md). Slot 1 is spare headroom, not used in
// normal play; if something else mounts a second container, later (higher-
// index) mounts take lookup priority over slot 0 on a colliding key.
#define ARCH_MAX_MOUNTED 2

#define RES_MAX_ENTRIES 64

// Resources one game::Scene may declare via GetResources().
#define SCENE_MAX_RESOURCES 8

/*******************************/
/** LEVEL BUDGETS             **/
/*******************************/

// Max billboards drawn in a frame (far-field ring around the camera).
#define LEVEL_FARFIELD_MAX_DRAWN 256

// Sector residency: recenter the 3x3 ring only once the camera leaves the
// current cell by this fraction of a cell (hysteresis against boundary thrash).
#define LEVEL_SECTOR_HYSTERESIS 0.15f

// Resident sectors: a 3x3 ring around the camera cell.
#define LEVEL_RESIDENT_SECTORS 9

// ARENA_LEVEL_DATA slot assignment: slots [0..CORE_SLOTS) hold the resident
// level core; sectors stream into the slots after that.
#define LEVEL_CORE_SLOTS 2
#define LEVEL_SECTOR_SLOT_BASE LEVEL_CORE_SLOTS

/*******************************/
/** GRAPHICS - GS VRAM        **/
/*******************************/

// pixels per page (width / height) in GS for a PSMCT32 surface
#define GFX_GS_PAGE_WIDTH_PSM32 64
#define GFX_GS_PAGE_HEIGHT_PSM32 32

#define GFX_MAX_TEXTURE_GS_PAGES 64

// Total GS VRAM pages available for textures across all slots.
#define GFX_GS_TEXTURE_PAGE_BUDGET 264

#define GFX_MAX_TEXTURE_WIDTH 512
#define GFX_MAX_TEXTURE_HEIGHT 512

// Both region framebuffer sizes are defined here (identical across variants);
// the active GFX_SCREEN_WIDTH / GFX_SCREEN_HEIGHT are chosen by the variant.
#define GFX_SCREEN_PAL_WIDTH 640
#define GFX_SCREEN_PAL_HEIGHT 512
#define GFX_SCREEN_NTSC_WIDTH 640
#define GFX_SCREEN_NTSC_HEIGHT 448

// Projection aspect ratio. The PS2 outputs its framebuffer to a 4:3 display with
// NON-SQUARE pixels, so the perspective projection must use the DISPLAY aspect
// (4:3), not the framebuffer pixel ratio (640/512 = 1.25 would stretch geometry
// ~7% wide and differ between regions).
#define GFX_DISPLAY_ASPECT (4.0f / 3.0f)
#define GFX_DISPLAY_ASPECT_X 4
#define GFX_DISPLAY_ASPECT_Y 3

/*******************************/
/** GRAPHICS - DRAW BUDGETS   **/
/*******************************/

#define GFX_MAX_DRAW_LIST_LENGTH 1024

// How large the interface draws on this display, as a percentage of the metrics
// the theme declares. The declaration is authored against the console reference
// framebuffer; a platform whose screen is materially smaller says so here rather
// than every theme carrying a second set of numbers.
#define UI_METRIC_SCALE_PERCENT 100

#define UI_MAX_QUADS 4096
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

// Shared clip-plane distances - both backends must build the same frustum so
// CPU-side culling matches what the GS rasterizes.
#define GFX_NEAR_PLANE 0.1f
#define GFX_FAR_PLANE 1000.0f

// Fixed 3D camera slots. Exactly one is the active render camera per frame.
#define GFX_MAX_CAMERAS_3D 4

// RenderToImage3D's scratch offscreen target (Ui_Image3D), shared by both PS2
// backends: one square colour+depth surface, reserved once at construction
// like the main frame/depth buffers rather than negotiated per call, matching
// this platform's fixed-budget-decided-upfront convention. A caller's
// requested width/height only sets the camera's aspect ratio; the actual
// render always lands at this resolution; the interface stretches it to fit
// whatever screen rect it asked for regardless, exactly as it already does
// for an ordinary loaded texture of a different size. See
// docs/ps2/renderers/GIFTAG.md and docs/ps2/renderers/PS2GL.md.
#define GFX_PS2_IMAGE_TARGET_SIZE 128

// ps2gl CurPacket is 65,000 qwords shared by all render paths.
// See docs/ps2/renderers/PS2GL_FUNCTIONS.md.
#define GFX_DRAW_CALL_BUDGET 720 // combined limit - primitives + model meshes

// GIFTAG renderer (direct GS packet path) per-frame budgets. Unlike the ps2gl
// path, vertices are transformed on the EE and written straight into a GIF/DMA
// packet, so the limit is packet capacity, not draw calls. The two geometry
// packets are double-buffered: while the GS drains frame N, the EE builds frame
// N+1 into the other packet. The renderer drops excess geometry loudly (never a
// silent DMA overrun).
//   Textured strip vertex = ST+RGBAQ+XYZ2 = 3 regs = 1.5 qw; 61440 qw ~ 40k
//   textured verts, ~960 KB per packet. packet2 qword count is u16 (<= 65535).
#define GFX_GIFTAG_PACKET_QWORDS 61440 // per-packet DMA capacity (~960 KB)
#define GFX_GIFTAG_PACKET_BUFFERS 2 // double-buffered geometry packets
#define GFX_GIFTAG_MAX_VERTS 40000 // per-frame transformed-vertex cap (scratch bound)
#define GFX_GIFTAG_XFORM_BATCH 1024 // verts per VU0 batch transform
#define GFX_GIFTAG_PACKET_MARGIN_QW 64 // headroom left free per packet

// Centre of the GS primitive coordinate space. Vertex coordinates are written
// relative to this origin and the GS subtracts XYOFFSET from them to reach
// window coordinates, so centring here is what gives off-screen geometry a
// symmetric guard band instead of wrapping a 12.4 unsigned field.
#define GFX_GIFTAG_GS_ORIGIN 2048.0f

// Widest representable GS coordinate (12.4 fixed point, 4096 - 1/16).
#define GFX_GIFTAG_GS_COORD_MAX 4095.9375f

// Capacity of the texture-transfer packet. A GIF image transfer carries at most
// GIF_BLOCK_SIZE (0x7FFF) qwords per block, so the largest supported texture
// (GFX_MAX_TEXTURE_WIDTH x GFX_MAX_TEXTURE_HEIGHT at 4 bytes) needs three
// blocks: six qwords of setup plus three per block, plus the cache flush.
#define GFX_GIFTAG_ENV_PACKET_QWORDS 64

// RenderToImage3D's own packet, built and sent independently of the frame's
// m_geom -- it must complete and drain entirely before the call returns, so
// it cannot share the frame's own double-buffered packets. One preview
// object is a few thousand vertices at most; this is a fraction of
// GFX_GIFTAG_PACKET_QWORDS, which is sized for a whole frame.
#define GFX_GIFTAG_IMAGE_PACKET_QWORDS 4096

// Register writes a texture binding may add to the geometry packet: sampling,
// texture buffer + colour table, and the mipmap addresses. Counted against
// packet capacity before an object is emitted.
#define GFX_GIFTAG_TEXBIND_QW 12

// Packet cost of one screen-space quad (a textured sprite: GIF tag plus three
// registers per corner), and the fixed allowance for the pass around them -
// its texture bindings and the pixel-test bracket. The interface's cost is
// reserved before world geometry is built, so a heavy world yields to it
// instead of consuming the packet and leaving the interface undrawable.
// Every batch names its primitive in its own register write rather than in the
// transfer tag, which costs one tag plus one register per batch.
#define GFX_GIFTAG_PRIM_QW 2

#define GFX_GIFTAG_QUAD2D_QW 4
#define GFX_GIFTAG_2D_RESERVE_QW 64

// glDrawElements is a hard mError() in ps2gl; indexed meshes are skipped.
#define GFX_MAX_MODEL_MESH_COUNT 8 // max meshes per model in DList cache
#define GFX_MAX_CACHED_MODELS 16 // max unique models resident at once
