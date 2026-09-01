# Backlog — performance review findings

Working notes for future sessions, not a commitment made in the change that
added them. Findings from a full-codebase performance pass, kept here until
each is either fixed (at which point it moves to
[fixed_issues/issues.json](../fixed_issues/issues.json)) or dismissed with a
reason recorded inline.

**Status.** First pass, 2026-09-16, over the whole tree on all four platforms:
the frame loop and the snapshot, both PS2 backends, both PSP backends, both
Vita backends, both Win32 backends, the shared geometry stager, the interface,
IO dispatch and the worker, sector streaming, and every platform's clock,
thread, console and memory implementation. It followed
[guidelines/PERFORMANCE_REVIEW.md](../guidelines/PERFORMANCE_REVIEW.md); the
rules it measured against are
[guidelines/PERFORMANCE.md](../guidelines/PERFORMANCE.md). **No engine code
was changed by this pass.** Every finding below is open; the pass also wrote
the *Performance* section into every platform spec and took the two baselines
it could take from here (Win32 on hardware, PS2 under emulation). The console
baselines on hardware are the first thing the next pass should add.

**nx addition, 2026-09-17.** The platform-addition pass
[guidelines/NEW_PLATFORM.md](../guidelines/NEW_PLATFORM.md) requires, over the
nx platform and both its backends, read, then run once the container build was
in place: the boot scene under Ryujinx held 60 frames per second (58.1–62.0
instantaneous) with the heap constant at 54 272 KB over 900 frames, recorded as
an emulator baseline in [nx/PLATFORM.md](../nx/PLATFORM.md#performance). One gap was fixed before the platform landed — the main
thread's priority was declared but never applied, so the worker-above-main rule
held only by the loader's default; the platform now sets it. Existing findings
that also hold on nx have it added to their platform column; PF-16 is new.

**PF-05 fixed, 2026-09-17.** Root cause was not the hardware or the emulator:
ps2sdk's `calculate_vertices` divides its output's x/y/z by the homogeneous w
internally and leaves w undivided, and both the self-test's comparison and the
guarded runtime transform assumed the same raw, undivided clip vector a
from-scratch matrix multiply produces. Fixed by comparing (and consuming) that
output on its actual terms; the self-test now passes and the direct backend's
default path is the vector-unit batch transform, confirmed against `ps2gl` on
the boot scene via `tools/ps2/emu_capture.py`. See
[fixed_issues/issues.json](../fixed_issues/issues.json) (EX-0016) for the full
account and [ps2/renderers/GIFTAG.md](../ps2/renderers/GIFTAG.md) for a
related gap the same investigation surfaced but did not fix: the batch
routine's own frustum test can return a clipped vertex as screen-centre rather
than signalling "discard."

## Scope and what was measured

The pass read the code rather than profiled it, and confirmed what it could by
running two builds:

- **Win32, hardware** (development desktop, RTX 3060, 144 Hz display), the
  game's boot scene (main menu), nine seconds per renderer, read from the
  heartbeat: `webgpu` 141–162 frames per second (instantaneous, so it
  jitters), `opengl` 143.7–145.5 (locked to the display's refresh), `null`
  10 000–20 000 (the engine's own tick with nothing drawn: 50–100 µs). Heap
  constant at 54 272 KB across all three runs.
- **PS2 PAL, emulation** (PCSX2, software renderer), same scene, default
  `giftag` backend, 25 seconds: holds 50.0 frames per second throughout, heap
  constant at 15 466 KB. The emulator holds the refresh whatever the frame
  costs, so the frame-rate figure proves only that the loop paces; the log did
  yield finding PF-05.

Nothing was run on the PSP or the Vita, and nothing was run on PS2 hardware.

## Findings

Severity is against the smallest platform the finding affects.

| Id | Platform | Severity | Summary |
|---|---|---|---|
| PF-01 | all | High | The engine-level wait figure is always zero; present wait is buried in *render*, so the snapshot cannot tell an idle frame from a saturated one |
| PF-02 | PS2, PSP, Vita | High | Seven per-frame diagnostics log a persisting condition every frame it holds; on the handhelds each line is a synchronous memory-card write |
| PF-03 | PSP, Vita, Win32, nx | Medium | The shared stager culls primitives but neither sectors nor models; every resident sector is transformed every frame whatever the camera sees |
| PF-04 | PSP, Vita, nx | Medium | The shared stager's storage is heap, grown by reallocation at runtime, to a ceiling far past any platform's budget; the specs say it lives in the arena |
| PF-06 | PSP | Medium | The frame is serialised: the processor waits for the graphics engine to finish this frame's list before waiting for the blank, so build and draw add rather than overlap |
| PF-07 | PSP | Medium | Every vertex is touched three times on the way to the hardware — staged with an unused normal, converted to the hardware layout, written back — on the most bandwidth-bound platform |
| PF-08 | PSP | Medium | Textures are uploaded unswizzled, which the backend's own spec says samples considerably slower |
| PF-09 | Vita | Medium | The default backend's single vertex buffer is overwritten by the next frame's upload while the previous frame may still be reading it |
| PF-10 | Vita | Low | The IO worker runs at the same priority as the main thread; the worker-above-main rule the other consoles document is not applied and the spec was silent |
| PF-11 | PS2, PSP | Low | The IO worker polls every millisecond when idle, above the main thread, rather than blocking on a queue |
| PF-12 | Win32 | Low | The frame budget constant says 60 Hz; the frame is paced to the display's refresh, 144 Hz here, so the snapshot's budget comparison and the real frame disagree |
| PF-13 | Win32 | Low | Neither desktop backend measures present wait or geometry build and upload, and the snapshot prints the present wait as `0.00` rather than absent |
| PF-14 | all | Low | The frame-rate figure is instantaneous; the heartbeat and snapshot have no windowed figure |
| PF-15 | PSP, Vita, nx | Low | The log file has no level filter; debug-level lines cost a memory-card write each, where the PS2 mutes them |
| PF-16 | nx | Low | The reference backend orphans its vertex buffer every frame, which is a driver allocation per frame; the default backend's fenced double buffer is the pattern |

### PF-01 — the wait figure measures nothing

The frame loop reads the clock after the backend's end-of-frame and then reads
it again immediately, and reports the difference as the wait. Nothing happens
between the two reads, so the figure is the cost of one clock read. Every
backend blocks on the display *inside* its end-of-frame, so that time lands in
*render*. Consequences: the snapshot's `GPU Wait` line is always near zero; its
`Total Frame Time` equals the whole frame; the testbed's *used of budget* bar
reads near 100 % whenever vertical sync holds, which is exactly when the frame
is idle. The backend-reported `presentWaitMs` is the real figure, printed on
the next line, but the split above it is what a reader trusts. The spec
([DEBUG.md](../subsystems/DEBUG.md)) is right and the loop is wrong.

Remedy: subtract the backend's present wait from *render* and report it as the
wait — or have the loop itself bracket the wait, which needs the present to be
a separate call from the end-of-frame. Either way the *used of budget* bar
should exclude it.

### PF-02 — persistent conditions logged per frame

Seven sites log a dropped-work condition every frame it persists, against the
rule in [DEBUG.md](../subsystems/DEBUG.md) that per-frame logging is for a
condition that has just changed:

- the shared stager's dropped screen-space runs (`StagedGeometry.cpp`, end of
  frame);
- the direct PS2 backend's dropped objects, dropped queued quads and dropped
  quads on a full packet (`GifTag.cpp`, three sites);
- the library PS2 backend's dropped queued quads (`Ps2Gl.cpp`);
- the interface's dropped quads and dropped overlay quads (`Ui.cpp`, two
  sites).

On the PSP and the Vita a line is three synchronous writes to the log file on
the memory card plus the debug output; on the PS2 it is a round trip to the IO
processor. An interface one quad over budget — a content mistake — therefore
costs a card write per frame, which is far more than the quad. The Vita's
default backend already reports its vertex overflow only when the shortfall
changes; that is the pattern to copy to all seven.

### PF-03 — sectors and models are never culled by the shared stager

The stager tests a primitive's bounding sphere against the view before
transforming it. It transforms every mesh of every ready resident sector, and
every mesh of every submitted model, with no test at all. The PS2 direct
backend, which has its own path, tests each sector's bounds and skips it —
so the two paths disagree, and the shared one is the slower on the platforms
that can least afford it. With nine resident sectors, most of the world behind
the camera is transformed every frame on the PSP and the Vita.

Remedy: test the sector bounds the level format already carries, and a
model's bounds (from its meshes at load time), before appending — the same
whole-entry refusal the primitive path already does, counted as *entries
culled* so the snapshot shows it working.

### PF-04 — stager storage is heap, and grows

The stager's vertex arrays are grown by reallocation, doubling from four
thousand vertices, to a ceiling of two million vertices — 96 MB at 48 bytes a
vertex. The PSP and Vita renderer specs say staging storage is the backend's
share of the renderer arena; that is true of the backend's *converted* copy,
not of the stager's own arrays. On the PSP the stager's arrays reach about
2.3 MB of the 6 MB heap at the platform's ceilings, invisible to the snapshot's
arena figures (they appear under *misc*), and a reallocation mid-play
fragments the heap the platform spec says is the statistic that matters.
Contradicts the fixed-budget principle and rule 3 of
[PERFORMANCE.md](../guidelines/PERFORMANCE.md).

Remedy: size the arrays once at construction from the backend's declared frame
ceiling (which every backend already passes to the stager) and take them from
the renderer arena; refuse past it as the backend already does. Correct the two
renderer specs.

### PF-06 — the PSP frame is serialised

The default PSP backend ends a frame by finishing the display list, waiting for
the graphics engine to complete it, then waiting for the vertical blank, then
swapping. The processor is idle while the engine draws and the engine is idle
while the processor stages the next frame; the display lists are
double-buffered but nothing overlaps. The graphics engine's time lands in
*render* (it is inside the end-of-frame before the timed wait), and the
reported present wait is only the blank.

Remedy: wait on the list about to be *reused* — the previous frame's — at the
start of the end-of-frame, and swap after the blank without waiting on this
frame's list, keeping the display buffers' own ordering. This is the standard
pipelined shape on this hardware and is what the double-buffered lists were for.

### PF-07 — three touches per vertex on the PSP

Each vertex is written by the stager into a 48-byte layout carrying a normal
no PSP backend reads, then read back and converted into the 24-byte hardware
layout in a second pass, then written back from cache by range. On the platform
whose binding constraint is bandwidth this is roughly twice the necessary
traffic, before the hardware has read a byte.

Remedy: let the stager emit a backend-declared layout, or convert during
staging; drop the normal from the staged vertex on platforms whose shaders do
not read it (the Vita's spec already records the same waste).

### PF-08 — PSP textures are unswizzled

Textures are uploaded with swizzling off. The backend's own spec states that
swizzled textures sample considerably faster and that bandwidth, not fill, is
the constraint. Remedy: swizzle at cook time through the platform's cook list
(so the upload stays a copy), or on upload.

### PF-09 — the Vita vertex buffer is not fenced

The default Vita backend uploads each frame's vertices by copying into a single
mapped buffer, then draws, then queues the frame for display. The display queue
allows one pending frame, so queueing frame N blocks until frame N−1 has been
displayed — which guarantees N−1's reads are finished, but says nothing about
frame N, whose commands may still be executing when the *next* end-of-frame
copies frame N+1's vertices over the same buffer. Nothing fences the copy. The
race window is the time the graphics processor is still working on N after the
processor has finished N+1's logic; a heavy frame widens it. This is a
correctness question as much as a performance one; the performance-correct fix
is a second buffer indexed by the display buffer, not a finish that would
serialise the frame as the PSP's is.

### PF-10 — Vita worker priority

The IO worker is created at the default priority, the same as the main thread,
and the main thread is not lowered. The PS2 and PSP specs document why workers
run above the main thread; the Vita spec said nothing. It works today because
the main thread blocks in the display queue every frame, so the worker gets the
processor; it would degrade silently the moment the frame loop waited by
spinning. The spec now records this; the remedy is to apply the same rule.

### PF-11 — the IO worker polls

When no request is queued the worker sleeps a millisecond and rescans the
request table. That is about a thousand wake-ups a second — on the PS2 and PSP
each one preempts the main thread, roughly sixteen to twenty times a frame,
each taking and releasing the request lock. Small, but permanent and avoidable:
a semaphore signalled by the enqueue would cost nothing until there is work.
The one-millisecond sleep also has a coarser real granularity on Win32 than its
name suggests, which is harmless there.

### PF-12 — Win32 pacing versus budget

The platform's frame budget is 16 667 µs; the frame is paced to the display
through vertical sync, and on the development desktop the display is 144 Hz,
so the frame is about 6.9 ms. The snapshot compares against the budget, the
testbed plots the budget line, and both are the right thing for content
authored against the console — but the spec did not say the two differ, and a
reader of the desktop snapshot would conclude the frame had two thirds of its
budget spare. Recorded in the spec; optionally the budget could follow the
display mode on this platform, since here it is policy rather than hardware.

### PF-13 — the desktop backends measure nothing but counts

Neither desktop backend fills present wait, geometry build or upload. The
snapshot prints `Present Wait : 0.00 ms` unconditionally, which is the zero the
debug spec forbids; the testbed's *Performance* scene guards build and upload
with *not measured* but has no guard for present wait. Remedy: bracket the
present on both backends (it is one call on each), and guard the snapshot
line.

### PF-14 — instantaneous frame rate

The frame-rate figure is the reciprocal of the last delta. The Win32 baseline
shows what that means: a locked 144 Hz reads as 141 to 162. A windowed average
beside it, over the heartbeat interval, would make the heartbeat a trend rather
than a sample.

### PF-15 — handheld log files take every level

The PS2 console mutes debug-level lines because each crosses to the IO
processor. The PSP and Vita write every level to the memory-card log file with
no filter, and each line there is a synchronous write. Nothing logs at debug
level per frame today, which is why this is low; the filter is cheap insurance.

### PF-16 — the nx reference backend orphans its buffer every frame

The reference backend re-specifies its whole vertex buffer before each frame's
upload so the driver can hand back storage the frame in flight is not reading.
That is a driver-side allocation per frame, sized at up to twice the frame's
vertices, inside a heap that on this platform is shared with everything else.
It is the reference backend, not the one that ships, which is why it is low.
Remedy: two persistently mapped buffers alternated per frame and fenced on the
previous frame's completion — what the default backend already does.

## Checked, no finding

Recorded so the next pass can skip it rather than re-derive it.

- **Draw-list sort**: two standard sorts per frame over at most the draw-list
  ceiling (1024 on the consoles, 4096 on the desktop). Bounded and cheap next to
  the run coalescing it enables.
- **Interface**: the state store is an open-addressing hash, one probe per
  widget; text is measured once per label; quads are clipped before submission
  so clipped-away content costs nothing; the overlay is concatenated rather
  than sorted; the frame's quad ceiling is a platform constant and whole
  containers are budgeted. Matches [UI.md](../subsystems/UI.md).
- **Action resolution**: once per frame, proportional to the bindings in the
  active contexts, from static tables. Matches [ACTION.md](../subsystems/ACTION.md).
- **Resource and IO pumps**: the resource update is a counter increment; the IO
  pump takes one lock and scans a 16- or 32-slot table, dispatching callbacks
  with the lock released.
- **Heartbeat**: one line every fifty frames, with a heap query — constant time
  on every platform except the PS2, where it walks the allocator's bins; once a
  second that is acceptable, and the line is deliberately per-second rather
  than per-frame.
- **Memory over a session**: the heap figure was constant across the whole of
  both runs (54 272 KB on Win32, 15 466 KB on PS2), so nothing on the frame
  path allocates in the boot scene. The stager's growth (PF-04) happens on the
  first frame that needs the room and then holds.
- **PS2 overlap**: both PS2 backends build the next frame's packet while the
  hardware drains the previous one, and wait on the previous frame's completion
  and the blank before sending. The direct backend reserves the interface's
  packet share before world geometry, drops whole entries with a count, and
  reports buffer fill and present wait. The library backend reports present
  wait only; it cannot see the packet it does not own.
- **Vita overlap**: the display queue with one pending frame lets the
  processor run a frame ahead; the default backend reports build, upload,
  present wait and buffer fill, and reports its overflow only on change. This
  is the model backend for measurement.
- **PSP cache write-back** covers exactly the two spans written this frame,
  not the buffer.
- **Frustum culling of primitives** in the shared stager, and of sectors in
  the PS2 direct path, both count into *entries culled* so the snapshot shows
  them working.
- **Clocks**: microsecond or better on every platform; the PS2's 32-bit wrap is
  absorbed by the platform; the Vita's is process time and stops while
  suspended, which is correct for a delta.
- **nx default backend overlap**: the vertex buffers and the command memory are
  doubled and alternate per frame, and the backend waits on the fence of the
  frame that last used a slice before rewriting it; the time blocked there and in
  acquiring a display buffer is reported as present wait, apart from build and
  upload. Its vertex overflow is reported on change. The normal is consumed by
  the shared shader, so the staged layout carries nothing unused (rule 12 holds
  here where it does not on the Vita and PSP).
- **nx scheduling**: the IO worker is created one priority above the main
  thread, which the platform now sets explicitly, and on a different core when the
  process core mask allows, so its polling (PF-11) does not preempt the main
  thread's core as it does on the single-core consoles.
- **nx clock**: the system tick at 19.2 MHz; time spent suspended by the HOME
  menu is frozen out of the clock, so the frame after resume is not a giant
  delta.
- **Synchronous reads on the frame path**: only the sector loader, which every
  spec documents as the accepted recentring hitch, and the testbed's asset
  browser, which is a debug tool.

## Known gaps a later pass should weigh

Neither is a finding today; both are places where the current design accepts a
cost knowingly, and a later pass should confirm the trade is still the right
one rather than re-discovering it.

- **No console baseline on hardware.** The PS2 and nx figures are emulation and
  the PSP and Vita have none. Every platform spec's *Baseline* item says which it
  has. Until a build has run on each console with the snapshot read, the specs'
  ceilings are budgets that have never been measured against.
- **No performance gate in CI, and no stress example.** The examples are a
  primitives showcase and an interface gallery; neither loads the frame. A
  standalone example that fills the draw list, the vertex ceiling and the
  interface quad budget to a declared fraction would give every platform a
  repeatable load to take its baseline from, and a heartbeat line the CI could
  read for a frame-rate floor on the desktop.
- **Synchronous sector recentring.** Up to three new sectors on a straight
  crossing and five on a diagonal, each a synchronous archive read of up to a
  slot (256 KB on the PS2, 512 KB on the PSP), on the frame that crosses.
  Documented as a hitch in every spec since before this pass; the asynchronous
  replacement the sector spec admits is the fix.
- **Level geometry is drawn double-sided** on every backend because compiled
  faces are not reliably wound, which spends fill rate twice on the platforms
  with the least. A content-pipeline fix, not a renderer one, recorded here so
  it is weighed against the renderer findings above.
