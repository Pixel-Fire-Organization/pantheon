# Guideline — adding a platform

A *platform* is a target the whole engine builds for: PS2PAL, PS2NTSC, WIN32.
This is the order to add one in, and what "finished" means.

**The property to preserve: adding a platform is adding one same-named
subdirectory under each of `engine/include/platform/`, `engine/src/platform/`
and `engine/config/` — never touching an existing platform's files.** No
shared engine file should need editing beyond a single registration. If you find
yourself changing an existing platform's files under those three roots, stop — something belongs behind the platform
contract that is not there yet.

---

## 1. Write the specs first

**Before any code.** A platform needs three or four documents:

| Document | Content |
|---|---|
| `docs/<platform>/PLATFORM.md` | Capability matrix, constants, memory budget, filesystem and path semantics, input sources, window model, renderer set and fallback order, **performance** — the nine items [PERFORMANCE.md](PERFORMANCE.md) lists: budget and pacing, the binding constraint, per-frame ceilings, clock, scheduling, cost of diagnostics, what the snapshot measures, a dated baseline, what is left on the table — and known limitations |
| `docs/<platform>/BUILD.md` | Prerequisites, build commands, presets, running, debugging, known blockers |
| `docs/<platform>/renderers/<NAME>.md` | One per backend: what it targets, geometry and texture model, **quirks and limits** |
| `docs/formats/` | Only if the platform introduces a format. It should not. |

Write them abstractly — contracts and guarantees, not code. The renderer specs
are the ones people will actually reach for: they exist to record what a backend
*cannot* do, so the next person does not spend a day discovering it.

Decide and write down, before building:

- **Is this one platform or a family?** Values that differ per variant (screen
  region, refresh, video mode) mean variants that inherit an abstract base, as
  PS2PAL and PS2NTSC do. The base is never selectable on its own, because every
  varying value must have exactly one correct answer in a built binary.
- **What is genuinely different in kind** from existing platforms — a policy
  budget rather than a hardware ceiling, a resizable window rather than a fixed
  framebuffer. State it, because it changes what the constants mean.
- **What binds first on this hardware** — packet bytes, draw calls, fill rate,
  bandwidth, processor-side transform, memory — and how the frame will be
  paced: what it waits on, in what order, and whether staging can overlap
  drawing. These decide the per-frame ceilings and the worker priorities
  before a line of the backend exists; a platform whose frame is serialised by
  accident has half the budget it was designed for. The performance section
  of the spec is written now, with the baseline left as *not yet measured*
  until section 5 fills it.

## 2. Decompose into components and tasks

Every entry below traces to a section of the platform contract. Work the list;
do not discover it as you go.

**Platform contract** (`engine/include/platform/Platform.h`) — all of it:

- [ ] Identity and lifecycle: id, name, `Init`, `Shutdown`, startup args
- [ ] `GetConstant` — **every** `PlatformConstant` key
- [ ] `HasCapability` — **every** `PlatformCapability` key, answered honestly
- [ ] Memory contract: reserve, release, aligned alloc and free, heap stats, budget
- [ ] Texture footprint accounting
- [ ] Filesystem: build path, resource token, open, seek, read, size, close
- [ ] Time: monotonic seconds, sleep
- [ ] Threads and semaphores
- [ ] Console write and panic
- [ ] Input: poll, and the gamepad, keyboard and mouse groups
- [ ] `Keyboard_PopCharacters` — the character channel behind
      `PlatformCapability::TextCharacters`; a platform with no character device
      returns zero always, honestly, the same as an absent capability elsewhere
- [ ] `Dialog_Open` / `Dialog_Poll` / `Dialog_Cancel` — the non-blocking dialog
      contract behind `PlatformCapability::SystemDialog`; shaped for a host
      that renders a dialog into its own frame and cannot be waited on
      synchronously, so implement the poll even where the host's own call
      blocks (see Win32's `MessageBox` for the pattern). A platform with no
      system dialog service answers `Dialog_Open` false and touches nothing —
      the UI subsystem's own drawn modal is what runs instead, unconditionally,
      so this is never a feature the platform can leave half-built
- [ ] Window: open, close, should-close, framebuffer size, native handle
- [ ] Debug combinations: the buttons **this** pad can actually produce
- [ ] Renderers: supported set, default, fallback chain, create, destroy

**Directory contents** — one `<name>` subdirectory under each of three roots,
split by what kind of file it is:

`engine/include/platform/<name>/` (headers):
- [ ] `Platform.h`, plus one header per concern
- [ ] `PlatformConstants.h` — budgets and capabilities, **no format constants**.
      `engine/src/platform/PlatformContract.cpp` lists every constant a
      platform must define and the invariants between them; it is compiled
      into every engine library, so a missing or inconsistent constant fails
      the build naming it.
- [ ] `renderer/` — one subdirectory per backend it introduces, its headers

`engine/src/platform/<name>/` (sources):
- [ ] `Platform.cpp`, plus one source per concern
- [ ] `Entry.cpp` — the process entry point
- [ ] `renderer/` — one subdirectory per backend it introduces, its sources

`engine/config/<name>/` (everything that is neither a header nor a source):
- [ ] `cooklist.json` — how this platform wants assets baked, validated
      against `tools/schemas/cooklist.schema.json`
- [ ] `platform.cmake` — `platform_configure()`, optionally
      `platform_dependencies()`, `platform_package()` and
      `platform_debug_symbols()`
- [ ] anything else that is build config or a non-C++ asset (a shader source
      the offline compiler consumes, an editor-config generator script) —
      never a header or a `.cpp`, which belong in the two roots above instead

**The fragment interface.** The root driver calls these by fixed name and
consumes three optional variables. Everything a platform needs to say about
itself goes through them — the root names no platform:

| Hook / variable | Purpose |
|---|---|
| `platform_configure(P)` | Sources, includes, libraries for the engine library |
| `platform_dependencies()` | Build third-party deps, before any engine target exists |
| `platform_package(P, EXE, DIST)` | Produce the distributable (ISO, staged bundle, ...) |
| `platform_run(P, EXE, DIST)` | A `run-<dist>` target launching the artifact |
| `platform_debug_symbols(P, EXE, DIST)` | Symbol extraction, if any |
| `platform_example_package(P, NAME, STAGE, EXE, DIST)` | Append a container build to an example's staging target, for a platform whose executable cannot run from a staged folder |
| `PLATFORM_<P>_LINK_DEPS` | Targets the executable must wait on |
| `PLATFORM_<P>_PACKAGE_TARGET` | The packaging target `dist` should depend on |
| `PLATFORM_<P>_CLEAN_PATHS` | Extra paths `clean-all` removes |

Anything else your platform needs — a disc serial, an image name, a packaging
tool — is set inside your fragment, not in the root.

**Integration** — the only edits outside your directory:

- [ ] One entry in `ENGINE_KNOWN_PLATFORMS` and its metadata block in
      `cmake/Platforms.cmake`
- [ ] A toolchain file under `toolchains/`, if it needs one
- [ ] Ids in `PlatformId` and any new `RendererId`

## 3. Evaluate off-the-shelf components

A platform is where third-party code is most tempting: windowing, graphics,
input, audio. Decide deliberately and record the reasoning in the platform spec.

For each candidate ask:

- **Does it build on this toolchain?** Prove it first. Cross-compilation and
  console toolchains break libraries that build fine elsewhere.
- **What does it replace?** If everything around it is already native, a library
  that saves a hundred lines but adds a submodule and a cross-compile step is a
  bad trade — that is exactly why the Win32 platform creates its own window.
- **Source or binary?** Source goes in `external/` as a submodule. A binary is a
  pinned release, fetched at configure time and **checksum-verified** — never an
  unpinned download.
- **Does it allocate?** It must not bypass the platform memory contract for
  anything the engine has to account for.
- **How does it fail?** Anything that aborts or throws on its own terms
  conflicts with the panic path.
- **Is it confined?** Its types must not appear in `engine/include`. If they
  would, wrap it.

Record the decision — including "we wrote our own, because X" — in the platform
spec. A future platform will face the same question.

## 4. Implement

**Implement the platform completely, then integrate.** A platform that is wired
into the build before its contract is fully implemented produces failures in
shared code that look like engine bugs.

1. **The platform contract, in full.** Every key, every capability, every
   method. Stub nothing silently: a capability the platform lacks returns a
   negative answer and says so through `HasCapability` — it never emulates one
   device as another.
2. **The renderers**, in order of how much they prove. Bring up the simplest
   path that gets pixels on screen first; add the preferred backend against it as
   a known-good reference. The null backend must be constructible and must
   terminate the fallback chain.
3. **The cook list**, so assets bake the way this hardware wants them. Cooked
   output is per platform by design.
4. **Build integration**: `platform.cmake`, the known-platform entry, the
   toolchain, and a self-contained `dist/<platform>/`.
5. **Specs updated** to match what was actually built, including what is missing.

### Rules that exist because they were broken here

- **A platform that cannot answer must not guess.** Three layers enforce this,
  and a new platform meets all three:
  1. `PlatformContract.cpp` fails the build if a required constant is undefined,
     or if the values contradict each other.
  2. `GetConstant` and `HasCapability` carry no `default:` label — list `Count`
     and put the fallback after the switch, so `-Wswitch -Werror` refuses a
     platform that forgets a key.
  3. Reaching that fallback panics, naming the key. It never returns a value.

  All three exist because one missing case silently returned `0`, which was read
  as a texture budget of zero and rejected every texture load on that platform.
- **Answer capabilities honestly.** A capability reported wrongly is worse than
  one reported absent: the window was needlessly fixed-size for a whole migration
  because a capability said `false` while the code supported `true`.
- **Aligned allocations may come from a different heap than the C allocator.**
  Whatever your `Alloc` uses, your `Free` must match it.
- **Panic must not return**, and should say something a person can act on.
- **Memory is reserved before any renderer is constructed** — backends take
  their staging storage as they are built.
- **Debug combinations are yours to name.** Answer with buttons your pad has;
  a combination copied from a fuller pad is unpressable, and looks like broken
  tooling rather than a missing button. If a device bridge is involved, the
  bridge must cover every button too.
- **A new backend must stage the built-in primitives when it is constructed**,
  from that same storage. Skip it and the backend still draws level and model
  geometry, so it looks alive — while every primitive the game submits is
  silently discarded. See [RENDERER.md](../subsystems/RENDERER.md).

## 5. Verify

- The platform builds clean under `-Wall -Wextra -Werror`, alongside every
  existing platform.
- `dist/<platform>/` runs **by copying the folder alone** — no reference back
  into the build tree or another platform's bundle.
- Each renderer runs and produces the same frame; a difference means one is wrong.
- `--renderer null` runs headless.
- An unsupported renderer name reports the backends **this** platform has.
- The performance snapshot prints sane timing, memory and budget figures, and
  names the active platform and renderer. It is the best single smoke test here.
  Record what it reports as the spec's *Baseline* — dated, naming the scene,
  the renderer and whether it came from hardware or an emulator — and run
  [PERFORMANCE_REVIEW.md](PERFORMANCE_REVIEW.md) in full against the new
  platform: it is a new clock, a new scheduler, a new cost per log line and a
  new binding constraint, and each is a place a rule that held everywhere else
  can stop holding.
- A level loads and unloads cleanly — that exercises the sector-streaming and
  model release paths, which little else reaches.
- **Every existing example under `examples/` builds and runs on the new
  platform** — see [NEW_EXAMPLE.md](NEW_EXAMPLE.md). A platform that has only
  ever run the one game binary has only been proven once; the examples suite
  is a second, independent smoke test across everything each one isolates.
- Existing platforms still build and run unchanged.

## Definition of done

- [ ] Platform spec, build doc and one spec per renderer exist
- [ ] Listed in `docs/PLATFORMS.md`
- [ ] Every constant key and capability answered; no `default:` in either switch
- [ ] Cook list present and validated by `tools/validate_cooked.py`
- [ ] Self-contained distribution
- [ ] Null backend constructible and last in the fallback chain
- [ ] Every existing example under `examples/` builds and runs on it
- [ ] Known limitations written down, not left to be discovered
- [ ] The platform spec's *Performance* section states all nine items in
      [PERFORMANCE.md](PERFORMANCE.md), with a dated baseline that names its
      source
- [ ] [PERFORMANCE_REVIEW.md](PERFORMANCE_REVIEW.md) run on the platform, its
      findings recorded
- [ ] No shared engine file changed beyond the registration
- [ ] `.github/copilot-instructions.md` updated if it changed a convention
