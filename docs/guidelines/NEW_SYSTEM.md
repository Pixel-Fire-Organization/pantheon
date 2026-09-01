# Guideline — adding a system

A *system* is an engine subsystem: memory, IO, archive, resource, level, sector,
input, debug, renderer. This is the order to build one in, and what "finished"
means.

Follow it in order. Each step exists because skipping it has cost this project
real debugging time, and the notes below say which.

---

## 1. Write the spec first

**Before any code.** Create `docs/subsystems/<NAME>.md` from the template every
other subsystem spec follows:

| Section | What goes in it |
|---|---|
| Purpose | Why this system exists at all. One paragraph. |
| Contract | What it guarantees to callers, stated so a second implementation could satisfy it |
| Depends on | Every system it uses, **by name** |
| Depended on by | Every system that will use it, **by name** |
| Lifecycle | When it starts, what it needs each frame, when it stops |
| When not loaded | What the engine does without it |
| Failure modes | Every way it fails, and what the caller sees |
| Limits | What it deliberately does not do |

**Write it abstractly.** Contracts, states, guarantees, failure modes. No code
excerpts, no function signatures, no paths into `engine/src`. A spec should stay
true across a refactor that preserves behaviour — that is the property that makes
it worth reading later.

Two sections do real work and are worth dwelling on:

- **Depends on / depended on by** determine the bring-up order and the dependency
  table in `EngineSubsystems.h`. Getting them wrong surfaces as a startup panic
  at best and a use-before-init at worst.
- **When not loaded** is what makes the system genuinely optional rather than
  merely listed. If you cannot answer it, the system is not optional and does not
  belong in the subsystem list.

Have the spec reviewed before continuing. It is far cheaper to move a dependency
line than a dependency.

## 2. Decompose the spec into components and tasks

Turn the spec into a list where **every item traces back to a line in the spec**.
Anything with no spec line behind it is either scope creep or a gap in the spec —
resolve which before building it.

A system in this engine almost always needs:

- [ ] An entry in `EngineSubsystem` (`../../engine/include/core/EngineSubsystems.h`)
- [ ] Its dependency rows in that file's dependency table
- [ ] An `Init` / `Shutdown` pair, called from `Engine_Init` / `Engine_Close`
      in dependency order
- [ ] A per-frame update, if it has one, called from `Engine_Update`
- [ ] Enumerated keys for anything looked up by name — never strings or bare
      indices
- [ ] Guards so every public entry point behaves correctly when the system is not
      loaded
- [ ] Fixed-capacity storage, sized from a platform constant
- [ ] A per-frame cost bounded by that constant, with no allocation, growth or
      per-frame diagnostic on the frame path — the rules are
      [PERFORMANCE.md](PERFORMANCE.md), and the spec's Limits section states
      the bound
- [ ] A failure path for each entry in the spec's failure-modes section
- [ ] Platform contract additions, if it needs anything from the OS
- [ ] Tests, if it has a tool-side or format-side component
- [ ] An example under `examples/` demonstrating the system in isolation —
      see [NEW_EXAMPLE.md](NEW_EXAMPLE.md). A subsystem with no example is a
      subsystem nobody can exercise without a whole game running around it.

Write the list down. It is the review checklist in step 4.

## 3. Evaluate off-the-shelf components

Ask explicitly, and record the answer in the spec. "We wrote our own" with no
stated reason is how a project accumulates code nobody can justify.

**This engine rules most libraries out by construction.** Check every one of
these before considering a dependency:

| Constraint | Consequence |
|---|---|
| C++11, no exceptions, no RTTI | Most modern C++ libraries will not compile |
| No STL containers, no `std::string` | Anything with a `std::vector` in its API is out |
| Fixed budgets, no growth | A library that allocates on demand cannot be bounded |
| Two toolchains, one of them a console cross-compiler | It must build for both, or be per platform |
| Constrained target, no virtual memory | Footprint is a hard limit, not a preference |

If a candidate survives, assess the risk honestly:

- **Build risk** — does it build on the *constrained* toolchain? Prove it before
  designing around it. The vector-microcode assembler in this repo faults on its
  own inputs; a dependency's build breaking is not hypothetical here.
- **Footprint** — measure, do not estimate.
- **Failure behaviour** — does it abort, throw, or return? Anything that
  terminates on its own terms conflicts with the engine's panic path.
- **Allocation** — does it call the system allocator? It must go through the
  platform memory contract or be confined to one platform.
- **Vendoring** — source in `external/` as a submodule, or a pinned prebuilt
  fetched at configure time and checksum-verified. Never an unpinned download.
- **Exit cost** — if it has to be removed later, how much of the engine goes
  with it? A previous scripting VM and graphics library were both removed from
  this engine; the cost was proportional to how far their types had spread.

**Prefer a small, owned implementation over a dependency whose API leaks into
shared code.** If a library is used, wrap it so its types never appear in
`engine/include`.

## 4. Implement

**If the system needs anything from the OS or hardware, do the platform work
first — for every platform — then the engine side.** A system built against one
platform and ported later encodes that platform's assumptions in its contract,
and they are expensive to remove afterwards.

Order:

1. **Extend the platform contract** if needed. Add to `Platform` (or a dedicated
   contract interface, as memory has), and implement it on **every** platform
   before writing engine code against it. A half-implemented contract compiles
   and fails at runtime on the platform you were not testing.
2. **Implement the system** in `engine/src/`, against the contract only. No OS
   calls, no `#ifdef PLATFORM_*`, no platform vocabulary — see the NEVER DO list.
3. **Register it**: subsystem enum, dependency rows, bring-up and teardown.
4. **Wire the game surface** if it has one, in `GameAPI.h`.

### Rules that exist because they were broken here

- **Exhaustive switches over enum keys carry no `default:`.** List `Count`
  explicitly and put the fallback after the switch. A `default:` once turned a
  missing platform constant into a silent `0`, which rejected every texture load
  on that platform with a nonsense message.
- **Pair every allocation with its own allocator's release.** Platform memory
  through the platform contract, `malloc` through `free`. Prefer `PlatformArray<T>`,
  which carries the release with the pointer.
- **Panics never return.** Do not write recovery code after one.
- **Report refusals.** A guard that returns `false` with no log is
  indistinguishable from success at a call site that ignores the return.

## 5. Verify

- Both PS2 variants and Win32 build clean under `-Wall -Wextra -Werror`.
- The system runs with its dependencies present.
- **The system is omitted from the subsystem list and the engine still runs** —
  this is the test that proves "when not loaded" is real.
- A configuration requesting it without a dependency panics naming both.
- Each failure mode in the spec is reachable and produces the message the spec
  describes.
- `python3 -m pytest tools/tests -q` passes.

## Definition of done

- [ ] Spec exists, is abstract, and matches the built behaviour
- [ ] Listed in `docs/PLATFORMS.md`
- [ ] Dependencies stated in the spec and in the dependency table, and they agree
- [ ] Behaves correctly when not loaded
- [ ] Every spec failure mode implemented and reachable
- [ ] Per-frame cost bounded by a platform constant and visible in the
      snapshot where it has one; nothing allocates on the frame path
- [ ] An example under `examples/` demonstrates it, in isolation
- [ ] Builds clean on every platform
- [ ] `.github/copilot-instructions.md` updated if it changed a convention
- [ ] No rationale comment in the code that duplicates the spec
