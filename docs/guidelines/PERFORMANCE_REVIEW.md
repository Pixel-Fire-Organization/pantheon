# Guideline — performance review

How to establish that the tree obeys [PERFORMANCE.md](PERFORMANCE.md) and that
every platform spec's *Performance* section describes the platform as built.
That guideline is the set of rules a change obeys; this one is the procedure a
pass follows to prove the tree obeys them — whether the pass reviews one change
or sweeps everything.

The full-codebase pass of 2026-09-16 is the model.
[backlog/performance_findings.md](../backlog/performance_findings.md) is its
record: what it examined, what it found, what it measured, and what it accepted
as a known cost. **A pass starts from that record**, so it spends its time on
what has changed rather than re-deriving what is already established.

---

## 1. When to run one

**After any change to:**

- the frame loop, the frame statistics, or the performance snapshot
- geometry staging, a backend's frame — begin, render, end, present — or a
  backend's texture upload
- the interface's submission or clipping
- IO dispatch, the IO worker, sector streaming or the level loader
- a platform's clock, thread creation, priorities, console output or memory
  contract
- any per-frame ceiling in a platform constants header

Review the change against section 3, check the platform spec items section 4
names for every platform the change touches, and take the snapshot on those
platforms (section 5).

**Periodically as a full pass, and always before a platform is added and again
before it is called done.** A new platform is a new clock, a new scheduler, a
new cost per log line and a new binding constraint; each is a place where a
rule that held everywhere else can stop holding. Run sections 2 through 6 in
full.

## 2. Enumerate the surface

A pass that reviews what it remembers misses what it forgot. Enumerate
mechanically, from the tree, every time. These commands are the starting list;
walk every hit against section 3. They use ripgrep; `grep -rnE` takes the same
patterns.

Every read of the clock — where the frame is measured, and where a backend
measures its own parts:

```
rg -n "GetTimeSeconds\(\)" engine/src
```

Every diagnostic phrased per frame — the candidates for a persistent condition
logged every frame it holds. The expected result is that each one either
reports on change or is inside a path that already does:

```
rg -n "Engine_Log(Error|Warn|Info)\(.*(this frame|per frame)" engine/src
```

Every allocation or growth in shared code — the expected result is load-time
paths only, never anything reachable from the frame:

```
rg -n "realloc\(|malloc\(|calloc\(" engine/src/graphics engine/src/ui engine/src/core engine/src/level engine/src/scenes
```

Every sleep, spin and drain — where a thread waits by polling rather than
blocking:

```
rg -n "SleepMicros\(|MAX_SPINS|while \(true\)" engine/src
```

Every synchronous read — the expected result is the level core, the sector
loader, the archive mount, the IO worker itself and the testbed's asset
browser; anything else on the frame path is a finding:

```
rg -n "Engine_Archive_ReadSync\(|->FileRead\(" engine/src
```

Every point a backend waits on the display — the order of calls here is the
platform's pacing model, and the spec must describe it:

```
rg -n "WaitVblank|wait_vsync|WaitForVSync|SwapBuffers\(|SurfacePresent|DisplayQueueAddEntry|sceGuSync\(" engine/src/platform
```

Every cache write-back — each must cover a range actually written this frame:

```
rg -n "Dcache|FlushCache" engine/src/platform
```

Every frame statistic a backend fills — what is absent from this list is what
the snapshot must show as *not measured* for that backend:

```
rg -n "presentWaitMs =|geometryBuildMs =|geometryUploadMs =|submitBufferUsedBytes =" engine/src
```

Every per-frame sort:

```
rg -n "std::sort|qsort\(" engine/src
```

Every transform path — each is a per-vertex loop, and each must sit behind a
whole-object cull:

```
rg -n "vertsTransformed \+=" engine/src
```

Every thread priority a platform sets, and the constant it sets it from:

```
rg -n "PRIORITY|Priority" engine/include/platform engine/src/platform
```

## 3. Walk each hit against the checklist

For each clock read in the frame loop:

1. Does the interval between two consecutive reads contain the work the
   figure claims to measure — and nothing else? Two reads with nothing between
   them measure nothing and print as zero.
2. Is the present wait accounted separately from the processor work that
   precedes it, so an idle frame and a saturated frame read differently?

For each per-frame diagnostic:

3. Is it emitted when the condition begins or changes magnitude, and silent
   while the condition merely persists?
4. On the platforms where a line is a synchronous storage write or a
   cross-processor round trip, would this line fire more than once a second in
   the failure it describes?

For each allocation, growth or free:

5. Is it reachable from the frame loop, from a backend's frame, from the
   interface, or from IO dispatch? If so, why is it not a fixed table sized
   from a platform constant?

For each sleep, spin or drain:

6. Is the thread waiting for work it could block on instead? What does one
   wake-up cost, and how many happen per frame, on the platform where the
   worker preempts the main thread?

For each synchronous read:

7. Is it behind a load screen, or is it the documented sector-recentre hitch?
   Anything else is a finding, and the platform specs it affects must state
   it.

For each display wait:

8. Does the backend wait on the *previous* frame's completion before reusing
   its buffers, and on this frame's only where the hardware gives no other
   choice? Is every buffer the hardware reads double-buffered against the
   processor's next write, or fenced?
9. Is the time spent blocked reported as present wait, and is the graphics
   hardware's own time — if the processor waits for it — reported separately
   from the processor's?

For each cache write-back:

10. Is the range exactly what was written this frame?

For each transform path:

11. Is there a whole-object bounds test before the first vertex is touched —
    for sectors, for models, and for primitives alike?
12. Is the vertex written once, into the layout the hardware reads, without
    attributes no backend on this platform consumes?

For each thread priority:

13. On a kernel that does not time-slice across priorities, is every worker
    above the main thread, and is the main thread lowered from what the loader
    gave it? Does the platform spec say so?

A "no" to any question is a finding. Record it before fixing it (section 6).

## 4. Check each platform spec against the code

Every `docs/<platform>/PLATFORM.md` has a *Performance* section with the nine
items [PERFORMANCE.md](PERFORMANCE.md) lists. For each platform:

- **Budget and pacing** matches the order of the display-wait calls found in
  section 2 and the constants header's frame budget.
- **Per-frame ceilings** match the platform's constants header, value for
  value. A ceiling in the header with no row in the spec, or a row with no
  constant behind it, is a finding.
- **What the snapshot measures here** matches the statistics-fill hits in
  section 2 for each of the platform's backends.
- **Scheduling** matches the priority hits.
- **Cost of diagnostics** matches the platform's console implementation —
  where a line goes, whether it is flushed per line, which levels are muted.
- **Baseline** is dated and names its source. A baseline older than the last
  change to the frame path is stale and is re-taken (section 5).

A spec item the code contradicts is a finding against the spec if the code is
right and against the code if the spec is; either way the two are brought into
agreement in the same change.

## 5. Measure

**Take the snapshot on every platform the change touches**, in the game's boot
scene and in the testbed's *Performance* scene, and read:

- the split against the budget — and whether the present wait is
  distinguishable from the rest, or buried in *render*;
- every renderer counter — entries culled must move when the camera turns
  away from content; a count that never moves is a cull that never runs;
- buffer fill against capacity, on backends that report it;
- heap, arena and pool occupancy — and the heap column of the heartbeat over
  a minute, which must not climb.

**Run the testbed's *Frame pacing* scene** and watch the plot: a frame that
crosses the budget line is a dropped refresh, and the marker's speed is the
same on every platform or something integrates the wrong time.

**Where the hardware is available, measure on it and record it as hardware.**
Where only an emulator is, record it as emulation and treat the timing figures
as proof that the loop runs, not that content fits — see rule 15 of
[PERFORMANCE.md](PERFORMANCE.md). `tools/ps2/emu_capture.py --log` keeps the
console log for the PS2, heartbeat and snapshot included; `tools/run_target.py`
launches the other emulators, whose logs are wherever the platform writes its
log file.

**Set the baseline** in the platform spec from what was measured: date, build,
scene, renderer, the figures, the source.

## 6. Record the outcome

Every finding goes to one of two places, in the same change:

- **Fixed** → an entry in [fixed_issues/issues.json](../fixed_issues/issues.json)
  (schema in `tools/schemas/fixed_issues.schema.json`) with the symptom, the
  root cause, what changed, the files, and a regression check naming the
  scene, the platform and the figure or log line to expect. The spec it
  corrected is updated in the same change, baseline included.
- **Dismissed or accepted** → a paragraph in
  [backlog/performance_findings.md](../backlog/performance_findings.md): under
  "Checked, no finding" with what was examined and why it is sound, or under
  "Known gaps" with the cost being accepted and the condition that would
  reopen it.

Nothing is left in a review comment, a commit message or a chat transcript.
The next pass reads the backlog and the issues log; it does not read those.

## Definition of done

A pass is complete when:

- [ ] Every command in section 2 was run and every hit walked against
      section 3
- [ ] Every platform spec's nine *Performance* items were checked against the
      code both ways
- [ ] The snapshot and the pacing scene were run on every platform the pass
      covers, and each spec's *Baseline* is dated within the pass, naming
      hardware or emulator
- [ ] Every finding is either an issues-log entry with its spec updated, or a
      backlog paragraph with its reason
- [ ] The status section of the backlog names the pass, its date and its
      scope
