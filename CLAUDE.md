# CLAUDE.md

Claude Code loads this file automatically every session. Keep it short.

## Read first, always

**Read [.github/copilot-instructions.md](.github/copilot-instructions.md) at
the start of every session, before anything else — unconditionally, not just
for non-trivial changes.** It is the single source of truth for build system,
toolchain, submodule rules, memory/resource philosophy, constants standard, and
the "NEVER DO" list. Claude Code does not auto-load it the way it auto-loads
this file, so that read is on you, every session. It also carries the platform
subsystem rules and the engine/game boundary.

## Specs are authoritative

**Read the relevant spec under [docs/](docs/) before implementing, planning, or
answering a question** about a subsystem, renderer, platform, format or the build
pipeline. Specs are not background reading — they are where this project keeps
the reasoning that used to live in code comments: hardware quirks, race
conditions, renderer limits, budget ceilings. That rationale was deliberately
removed from the source, so working from the code alone means working without it.

- Subsystem → `docs/subsystems/<NAME>.md`
- Renderer → `docs/<platform>/renderers/<NAME>.md`
- Platform → `docs/<platform>/PLATFORM.md`
- On-disc format → `docs/formats/<NAME>.md`
- Build stages → `docs/PIPELINE.md`; engine architecture → `docs/ENGINE.md`

Specs are written abstractly — contracts, states, guarantees, failure modes — so
they stay true across refactors. Keep them that way: no code excerpts, no
function signatures, no paths into `engine/src`.

## Building something new

**Before implementing a new system or a new platform, read the guideline for it
in [docs/guidelines/](docs/guidelines/).** Not while reviewing — before starting.

- New engine subsystem -> [docs/guidelines/NEW_SYSTEM.md](docs/guidelines/NEW_SYSTEM.md)
- New platform (console, desktop OS, or a variant) -> [docs/guidelines/NEW_PLATFORM.md](docs/guidelines/NEW_PLATFORM.md)
- New standalone example -> [docs/guidelines/NEW_EXAMPLE.md](docs/guidelines/NEW_EXAMPLE.md)
- Code that reads on-disc bytes, completes later, touches IO-worker state, hands a buffer to an OS API, or runs in build/CI -> [docs/guidelines/SECURE_CODING.md](docs/guidelines/SECURE_CODING.md)
- Checking the tree has none of that class of defect -> [docs/guidelines/SECURITY_REVIEW.md](docs/guidelines/SECURITY_REVIEW.md)
- Code on the frame path (the loop, geometry staging, a backend's frame, the interface, IO dispatch, sector streaming, or a platform's clock, threads or console) -> [docs/guidelines/PERFORMANCE.md](docs/guidelines/PERFORMANCE.md)
- Checking the tree obeys those rules, and that every platform spec's Performance section matches the platform as built -> [docs/guidelines/PERFORMANCE_REVIEW.md](docs/guidelines/PERFORMANCE_REVIEW.md)

The `NEW_*` guidelines run: spec first, decompose the spec into tasks and components, evaluate
off-the-shelf components and their risk, then implement per platform before
engine-wide. Each ends with a definition of done; work it.

## Language-specific rules

Read before editing:
- C → [.github/instructions/c-expert.instructions.md](.github/instructions/c-expert.instructions.md)
- C++ → [.github/instructions/cpp-expert.instructions.md](.github/instructions/cpp-expert.instructions.md)

## Orientation

[README.md](README.md) for project overview; [docs/PLATFORMS.md](docs/PLATFORMS.md)
indexes every spec.

## Keeping instructions in sync

Conventions/build/rules belong in `copilot-instructions.md` — not here; behaviour
and rationale belong in the specs. Update the instructions file, the matching
C/C++ instructions file for language-standard changes, and any spec the change
invalidates, in the same change, every time — never deferred to a later "docs
pass". This file should almost never need edits; when it does, keep it to
pointers.
