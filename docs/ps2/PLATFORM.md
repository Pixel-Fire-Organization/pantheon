# Platform — PlayStation 2

Two selectable platforms, `ps2pal` and `ps2ntsc`, sharing one abstract PS2 base.
The base is never selectable on its own: every value that differs by broadcast
region has exactly one correct answer in a given build, and shipping a binary
that could be either would mean carrying a wrong answer.

Build instructions are in [BUILD.md](BUILD.md); renderers are in
[renderers/](renderers/).

## Variants

| | `ps2pal` | `ps2ntsc` |
|---|---|---|
| Framebuffer | 640 x 512 | 640 x 448 |
| Refresh | 50 Hz | ~60 Hz |
| Frame budget | 20000 us | 16667 us |

Everything else is identical. Each variant is a separate binary and a separate
distribution, so a disc image cannot boot in the wrong video mode.

**Framebuffer dimensions are compile-time, not runtime.** The direct-packet
renderer multiplies by them once per transformed vertex; resolving that through a
call per vertex is not affordable on this hardware. This is the one deliberate
exception to keeping variant differences runtime-only, and it is safe precisely
because the variants ship separately.

**Pixels are not square.** The console outputs a 4:3 display regardless of
framebuffer height, so the projection uses the display aspect, not the
framebuffer ratio. Using the framebuffer ratio stretches geometry noticeably and
differently per region.

## Capabilities

| Capability | Available | Note |
|---|---|---|
| Gamepad | Yes | Two ports |
| Keyboard | No | Queries report neutral |
| Mouse | No | Queries report neutral |
| Analog triggers | No | The pad reports shoulder pressure; the engine does not surface it |
| Resizable window | No | Framebuffer is fixed at build time |
| Async IO | Yes | |
| File write | Yes | To a memory card. The boot device is read-only |
| System dialog | No | No host dialog service; the UI subsystem's drawn modal covers dialogs and text entry here |
| Text characters | No | No key-level device to drive one from |

## Memory

| | Size | Slots | Per slot |
|---|---|---|---|
| Engine ceiling | 31 MB | | Of 32 MB total; the engine refuses to map more and panics rather than over-committing |
| Config arena | 256 KB | 4 | 64 KB |
| Level-data arena | 4 MB | 16 | 256 KB |
| Renderer arena | 3 MB | 1 | 3 MB |
| Main pool | 1 MB | | 256 B chunks |

Slot alignment is 16 KB, so every slot start is quadword-aligned and the DMA and
vector-unit transfer paths can consume a slot where it lies.

Level-data slots are assigned: the first two hold the resident level core, the
next nine the streamed sector ring, the remainder prefetch and spare. One sector
payload fits one slot, and that relationship is asserted when the engine is
built rather than checked on the console.

## Storage and IO

| | |
|---|---|
| Max async read | 512 KB — one shared buffer, not one per request |
| Queued requests | 16 |
| Mounted archives | 2: the master archive (every rasset and every level), and one spare slot |
| Resource handles | 64 |

The single shared read buffer is a hardware requirement, not a simplification. A
per-request buffer design multiplies that size by the queue depth and pushes
static storage past the address range the memory management unit covers at
startup, faulting during the C runtime zeroing pass before any engine code runs.

Device paths carry a device token and a version suffix; the canonical key rule in
[ARCHIVE_FORMAT.md](../formats/ARCHIVE_FORMAT.md) strips both. The active device
is taken from the launch arguments, so the same binary runs from disc, from a
host filesystem during development, and from mass storage.

**Writes go to a memory card, and there may not be one.** The per-title location
is a directory named for the title on the first card that has one, checked in
slot order. The card library is brought up on first use rather than at startup,
so a title that never writes never loads the modules.

This is the only platform here where writable storage is **removable, absent on a
perfectly healthy console, and full at sizes a desktop would call empty**. A
console with no card is therefore a normal state and not a fault: anything the
engine would have persisted is kept for the session and lost at power-off, and
the subsystem that wanted it says so once. Treating a missing card as an error
would make one a requirement for playing rather than for saving.

An unformatted card counts as no card. The engine does not offer to format one:
that is a decision about the player's other saves, not ours to take.

**A save the console cannot browse looks broken to the player.** The card browser
does not read a save's data; it reads a descriptor naming an icon model, and
draws that. A directory carrying neither is reported as *corrupted data* even
when every byte the engine wrote is intact and reads back perfectly — the player
cannot see what it is, and cannot delete it to reclaim the space. The engine
therefore writes both the first time it creates the directory, and only then, so
the cost is one failed open per boot rather than a rewrite.

Both are generated from the title declaration and compiled in, for two reasons:
the name shown on the card is then the name the title declares and cannot drift
from it, and writing them needs nothing from the disc, so it works whichever
device the title was launched from.

## Input

Two pad ports. Analog sticks report a raw byte centred at 128; magnitudes below a
quarter of full deflection are clamped to zero, because worn hardware rests
off-centre and would otherwise drift constantly.

No keyboard and no mouse: those queries report neutral rather than being
synthesised from the pad.

The pad has both shoulder rows and both stick clicks, so this platform answers
the debug combinations with the full-pad set. See
[DEBUG.md](../subsystems/DEBUG.md).

| Intent | Combination |
| :--- | :--- |
| Performance snapshot | L1 + L2 + R1 + R2 |
| Overlay toggle | L1 + L2 + L3 + R3 |
| Debug menu | Select + Start |

Button prompts draw PlayStation shapes — this platform's pad is a DualShock 2,
and the interface's icon set draws accordingly. See
[subsystems/UI.md](../subsystems/UI.md).

**Text entry has no platform mechanism at all on this console** — neither
`PlatformCapability::SystemDialog` nor `PlatformCapability::TextCharacters` is
present — so the UI subsystem's own drawn modal is the only path: a message
box built from `Ui_BeginModal`, and an on-screen keyboard driven entirely by
the pad through the same row/run navigation a menu bar already uses. This is
the floor every platform can fall back to, not a console-specific feature, but
it is the *only* mechanism this console ever reaches.

## Threads and synchronisation

Threads and semaphores come from the console kernel. Two of its behaviours are
load-bearing and easy to undo by accident:

- **A thread descriptor must be zeroed before use.** The kernel's behaviour on
  thread creation is undefined for attribute bits it does not recognise, and
  stack garbage in those fields corrupts the thread table in a way that surfaces
  much later, inside an unrelated interrupt handler — typically the pad driver's
  transfer handler. The symptom appears nowhere near the cause. Zeroing the whole
  descriptor is required; a partial assignment or a designated initialiser that
  leaves fields untouched is not equivalent.
- **Semaphore ordering around the shared read buffer** is what keeps the IO
  worker from overwriting bytes a callback still holds. See
  [IO.md](../subsystems/IO.md).

## Graphics budgets

| | |
|---|---|
| Texture budget | 264 video memory pages |
| Level texture allowance | 200 pages, leaving headroom for game and UI textures |
| Max texture | 512 x 512 |
| Draw list capacity | 1024 |
| Camera slots | 4 |
| Near / far plane | 0.1 / 1000.0 |

Texture cost is accounted in pages here and converted to bytes at the engine
boundary, so the budget comparison means the same thing as on other platforms.

## Renderers

| Backend | Role |
|---|---|
| [giftag](renderers/GIFTAG.md) | **Default.** Builds display packets directly |
| [ps2gl](renderers/PS2GL.md) | Vector-unit microcode path through a GL-1.1-subset library |
| null | Final fallback; headless. See [RENDERER.md](../subsystems/RENDERER.md) |

Fallback order is giftag, then ps2gl, then null.

**The two are unrelated backends that happen to share a heritage of naming.** One
is a library implementing a subset of a graphics API on top of vector microcode;
the other writes hardware display packets directly. They share no code and no
base class beyond the renderer contract, and their limits differ — see each spec.

## Threads

The kernel schedules strictly by priority and does **not** time-slice between
different ones: a thread only yields when it blocks, sleeps, or is preempted by
something more urgent. A worker placed below the main thread therefore runs only
when the main thread happens to block, and this platform's frame loop waits on
the display hardware by spinning, so it can go a whole second without blocking at
all.

Workers consequently run **above** the main thread, which is lowered from the
priority the loader gives it during platform start-up. This is safe because the
engine's workers are blocked or asleep almost always and preempt only to service
work that has arrived; it is not an invitation to add a worker that spins.

Getting this wrong does not fail, it only goes slow, and the slowness looks like
a defect somewhere else entirely — an asset path that appears to stall, or a
renderer that appears not to sample its textures.

## Performance

The nine items [guidelines/PERFORMANCE.md](../guidelines/PERFORMANCE.md) asks
every platform to state.

**Budget and pacing.** 50 Hz and 20000 us on `ps2pal`, about 60 Hz and
16667 us on `ps2ntsc`. The processor builds the next frame's display packet
while the hardware drains the previous one. At the end of a frame the direct
backend waits, in order, for the previous packet's transfer to complete, for
the hardware to finish drawing it, and for the vertical blank, then sends the
packet it has just built; the library backend waits for the previous frame's
completion signal and then the blank, then swaps and dispatches. Both wait by
spinning, which is why workers sit above the main thread (see Threads). The
processor therefore overlaps the hardware by one frame, and the frame drops a
whole refresh the moment building plus waiting exceeds the budget.

**The binding constraint.** On the direct backend, packet capacity and the
transformed-vertex ceiling: a textured vertex costs one and a half quadwords,
so the packet holds roughly forty thousand of them, transformed in batches by
the vector coprocessor — guarded by a start-up self-test, which now passes
under emulation; see [backlog/performance_findings.md](../backlog/performance_findings.md)
for what it checks. On the library backend, draw calls. On both, fill rate is
spent twice on world geometry because nothing rejects back faces, and texture
memory on the direct backend is roughly an eighth of the library figure.
Content fits here by vertex count first and by texture pages second.

**Per-frame ceilings.**

| | Direct backend | Library backend |
|---|---|---|
| Transformed vertices | 40000 | — |
| Packet | 61440 quadwords, two buffers | 65000 quadwords, shared |
| Draw calls | — | 720 |
| Draw list entries | 1024 | 1024 |
| Interface quads | 4096, plus 512 overlay quads | same |
| Screen-space queue | the interface budget plus 256 | same |
| Resident sectors | 9 | 9 |

**Clock.** A 32-bit microsecond counter that wraps after about 72 minutes; the
platform absorbs the wrap so callers see monotonic seconds. Microsecond
resolution.

**Scheduling.** The kernel does not time-slice across priorities. The main
thread is lowered at start-up and every worker is created above it; see
Threads for why. The IO worker polls the request table once a millisecond when
idle, which costs roughly twenty wake-ups a frame above the main thread.

**Cost of diagnostics.** Every log line crosses from the main processor to the
IO processor and out over the development link; the platform mutes debug-level
lines for that reason. A diagnostic that fires every frame costs more than the
frame it describes, and its cost lands in whichever phase contains it.

**What the snapshot measures here.** Both backends report the present wait —
the transfer, the hardware finish and the blank together. The direct backend
reports packet fill against capacity. Neither reports geometry build or upload
separately, so those read as not measured.

**Baseline.** 2026-09-16, `ps2pal`, direct backend, the game's boot scene (the
main menu), **under emulation** with the emulator's software renderer: holds
50.0 frames per second for the whole run; heap 15466 KB of the 31 MB budget,
constant. Emulation holds the refresh whatever the frame costs, so this
establishes pacing and memory, not headroom. No figure from hardware yet. This
baseline predates the vector-unit batch path being enabled (see below) and
should be retaken now that transform work has moved off the main processor.

**Left on the table**, in the order to attack it: back-face rejection on world
geometry, which needs correctly wound content; far-field impostors, which the
level format budgets for and nothing draws; asynchronous sector recentring.
The vector-unit batch transform itself is no longer on this list — its
self-test now passes under emulation (see
[backlog/performance_findings.md](../backlog/performance_findings.md)) — but
whether it also passes, and actually helps, on hardware is still unmeasured.

## Known limitations

- **Texture memory differs per backend.** The page budget below is what the
  ps2gl buffer layout leaves. The default backend renders at full height in 32
  bits and has far less, so it reports its own ceiling and the resource manager
  enforces that instead.
- **Far-field geometry is not drawn by any backend.** The level format
  describes it and budgets for it; nothing renders it yet.
- Sound and font assets are unimplemented, as on every platform.
- Sector recentring is synchronous and hitches on optical media.
