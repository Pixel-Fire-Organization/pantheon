# Platform — PlayStation Portable

A handheld console with a programmable graphics processor, a fixed framebuffer,
one analog stick, and the tightest memory ceiling of any platform here — tighter
than the PlayStation 2's, on hardware whose graphics processor is considerably
more capable. That combination is what makes this target awkward: nothing else in
the engine has to be this careful about memory while having this much drawing
power available.

Build instructions are in [BUILD.md](BUILD.md); packaging and the two
distribution containers are in [PACKAGING.md](PACKAGING.md); renderers are in
[renderers/](renderers/).

## One platform, not a family

Unlike the PlayStation 2 and the Vita, this is a **single selectable platform**.
The later hardware models add a second memory region, but nothing else differs —
screen, graphics processor, pad and storage are identical across every model —
and a memory ceiling is not a reason to ship two binaries when the smaller one
runs correctly on all of them.

So `psp` is built for the original model's allowance and runs everywhere. The
packaged metadata still requests the larger allowance, which is harmless on the
original hardware and correct on the later models; the engine simply never spends
it. A variant that did spend it would be a family whose only differing value is a
budget, which is the case the
[platform guideline](../guidelines/NEW_PLATFORM.md) asks to think twice about.

## What is different in kind from the other platforms

- **The advertised memory is not the available memory.** The console has 32 MB,
  and a title cannot reach it. Main memory is split into a kernel partition and a
  **24 MB user partition**, and the loaded executable, its zero-initialised data
  and every thread stack come out of that 24 MB before the engine asks for
  anything. See [Memory](#memory) — the most important section of this document,
  and the one most easily got wrong by measuring on an emulator.
- **The two renderers are left with very different amounts of texture room**, and
  the difference is not marginal. One sources textures from main memory against
  the platform figure; the other stores them in the 2 MB of video memory its
  library manages. A ceiling derived from either is simply wrong for the other,
  which is exactly the case [RENDERER.md](../subsystems/RENDERER.md) describes.
- **The graphics processor reads palettised textures directly.** It is handed a
  colour table and an index per pixel, with no expansion step. On a machine this
  size that is worth four times the memory, so this platform cooks its textures
  the way the PlayStation 2 does — honouring each asset's own declared encoding
  rather than flattening everything to 32-bit colour the way every other non-PS2
  platform here does. The saving comes with an obligation: the cooked alpha
  convention is rescaled on expansion everywhere else, so a backend that skips
  expansion has to rescale deliberately. See [renderers/GU.md](renderers/GU.md).
- **The scheduler does not time-slice between priorities.** A lower-priority
  worker runs only when the main thread happens to block, which a frame loop
  spinning on display hardware may not do for a very long time. Workers therefore
  sit **above** the main thread. This is the same hazard the PlayStation 2
  documents, with the same silent symptom — reads still complete, just orders of
  magnitude slower than the medium, which reads as a stall somewhere else
  entirely.
- **There is a real quit signal.** Alone among the consoles here, the system asks
  the title to exit: the player presses a hardware button and the system delivers
  a callback. The close query therefore means something on this platform, rather
  than being the permanently-negative stub the other two consoles implement.
- **One binary boots from two different containers.** The same executable runs
  from a disc image and from a memory card, and resolves its asset root from
  whichever it was started from. No other platform here ships two containers.

## Capabilities

| Capability | Available | Note |
|---|---|---|
| Gamepad | Yes | One port, built in |
| Keyboard | No | The system offers an on-screen keyboard dialog, not a key-level device |
| Mouse | No | |
| Analog triggers | No | The two shoulders are digital switches; the trigger queries report fully released or fully pressed |
| Resizable window | No | Fixed framebuffer |
| Async IO | Yes | |
| File write | Yes | To the memory card only — never to the disc |
| Touch | No | |
| System dialog | Yes | The system message dialog and the on-screen keyboard |
| Text characters | No | Typed text only ever arrives through the dialog above, not key by key |

Button prompts draw PlayStation shapes: the face buttons are the same four this
family has always used.

## Memory

**The console's 32 MB is not a budget a title can spend.** Main memory is
partitioned, and a user-mode title sees only the user partition:

| Region | Size | Whose |
|---|---|---|
| Kernel partition | 8 MB | The system's |
| **User partition** | **24 MB** | The title's — and everything the title has comes out of this |
| Extended region | A further 32 MB | Later hardware models only; requested by declared metadata |

Out of that 24 MB, the executable image, its zero-initialised data and every
thread stack are spent before the engine reserves anything. What is actually free
to a running title is roughly **20 to 22 MB**.

**This is the trap on this platform, and it is an emulator-shaped trap.** The
common emulator defaults to modelling a later hardware model with the extended
region present, and reports free memory well above thirty megabytes. A budget
tuned to that figure runs perfectly in the emulator and fails to reserve on
original hardware. Set the emulator to the original model and its 32 MB before
believing any memory figure it prints — see [BUILD.md](BUILD.md).

The engine map, inside the user partition:

| | Size | Slots | Per slot |
|---|---|---|---|
| Engine budget | 16 MB | | A ceiling inside the 24 MB partition, covering the map and everything taken from the heap below |
| Config arena | 256 KB | 4 | 64 KB |
| Level-data arena | 6 MB | 12 | 512 KB — exactly one compiled sector |
| Renderer arena | 512 KB | 1 | 512 KB |
| Main pool | 1 MB | | 256 B chunks |
| C heap | 6 MB | | Textures, the graphics-engine buffers, small aligned allocations |

The map is 7.75 MB and the heap is 6 MB, so a running title holds about 13.75 MB
of the partition, leaving the executable, the stacks and several megabytes of
slack inside the ~21 MB a real console actually offers.

**The map and the C heap are sized together, because they come from the same
place.** The heap is carved out of the same user partition the map is reserved
from, so they are not independent budgets: growing one shrinks what the other can
have. That is why the level-data arena is 6 MB here where the PlayStation 2
spends 8 MB with the same 512 KB slots — the twelve slots still hold the two core
slots and the nine resident sectors with one spare, and the 2 MB that buys goes to
the heap, which is where textures live on this platform.

**The map itself is reserved from the system partition, not from the C heap.**
Taking 7 MB of arenas out of a heap that was itself carved from the partition
would count that memory twice against one ceiling and leave the engine convinced
it had room it did not have. The arenas and the pool are reserved as their own
partition block and released the same way; the heap serves the aligned
allocations. Crossing the two corrupts one of them.

Memory is reserved **before** any renderer is constructed, so a backend that
cannot fit says so at construction rather than corrupting an arena.

Slot alignment stays at 16 KB, matching every other platform, so slot arithmetic
behaves identically everywhere. It is also a whole multiple of the processor's
cache line, so a slot handed to the graphics processor or to a hardware copy
never shares a line with its neighbour.

Heap statistics report both the total free memory and the largest single free
block. The gap between those two is the fragmentation, and on a machine this size
it is the statistic that actually matters — a reservation can fail with several
megabytes free.

## Storage and IO

| | |
|---|---|
| Max async read | 512 KB |
| Queued requests | 16 |
| Mounted archives | 2: the master archive (every rasset and every level), and one spare slot |
| Resource handles | 64 |

**Three roots, resolved at startup.** The same binary ships in two containers and
is also run over a development link, so the asset root is discovered rather than
assumed:

| Root | When |
|---|---|
| The disc's user directory | Started from a disc image |
| The title's own directory on the memory card | Started from a memory card, derived from the path the title was launched with |
| The development host | Started over the development link |

Writes go to the memory card directory and nowhere else. A title started from a
disc with no memory card present has nowhere to write, and the writable-path
query answers negatively — an ordinary state on a console with removable storage,
not a fault to recover from. The disc is read-only; attempting to write to it is
a programming error.

Paths use forward slashes and a device prefix. Engine canonical asset keys are
uppercase and backslash-separated, so the platform translates on the way out, as
every other platform does for its own convention.

## Input

The pad is sampled once per frame into the shared snapshot. The stick must be put
into its analog sampling mode explicitly at startup; without that it reports a
**constant centred value forever**, which presents as a dead stick rather than as
a missing call, and nothing reports an error.

Stick axes arrive as bytes centred at 128, which is the convention the engine
uses throughout, so the deadzone and range constants mean literally what they say
here.

**This pad is a subset, and it is reported as one.** There is one shoulder row,
one analog stick, and no stick clicks. The second shoulder row, the right stick
and both stick-click buttons are reported absent rather than synthesised from
something else. The shoulders are reported as the **primary** row, where a caller
expecting one row will look for them.

The debug combinations are built only from buttons this pad has:

| Intent | Buttons |
| :--- | :--- |
| Performance snapshot | L1 + R1 + Select |
| Overlay toggle | L1 + R1 + Start |
| Debug menu | Select + Start |

These match the Vita handheld's, because the two devices have the same reduced
button set. They are still answered independently rather than inherited, which is
what keeps this platform free to differ the moment an intent needs a button it
does not have. See [DEBUG.md](../subsystems/DEBUG.md).

Analog triggers are reported absent. The trigger queries still answer — fully
released or fully pressed, following the shoulder switches — because the input
contract guarantees they answer everywhere; the capability is what tells a caller
the value has only two states.

## Window

There is no window. The framebuffer is fixed at 480 x 272 and the native handle
is null.

**The close query is real here**, unlike on the other two consoles. The system
delivers an exit request when the player presses the hardware menu button, and
the platform reports it through the close query so the engine shuts down through
its ordinary path — releasing resources and flushing the log — rather than being
terminated mid-frame. A platform that ignored that callback would appear to hang
on that button press.

## Graphics

| | |
|---|---|
| Framebuffer | 480 x 272, fixed |
| Frame budget | 16667 us |
| Display aspect | 16:9 — **not** the framebuffer ratio; pixels are very slightly non-square |
| Draw list capacity | 1024 |
| Depth buffer | 16 bit — the near plane is raised to 1.0 to suit it |
| Texture budget | 2 MB, in main memory — not video memory |
| Max texture | 512 x 512 — a hardware limit, not a policy |
| Video memory | 2 MB, spent on the frame and depth buffers |

The 512-pixel texture ceiling is the only hard hardware dimension cap among the
platforms here; everywhere else the cap is a budget decision. A cooked texture
larger than that cannot be sampled at all, so the cook list enforces it rather
than leaving it to fail at upload.

## Renderers

| Backend | Role |
|---|---|
| [gu](renderers/GU.md) | **Default.** The native graphics interface, driven directly |
| [pspgl](renderers/PSPGL.md) | A fixed-function subset layered over the same hardware |
| null | Final fallback; headless. See [RENDERER.md](../subsystems/RENDERER.md) |

Fallback order is gu, then pspgl, then null.

The relationship mirrors the console pair on the PlayStation 2 and on the Vita:
one backend drives the hardware directly and is the default, the other exposes an
older and simpler drawing model and is kept as a known-good reference to compare
against. They share no code.

Unlike the Vita's pair, **both ship complete in the toolchain** — the fallback
library is a first-party part of the development kit rather than third-party
source this repository has to carry. That removed a submodule, a build step and a
class of failure from this platform before it was written, and it is why this
platform declares no third-party dependency at all.

The two differ most in where textures live, and therefore in how much texture
they can hold — see [Memory](#memory) above and each backend's own spec. That
difference is reported by each backend rather than derived once for the platform,
because a ceiling taken from one of them is wrong for the other by a large
factor.

### System dialogs are drawn into the title's own frame

As on the Vita, a system dialog here is **not** drawn over the running title by
the system. It is composited into the title's own back buffer, so **every
renderer must hand each presented frame to the dialog service while a dialog is
open**, and the title must keep presenting frames for as long as one is.

Two failure modes follow, and neither reports itself:

- A title that opens a dialog and then waits for it **without presenting frames**
  waits forever. The dialog stays running, the screen holds the last frame, and
  the only symptom is a timeout somewhere unrelated.
- A renderer that presents frames but does not hand them over draws the title
  correctly with **no dialog visible on it**, while the dialog is nonetheless open
  and consuming input.

This is why the flag saying a dialog is open is platform state rather than
something a caller passes: a renderer added later must observe it, and a caller
must not be able to forget to. The non-blocking open/poll/cancel contract is what
this hazard requires; a message box or a confirmation opens the system message
dialog, a text field opens the on-screen keyboard, and neither can be waited on
synchronously without hanging the title.

## Performance

The nine items [guidelines/PERFORMANCE.md](../guidelines/PERFORMANCE.md) asks
every platform to state.

**Budget and pacing.** 60 Hz, 16667 us. At the end of a frame the default
backend finishes its display list, waits for the graphics engine to complete
it, then waits for the vertical blank, then swaps. The processor is idle while
the engine draws and the engine is idle while the processor stages the next
frame: the two halves add rather than overlap, and the display lists — though
double-buffered — buy no overlap today. Build plus draw must fit in one budget
together. The fallback backend presents through its library's swap.

**The binding constraint.** Memory bandwidth, then the main processor. Every
vertex is touched three times on the way to the hardware — staged into a
layout carrying a normal no backend here reads, converted into the hardware's
layout, written back from the cache — and every resident sector is staged
whether or not it is in view. The vector unit is unused. Textures are sampled
unswizzled. Content fits here by bytes moved per frame first.

**Per-frame ceilings.**

| | |
|---|---|
| World vertices per frame, default backend | 24576 |
| Screen-space vertices per frame | 13824 — six per quad of the interface budget plus 256 headroom |
| Display list | 128 KB, two buffers |
| Draw list entries | 1024 |
| Interface quads | 2048, plus 512 overlay quads |
| Draw runs | 1024 world, 256 screen-space |
| Staged vertices | grows on demand from the C heap above — see [backlog/performance_findings.md](../backlog/performance_findings.md) |
| Resident sectors | 9 |

**Clock.** The 64-bit microsecond system clock. It does not wrap.

**Scheduling.** The kernel does not time-slice across priorities. The main
thread runs at the priority the executable's own header declares; the IO
worker and the exit-callback thread are created above it, so a worker preempts
the frame loop to service a request and sleeps otherwise. The worker polls
every millisecond when idle, which costs about sixteen wake-ups a frame above
the main thread; recorded as a finding.

**Cost of diagnostics.** Every log line is three synchronous writes to the
debug output and three more to the log file on the memory card, at every
level. Writing one line to a memory card can cost more than a frame of
rendering; a diagnostic that fires every frame replaces the problem it reports.

**What the snapshot measures here.** Both backends report the present wait —
and on this platform that is only the blank, because the wait for the graphics
engine happens before the timed region and lands in the render figure. Neither
reports geometry build, upload or buffer fill.

**Baseline.** Not yet measured. The next pass runs the boot scene and the
testbed's timing scenes on an original-model console — not the emulator, which
models memory this hardware does not have — and records the split and the heap
here.

**Left on the table**, in the order to attack it: overlapping the list in
flight with the next frame's build; culling sectors and models before staging;
one touch per vertex, in the hardware's layout, without the normal; swizzled
textures; the vector unit; the sky; asynchronous sector recentring.

## Known limitations

- **No native achievement service exists on this hardware**, so the achievement
  contract is absent and unlocks live only in the engine's own cross-platform
  system. That is the hardware's shape, not a gap in progress. See
  [ACHIEVEMENT.md](../subsystems/ACHIEVEMENT.md).
- **The disc container carries an unencrypted executable.** It runs under the
  common emulator and under loaders that tolerate it, but it is not what retail
  hardware expects, and the memory-card container is the route that works
  everywhere. Both ship; see [PACKAGING.md](PACKAGING.md).
- **A failed renderer falls back silently.** The engine logs it and continues with
  the next backend, but the console shows neither the log nor the choice, so the
  only on-device symptom is a frame that looks wrong. Read the log on the memory
  card to find out which backend actually started.
- **The extended memory of later hardware models is requested but never spent.** A
  title built here uses the original model's allowance on every model. Spending it
  would mean a second binary.
- **The near plane is 1.0, not the 0.1 every other platform here uses.** The
  depth buffer is 16 bit and a 0.1 near plane spends almost the whole range in
  the first few units, leaving coplanar surfaces to fight across the rest of the
  scene. Content authored to pass within a unit of the camera will clip on this
  platform and nowhere else.
- **The vector unit is not used.** The processor has one, and the geometry path
  transforms on the main processor without it. Nothing is wrong; it is performance
  left on the table, and the first place to look if the frame budget becomes a
  problem.
- **No sound**, as on every platform.
- **Wireless networking, the camera and the infrared port are not exposed.** The
  hardware has them; the platform contract has no vocabulary for them, and
  inventing one for a single platform would encode this platform's assumptions
  into it.
- Sector recentring is synchronous, as everywhere.
