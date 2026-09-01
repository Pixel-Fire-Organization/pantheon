# Guideline — performance

The rules a change to anything on the frame path obeys, why each one exists,
and what every platform spec has to say about performance so the rules can be
applied to that hardware rather than to a desktop. The procedure for checking
the tree against these rules is [PERFORMANCE_REVIEW.md](PERFORMANCE_REVIEW.md);
the record of the last pass is
[backlog/performance_findings.md](../backlog/performance_findings.md).

**Performance here means the smallest target.** The engine's principle is fixed
budgets decided before the game runs (see [ENGINE.md](../ENGINE.md)), and the
budget that matters is the one on the slowest console, not the one on the
development desktop. A change that is free on Win32 can cost a whole frame on
the PSP. Every rule below is written for the console and merely tolerated by
the desktop.

---

## 1. The frame, as every platform runs it

A frame is the same sequence everywhere; what differs per platform is how long
each part takes and which parts overlap.

| Phase | What happens | Where it is measured |
|---|---|---|
| Poll | Input snapshot, IO completions dispatched, resource frame counter | Inside **logic** |
| Logic | Action resolution, scene or game update, achievements, interface widgets, overlay | **logic** |
| Stage | Draw lists sorted by texture, geometry culled, transformed and written into the backend's layout | **render** (or the backend's own geometry-build figure) |
| Submit | Vertices handed to the hardware — a copy into mapped memory, a packet transfer, a draw call | **render** (or the backend's own upload figure) |
| Present wait | Blocked on the previous frame finishing and on the vertical blank | Reported by the backend as **present wait**; today also inside **render** |

Two facts about this sequence shape every rule that follows:

- **Screen-space work is submitted during logic, before the frame begins.**
  Its cost is known before world geometry is staged, which is why every backend
  reserves the interface's share of its budget up front. See
  [RENDERER.md](../subsystems/RENDERER.md).
- **The present wait is where the frame absorbs slack.** A frame that spends
  most of its time there is idle; a frame that spends none is saturated, and the
  next cost added drops a whole refresh. Reading the two apart is the whole
  point of the snapshot's breakdown, and the reason a part a backend does not
  measure must read as absent rather than zero — see
  [DEBUG.md](../subsystems/DEBUG.md).

Whether staging overlaps drawing is a platform property, stated in each
platform spec: the PS2 builds the next packet while the hardware drains the
previous one; the Vita may run one frame ahead of its display queue; the PSP
today waits for the graphics engine to finish before it waits for the blank, so
its two halves add rather than overlap.

## 2. Rules

Each rule names the reason it exists. Most were learned here; the findings
record says where.

1. **Every per-frame cost has a ceiling, and the ceiling is a platform
   constant.** Vertices per frame, packet bytes, draw calls, draw runs,
   interface quads, overlay quads, resident sectors — each is a number in the
   platform's constants header, and the code refuses work past it rather than
   growing to fit. A ceiling that scales with content is not a ceiling; it is a
   crash deferred to the smallest target.

2. **Reject whole entries before building them, never after.** A backend
   declares what it can accept for the coming frame; staging refuses an entry
   that will not fit or is outside the view before transforming a single vertex
   of it. Building geometry and discarding it at upload costs the full price of
   work that was never going to be shown, and discards mid-object. This is the
   renderer contract; it applies to anything else that stages per-frame work.

3. **Nothing on the frame path allocates, grows, or frees.** Storage is taken
   at load time from an arena, a pool or a fixed table, and the frame runs
   inside it. Growth mid-play fragments a heap that on the handhelds is the
   statistic that decides whether a later reservation succeeds, and it hides a
   content problem until the smallest target hits its ceiling. The
   processor-side geometry stager currently breaks this rule; the findings
   record tracks it.

4. **Cull before transforming, and cull by whole object.** A bounds test costs
   a few multiplies; a transformed vertex costs tens, plus the bytes it writes
   and the bytes the hardware reads back. Anything with a bound — a sector, a
   model, a primitive — is tested against the view before its vertices are
   touched. A path that transforms first and lets the hardware clip pays for
   every vertex behind the camera.

5. **Touch each vertex once, in the layout the hardware reads.** A vertex
   transformed into an intermediate layout and then converted is transformed
   twice from the memory system's point of view. Do not carry an attribute no
   backend on the platform consumes: on the Vita and PSP the staged normal is a
   quarter of every vertex's work and bytes, for a shader that does not read it.

6. **Sort once per frame so runs coalesce.** Draw lists are sorted by texture
   before staging so that consecutive geometry sharing a texture becomes one
   run and one draw call. Texture binds and draw calls are the cost the
   fixed-function consoles feel most; the sort is bounded by the draw-list
   ceiling and is cheap next to what it saves.

7. **Overlap the processor with the graphics hardware.** Double-buffer whatever
   the hardware reads while the processor writes the next frame — packets,
   display lists, vertex buffers — and at the end of a frame wait on the
   *previous* frame's completion, not this one's. Waiting on this frame
   serialises the two halves and halves the budget. Never overwrite a buffer
   the hardware may still be reading; a race there presents as intermittent
   geometry corruption and is not a performance bug you can measure your way
   out of.

8. **Write back only the cache lines you wrote, by range.** Where the hardware
   reads main memory directly, the processor's cache must be written back
   before the transfer — for exactly the spans written this frame, not the
   whole buffer and not the whole cache. On the most bandwidth-bound platform
   here the buffer is mostly empty most frames.

9. **Diagnostics report a change, never a state.** A condition that persists —
   an over-budget interface, a dropped object, a full run table — is logged
   when it starts and when its magnitude changes, not every frame it holds. A
   log line costs what the platform says it costs: a synchronous write to a
   memory card on the handhelds, a round trip to the IO processor on the PS2.
   A per-frame diagnostic on those platforms becomes the slowest thing in the
   frame and lands its cost in whichever phase contains the call. The Vita's
   default backend reports its vertex overflow on change; that is the pattern.

10. **Measure the parts, and let an unmeasured part read as absent.** A backend
    reports what it can time — geometry build, upload, present wait, buffer
    fill — and leaves the rest at the sentinel the snapshot displays as *not
    measured*. A zero that means "not measured" reads as "not a problem" and
    sends the reader to the wrong phase. The engine-level wait figure is itself
    a victim of this today; the findings record has it.

11. **Workers run above the main thread where the kernel does not time-slice,
    and workers block; they never spin.** A frame loop that waits on display
    hardware by spinning does not yield, so a lower-priority worker starves
    silently and IO completes orders of magnitude slower than the medium. A
    worker that polls on a timer costs a wake-up every tick, above the main
    thread, forever; a worker that blocks on a semaphore costs nothing until
    there is work. See the PS2 and PSP platform specs.

12. **Synchronous reads happen behind a load screen, or not at all.** Level
    cores load blocking by design and say so. Sector recentring is the one
    accepted synchronous read on the frame path, documented as a hitch in every
    platform spec; nothing else joins it without the same statement in its own
    spec and in the platform specs it affects.

13. **The cooked texture format is the platform's, and the budget is charged at
    the size the hardware holds.** Palettised where the hardware samples
    indices directly, swizzled where the sampler wants it, expanded to 32-bit
    colour only where nothing smaller exists — and the footprint accounting
    charges the expanded size where expansion happens, or the resource manager
    accepts textures the backend cannot hold. Bandwidth follows the same rule:
    a texture the hardware reads at a quarter the bytes is sampled at a quarter
    the cost.

14. **The interface is budgeted in quads, and text is the cost.** A glyph from
    a cooked font is one quad; from the built-in font it is several. A screen
    is designed to fit the platform's quad ceiling rather than paged at
    runtime, and an unclipped list costs on the slowest platform more than the
    frame it describes. Clipped-away content costs nothing to submit or draw,
    which is why the interface clips before submitting instead of asking the
    hardware to scissor. See [UI.md](../subsystems/UI.md).

15. **Measure on hardware. An emulator lies about time and about memory.** An
    emulator holds the refresh rate whatever the real cost was, so a frame that
    would drop on the console reads as full speed; and it models memory the
    console does not have. Emulator numbers establish that the engine runs and
    that its accounting is sane; they never establish that content fits. Record
    which one a figure came from.

## 3. Per platform

The numbers live in each platform spec's **Performance** section; this is the
shape of the problem on each, and what to reach for first.

**PlayStation 2** — [ps2/PLATFORM.md](../ps2/PLATFORM.md#performance). The
budget is packet bytes and transformed vertices on the direct backend, draw
calls on the library one; fill rate is spent twice on world geometry because
nothing rejects back faces; texture memory is the smallest of any platform on
the default backend. The transform runs on the main processor today. Reach for:
fewer, larger runs; content wound so back-face rejection can be turned on;
anything that keeps geometry inside the guard band so it is not discarded
whole. Never log per frame: every line crosses to the IO processor.

**PlayStation Portable** — [psp/PLATFORM.md](../psp/PLATFORM.md#performance).
Memory bandwidth and the main processor bind first; the vector unit is unused
and the graphics engine is idle while the processor stages, because the frame
is serialised. Every vertex is touched three times on its way to the hardware.
Reach for: culling sectors and models before staging; a staging layout that is
the hardware's; overlapping the list in flight with the next build; swizzled
textures. Every log line is a synchronous memory-card write.

**PlayStation Vita** — [vita/PLATFORM.md](../vita/PLATFORM.md#performance).
The processor is the weakest in the family and stages every vertex every
frame, normals included, for a shader that does not read them; textures are
32-bit uncompressed. The display queue lets it run a frame ahead, which is what
keeps it comfortable today. Reach for: dropping the unused attribute; culling
sectors and models; a compressed texture vocabulary in the cook list. Every
log line is a synchronous memory-card write, and the worker thread has not been
given the priority rule the other consoles use.

**Win32** — [win32/PLATFORM.md](../win32/PLATFORM.md#performance). Nothing
hardware-shaped binds; the frame runs at the display's refresh, which is not
the budget constant, and the engine's own tick without rendering costs a
fraction of a millisecond. This platform is for establishing correctness and
for comparing backends, never for judging cost — a change that measures as slow
here is unshippable on every console. Neither desktop backend measures its
present wait, so the desktop snapshot has no idle figure at all.

**Nintendo Switch (nx)** — [nx/PLATFORM.md](../nx/PLATFORM.md#performance).
The graphics processor has far more headroom than the engine uses; the main
processor's single core staging every vertex binds first, and docked 1080p fill
rate binds second. The default backend double-buffers its vertex and command
memory so staging overlaps drawing by a frame. Reach for: moving static world
geometry's transform onto the graphics processor; a compressed texture
vocabulary; culling before staging. Every log line is a synchronous write to the
SD card through a system service, and the memory that matters is the smaller
allowance a title launched as an applet receives.

## 4. What a platform spec states

A platform is not finished until its spec has a **Performance** section
answering all of the following, in this order, in prose and tables — no code
names, no paths. [NEW_PLATFORM.md](NEW_PLATFORM.md) requires it before the
platform is integrated, and [PERFORMANCE_REVIEW.md](PERFORMANCE_REVIEW.md)
checks each item against the code.

| Item | What it states |
|---|---|
| **Budget and pacing** | Refresh rate and frame budget; what the frame blocks on at its end and in which order; whether staging overlaps drawing, and by how many frames |
| **The binding constraint** | Which resource runs out first on this hardware — packet bytes, draw calls, fill rate, bandwidth, processor transform, memory — and what that means for how content is authored |
| **Per-frame ceilings** | Every fixed per-frame limit the platform sets: vertices, packet or submit buffer, draw calls or runs, interface and overlay quads, resident sectors |
| **Clock** | The source of monotonic time, its resolution, and any wrap the platform absorbs |
| **Scheduling** | Whether the kernel time-slices across priorities; where the main thread and each worker sit; what a spinning frame loop does to the workers |
| **Cost of diagnostics** | Where a log line goes and what one costs; which levels the platform mutes |
| **What the snapshot measures here** | Which of build, upload, present wait and buffer fill each backend reports, and which read as absent |
| **Baseline** | The figures the snapshot or heartbeat reported for a named scene on a named build, dated, with whether they came from hardware or an emulator. "Not yet measured" is an acceptable entry; a missing section is not |
| **Left on the table** | The performance the platform is known to leave unclaimed, in the order to attack it |

The baseline exists so that a later change has something to be compared
against. A platform with no baseline can only be *felt* to have regressed.

## 5. Where the numbers come from

- **The performance snapshot**, taken on demand through the reserved debug
  action ([DEBUG.md](../subsystems/DEBUG.md)): the logic, render and wait split
  against the platform budget; the backend's build, upload, present wait and
  buffer fill; draw counts; texture, arena, pool and heap occupancy; the active
  platform and renderer. The best single smoke test the engine has, and the
  only one that works on hardware with nothing attached.
- **The heartbeat**: one line every fifty frames with frame number, elapsed
  time, heap and instantaneous frame rate. After a hang it pins the frame of
  death; over a session its heap column proves nothing grows.
- **The testbed's timing scenes** ([TESTBED.md](../TESTBED.md)): *Performance*
  shows the split and the renderer's counters live; *Frame pacing* plots frame
  deltas against the budget line and moves a marker at a fixed real speed, so
  a dropped frame is visible as a hitch and a refresh-rate mistake as a speed
  difference; *Platform info* lists every budget the platform declares;
  *Memory* shows every arena and the pool.
- **The examples** ([EXAMPLES.md](../EXAMPLES.md)): each is one capability in
  its own process with its own memory, so a figure taken in one is not
  contaminated by another's residue — the property that makes it a baseline.
- **Emulator capture** (`tools/ps2/emu_capture.py` for the PS2;
  `tools/run_target.py` launches the others): keeps the console log, in which
  the heartbeat and any snapshot appear. Emulation figures are labelled as such
  wherever they are recorded; see rule 15.

## Definition of done

For a change that touches a per-frame path — the frame loop, staging, a
backend's frame, the interface, IO dispatch, sector streaming, or a platform's
clock, thread or console:

- [ ] Every new per-frame cost is bounded by a platform constant, and whole
      entries are refused past it
- [ ] Nothing new allocates, grows or frees on the frame path
- [ ] Any new diagnostic on the frame path reports on change, not per frame
- [ ] Anything new the backend can time is reported through the frame
      statistics, and anything it cannot is left absent
- [ ] Any change to what a frame waits on, or in what order, is reflected in
      the platform spec's *Budget and pacing* item
- [ ] Any change to a ceiling, a priority, a clock or the cost of a log line is
      reflected in the matching item of the platform spec
- [ ] The snapshot was taken on the platforms the change affects, and the
      spec's *Baseline* updated if the figures moved — with the source
      (hardware or emulator) named
- [ ] A regression or a newly unclaimed cost is recorded in
      [backlog/performance_findings.md](../backlog/performance_findings.md) or,
      if fixed, in [fixed_issues/issues.json](../fixed_issues/issues.json)
