# Platform — PlayStation Vita

A handheld console with a programmable graphics processor, a fixed framebuffer, a
policed memory budget, and two input surfaces no other platform here has: a front
touchscreen and a rear touchpad.

Build instructions are in [BUILD.md](BUILD.md); packaging and title metadata are
in [PACKAGING.md](PACKAGING.md); renderers are in [renderers/](renderers/).

## One family, two variants

Like the PlayStation 2, this is a **family**, not a single target. `vita` is the
handheld; `vitatv` is the set-top variant. The shared base is abstract and is
never selectable on its own, because the values that differ have to have exactly
one answer inside a built binary.

| | `vita` | `vitatv` |
|---|---|---|
| Gamepad ports | 1, built in | 4, external |
| Front touchscreen | Yes | No |
| Rear touchpad | Yes | No |
| Framebuffer | 960 x 544 | 960 x 544, scaled to the display |

The framebuffer is identical, so this split is **not** the PAL/NTSC case of a
differing resolution. It exists because the port count sizes a fixed array at
compile time, and because a capability must be answered honestly rather than
guessed from a runtime probe that a player could defeat by unplugging a pad.

Everything else — memory, storage, renderers, budgets — is shared.

## What is different in kind from the other platforms

- **The framebuffer is fixed, but pixels are not square.** The panel is 16:9 and
  the framebuffer is 960 x 544, which is 1.765:1. Projection must use the
  **display** aspect, not the framebuffer ratio, or everything renders subtly
  stretched. This is the console situation rather than the desktop one — but for a
  different reason than on the PlayStation 2, where the framebuffer ratio varies
  by region.
- **Engine memory does not come from the C heap.** The heap is itself a fixed
  pre-allocation carved out of the process main-memory allowance, so taking the
  arenas from it would double-count them against the same ceiling. The arenas and
  the pool are reserved directly from the system as their own blocks, and are
  released the same way. Small aligned allocations still come from the heap.
  Crossing the two corrupts one of them.
- **Storage is split by writability.** The application mount is read-only; a
  separate per-title data location is the only writable one. A platform that
  assumed one root served both, as the desktop does, would fail on first write.
- **The distribution carries title metadata.** No previous platform here needed a
  display name, an icon, a store-front layout or a trophy list. That metadata is
  declared, validated and packaged — see [PACKAGING.md](PACKAGING.md).

## Capabilities

| Capability | `vita` | `vitatv` | Note |
|---|---|---|---|
| Gamepad | Yes | Yes | One port built in; four external on the set-top |
| Keyboard | No | No | The system offers a text-entry dialog, not a key-level device |
| Mouse | No | No | |
| Analog triggers | No | No | Shoulder buttons are digital, and external-pad pressure is not exposed through the sampling buffer used here |
| Resizable window | No | No | Fixed framebuffer |
| Async IO | Yes | Yes | |
| File write | Yes | Yes | To the per-title data location only |
| Touch | Yes | No | Front surface and rear surface, reported separately |
| System dialog | Yes | Yes | `sceMsgDialog` and `sceImeDialog`, both via `sceCommonDialog` |
| Text characters | No | No | Typed text only ever arrives through the IME dialog above, not key by key |

The touch capability is the reason the two variants exist as separate binaries
rather than one binary with a runtime probe.

## Memory

The system divides a process allowance into several pools with different
properties. Only the first is the engine's concern; the rest are named because
mistaking one for another is the expensive error here.

| Pool | Allowance | Used for |
|---|---|---|
| Main | 256 MB, extendable | Code, heap, stack, engine arenas |
| Video | 112 MB | Render targets and textures |
| Physically contiguous | 26 MB | Transfer-sensitive buffers |
| Dialog | ~8 MB | System dialogs, not the application's to spend |

The extended main allowance is a **declared** property of the title, not a
runtime request; it is set at packaging time and is off by default here.

The engine map, inside the main allowance:

| | Size | Slots | Per slot |
|---|---|---|---|
| Engine budget | 128 MB | | A policy ceiling well inside the system allowance |
| Config arena | 1 MB | 4 | 256 KB |
| Level-data arena | 32 MB | 16 | 2 MB |
| Renderer arena | 16 MB | 1 | 16 MB |
| Main pool | 4 MB | | 256 B chunks |

Slot alignment stays at 16 KB, matching both other platforms, so slot arithmetic
behaves identically everywhere and a slot-boundary bug cannot hide on one
platform. It also exceeds the system block granularity, so a reserved block is
always usable as handed back.

The gap between the 53 MB the map actually spends and the 128 MB ceiling is not
slack — it is the heap, the stack and the graphics staging the renderer takes as
it is built. Memory is reserved **before** any renderer is constructed, so a
backend that cannot fit says so at construction rather than corrupting an arena.

## Storage and IO

| | |
|---|---|
| Max async read | 4 MB |
| Queued requests | 32 |
| Mounted archives | 4 |
| Resource handles | 256 |

**Two roots, with different rules.** Assets are addressed relative to the
application mount, which is read-only and contains exactly what was packaged.
Writes go to a per-title data location keyed by the title identifier, created on
first use. Attempting to write to the application mount fails; that is a
programming error, not a runtime condition to recover from.

Paths use forward slashes and a device prefix. The engine canonical asset keys
are uppercase and backslash-separated, so the platform translates on the way out,
exactly as the desktop platform does for its own convention.

## Input

The pad is sampled once per frame into the shared snapshot, in the wide analog
mode so both sticks report. Stick axes already arrive as bytes centred at 128,
which is the convention the engine uses throughout, so no conversion is needed —
the deadzone and range constants mean literally what they say here.

**The handheld has one shoulder row, and reports it on the trigger bits.** The
sampler puts the two physical shoulders where a fuller pad would report its
second row, so taken literally the handheld would answer that it has triggers
and no shoulders — the exact opposite of the hardware. The handheld variant
therefore reports them as the primary shoulders, and reports the second row and
the stick clicks as absent, because it has neither. The set-top variant is
driven by a wireless controller that really does have all of them and passes
its buttons through unchanged.

This is also why the two variants answer the debug combinations differently.
The handheld uses combinations built from the buttons it has; the set-top
variant uses the same ones as every other full pad. Neither is a preference:
a combination needing a second shoulder row cannot be pressed on a handheld
that does not have one. See [DEBUG.md](../subsystems/DEBUG.md).

| Intent | Handheld | Set-top |
| :--- | :--- | :--- |
| Performance snapshot | L1 + R1 + Select | L1 + L2 + R1 + R2 |
| Overlay toggle | L1 + R1 + Start | L1 + L2 + L3 + R3 |
| Debug menu | Select + Start | Select + Start |

The two variants agree on the last one only because both pads have those two
buttons. They still answer it separately rather than inheriting a family default,
which is what keeps the handheld free to differ the moment an intent needs a
button it does not have.

Button prompts draw PlayStation shapes on both variants — the handheld's own
face buttons and the set-top's wireless controller are both DualShock-family
hardware. See [subsystems/UI.md](../subsystems/UI.md).

**Touch is a device group of its own**, not a mouse. Two surfaces are reported
separately: the front screen, which is the one a player points at, and the rear
pad, which is behind the device and has no visible cursor. Neither is mapped onto
the mouse queries, because a platform must never emulate one device as another —
a caller that wants to know whether pointing is possible asks for the touch
capability.

The set-top variant reports no touch at all. It does not synthesise touch from a
pad, and content that needs pointing must check the capability rather than assume
the family provides it.

## Window

There is no window. The framebuffer is fixed at 960 x 544, the close query is
always negative, and the native handle is null. Shutdown is driven by the game
asking to exit or by the system suspending the title.

## Graphics

| | |
|---|---|
| Framebuffer | 960 x 544, fixed |
| Frame budget | 16667 us |
| Display aspect | 16:9 — **not** the framebuffer ratio; pixels are non-square |
| Draw list capacity | 4096 |
| Texture budget | 64 MB |
| Max texture | 1024 x 1024 |

## Renderers

| Backend | Role |
|---|---|
| [gxm](renderers/GXM.md) | **Default.** The native graphics API, driven directly |
| [vitagl](renderers/VITAGL.md) | A fixed-function subset layered over the same API |
| null | Final fallback; headless. See [RENDERER.md](../subsystems/RENDERER.md) |

Fallback order is gxm, then vitagl, then null.

The two differ in one way that matters to whoever installs the title: `gxm`
carries its shaders as binaries compiled at build time and needs **nothing** on
the player's console, while `vitagl` compiles its shaders at run time and needs
`libshacccg.suprx` extracted. That is why `gxm` is the default and why the build
requires an offline shader compiler rather than treating it as optional.

The relationship between the two mirrors the console pair on the PlayStation 2:
one backend drives the hardware directly and is the default, the other is a
library exposing an older and simpler drawing model, kept as a known-good
reference to compare against. They share no code.

### System dialogs are drawn into the title's own frame

A system dialog on this platform is not drawn over the running title by the
system. It is composited into the title's own back buffer, which means **every
renderer must hand each presented frame to the dialog service while a dialog is
open**, and the title must keep presenting frames for as long as one is.

Two failure modes follow, and neither reports itself:

- A title that opens a dialog and then waits for it **without presenting frames**
  waits forever. The dialog stays running, the screen holds the last frame, and
  the only symptom is a timeout somewhere unrelated — the dialog never says it
  was not serviced.
- A renderer that presents frames but does not hand them over draws the title
  correctly with **no dialog visible on it**, while the dialog is nonetheless
  open and consuming input.

This is why the flag saying a dialog is open is platform state rather than
something a caller passes: a renderer added later must observe it, and a caller
must not be able to forget to.

**`Dialog_Open`/`Dialog_Poll`/`Dialog_Cancel` are the non-blocking contract this
frame-servicing hazard requires**, and this platform is the reason that
contract is shaped the way it is rather than as an ordinary blocking call: a
message box or a confirmation opens `sceMsgDialog`, a text field opens
`sceImeDialog`, both through `sceCommonDialog`, and neither can be waited on
synchronously without hanging the title per the two failure modes above.
`Dialog_Poll` reports `Pending` for as long as `sceCommonDialogGetStatus`
does, and `Accepted`/`Cancelled` exactly once, the frame the dialog's own
result becomes available. Win32's `MessageBox`, which blocks the calling
thread and already knows its answer by the time `Dialog_Open` returns, is
still driven through this same poll — its first call simply reports what
already happened, rather than this platform's contract growing a second,
blocking shape for a host that does not need one. Typed text arrives only
through `sceImeDialog`'s own on-screen keyboard, converted between the
engine's ASCII and the dialog's UTF-16 at the platform boundary; characters
outside printable ASCII are dropped in that conversion, in both directions.

## Performance

The nine items [guidelines/PERFORMANCE.md](../guidelines/PERFORMANCE.md) asks
every platform to state.

**Budget and pacing.** 60 Hz, 16667 us. The default backend hands each frame
to a display queue that presents on the vertical blank and allows one frame to
be pending, so the processor may run one frame ahead of the display; handing
over a frame blocks while the previous one is still pending, and that block is
the present wait. The fallback backend swaps through its library, which waits
on the blank itself. Staging therefore overlaps drawing by one frame on the
default backend.

**The binding constraint.** The processor — the weakest in the family. Every
vertex of every visible object is transformed on it every frame, into a layout
that carries a normal the shader set never reads, and every resident sector is
transformed whether or not it is in view. Textures are uncompressed 32-bit
colour, which is spent from video memory and from bandwidth alike. Content
fits here by processor-side vertex count first.

**Per-frame ceilings.**

| | |
|---|---|
| Vertices per frame, default backend | 65536, world and screen space together, screen space reserved first |
| Draw list entries | 4096 |
| Interface quads | 16384, plus 2048 overlay quads |
| Draw runs | 1024 world, 256 screen-space |
| Staged vertices | grows on demand from the heap — see [backlog/performance_findings.md](../backlog/performance_findings.md) |
| Resident sectors | 9 |

**Clock.** The 64-bit microsecond process clock. It counts only while the
process runs, so a suspend does not appear as a giant delta. It does not wrap.

**Scheduling.** The IO worker is created at the default priority — the same as
the main thread — and the main thread is not lowered; the rule the
PlayStation 2 and the PlayStation Portable apply is not applied here. It works
because the main thread blocks in the display queue every frame, which hands
the processor to the worker; it would degrade silently if the frame loop ever
waited by spinning. Recorded as a finding.

**Cost of diagnostics.** Every log line is written to the debug output and
then, as three synchronous writes, to the log file on the memory card, at every
level. A diagnostic that fires every frame is the slowest thing in the frame;
the default backend's overflow report, emitted only when the shortfall changes,
is the pattern.

**What the snapshot measures here.** The default backend reports geometry
build, upload, present wait and buffer fill against the vertex ceiling — the
most complete accounting of any backend. The fallback reports present wait
only.

**Baseline.** Not yet measured. The next pass runs the boot scene and the
testbed's timing scenes on the handheld and records the split, the buffer fill
and the heap here, naming hardware or emulator.

**Left on the table**, in the order to attack it: the unread normal, a quarter
of every vertex's work and bytes; culling sectors and models before staging; a
compressed texture vocabulary in the cook list; the sky; asynchronous sector
recentring.

## Known limitations

- **The native trophy service is not integrated.** `GetAchievements()` returns
  null on this platform; unlocks are never mirrored into the console's own
  trophy application. This is a decision, not a gap in progress: the service
  requires software the player installs themselves and never reaches the
  online service even then, and the engine's own cross-platform achievement
  system (`game/config/achievements.json`) is what actually ships, unaffected by
  this. See [ACHIEVEMENT.md](../subsystems/ACHIEVEMENT.md) and
  [PACKAGING.md](PACKAGING.md)'s Trophies section.
- **Textures are cooked to plain 32-bit colour.** The hardware supports
  compressed formats that would cost far less video memory, but the cook list has
  no vocabulary for them yet, so the budget is spent uncompressed.
- **A failed renderer falls back silently.** The engine logs it and continues
  with the next backend, but a console shows neither the log nor the choice, so
  the only on-device symptom is a frame that looks wrong. Read
  `ux0:data/<TITLE_ID>/engine.log` to find out which backend actually started.
- **No sound and no fonts**, as on every platform.
- **Motion sensors, camera and microphone are not exposed.** The hardware has
  them; the platform contract has no vocabulary for them, and inventing one for a
  single platform would encode this platform's assumptions into it.
- Sector recentring is synchronous, as everywhere.
