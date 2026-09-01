# Subsystem — Testbed

## Purpose

Exercise, live, exactly what a real running game is doing — on the real
hardware, from inside the shipping binary. This engine targets five binaries
across three very different machines, and most of what breaks between them is
not visible from a log: a stretched aspect ratio, a pad button that never
arrives, a touch surface reporting nothing, a memory figure that only looks
wrong once something else has actually allocated against it. Those need to
be looked at, on the device, against real state, while the game is actually
running.

That is a narrower purpose than this subsystem once had. Capability
showcases — a scene whose whole point is display, not live introspection —
now belong in [`examples/`](../EXAMPLES.md) instead, each its own standalone
binary, isolated from every other scene's state. Building a separate program
per capability would test a different program than the one that ships, and
that argument still holds for everything left in this catalogue: raw
hardware state, and the engine's own live occupancy and timing figures,
mean nothing decoupled from a real game actually running. It does not hold
for a scene whose content is the same whether or not anything else is going
on — that scene was never testing "the one that ships" any more precisely
than a standalone binary would, and duplicating it in both places only
doubles the maintenance for no isolation gained. See
[EXAMPLES.md](../EXAMPLES.md) for what moved and
[backlog/testbed_examples.md](../backlog/testbed_examples.md) for what's
still to move.

What remains here is being reworked from passive display into instruments —
scenes you act on (force an allocation, load an arbitrary resource by path,
trigger a reset) rather than scenes that only render a snapshot, since that
is what a *debugging* tool does that a showcase does not.

## Contract

**It exists only in a debug configuration.** Not disabled at run time — absent.
The scenes, their strings and the catalogue are not in a release binary at all,
and the chord that would open it does nothing. Which configuration is being
built decides which sources are compiled, exactly as which platform is being
built decides that; neither is expressed as a conditional inside shared code.

**The game does not know it exists.** It is not on the game's subsystem list,
needs no cooperation from game code, and cannot be requested or refused by it.
Where it is compiled in, the engine turns it and the interface on itself. A game
written against the engine gets the testbed for free in debug and loses nothing
in release.

**It is reached through [Action](ACTION.md)'s reserved `EngineDebugMenu`
action, from anywhere.** Checked exclusively through Action, the same as
everything else that reads input in this engine — never `Input` or a
platform chord directly, and exempt from context suppression so it stays
reachable no matter what a game has pushed. The same action closes it.

**The catalogue is grouped, and presented as a grouped list.** Scenes are
declared with a category, and the menu draws a heading per category with that
category's scenes beneath it. A flat list of twenty entries is not navigable on
a television at three metres.

**Every transition resets the engine's runtime state.** Opening the menu,
entering a scene, leaving it, and closing the menu back to the game all pass
through the same reset. This is the point of the subsystem, not an
implementation detail: a scene that inherited whatever the previous scene left
resident would measure that instead of the thing under test, and the memory
figures — the most useful numbers here — would be meaningless. Returning to the
game therefore re-runs game initialisation, and the game restarts from the
beginning rather than resuming.

**One scene runs at a time, and the game does not run while it does.** A scene
has the frame to itself. Nothing else is submitting draw calls, allocating, or
reading input, so what a scene reports is attributable to the scene.

**Scenes are gated on capability, never on platform identity.** A scene for a
device this platform does not have still appears in the catalogue and says the
device is unavailable. Hiding it would make an absent capability and an absent
scene indistinguishable, which is the opposite of what a testbed is for — and
the honest answer is itself the thing being tested on four of the five binaries.

**A scene fits the interface budget.** The quad ceiling is a real constraint on
the constrained platform, not a formality. A scene that does not fit is paged by
its author; the interface reports the overflow once per frame, and that report
is a defect in the scene.

## Depends on

- [UI](UI.md) — every scene's entire presentation and interaction.
- [Action](ACTION.md) — the open/close trigger, through the reserved
  `EngineDebugMenu` action. The only way this subsystem checks for input.
- [Input](INPUT.md) — everything the raw hardware-diagnostic scenes (gamepad,
  keyboard/mouse, the platform's chord table) report; these read it directly,
  deliberately bypassing Action, since their purpose is showing state beneath
  any binding layer.
- [Debug](DEBUG.md) — logging, and the platform chord table those same
  diagnostic scenes read.
- [Renderer](RENDERER.md) — submission, and the per-frame statistics scenes read.
- [Memory](MEMORY.md) — the runtime reset, and the occupancy figures scenes read.

Scenes additionally read [Resource](RESOURCE.md), [Level](LEVEL.md),
[Sector](SECTOR.md) and [Achievement](ACHIEVEMENT.md) where those are running,
and report them as absent where they are not.

## Depended on by

Nothing. It is a leaf, deliberately: anything depending on it would not build in
release.

## Lifecycle

Brought up after the interface it draws through, and torn down before it. It
holds no resources of its own — a scene borrows the engine's, and the reset on
every transition gives them back. Each frame it either draws the menu, or runs
the one active scene, or does neither and lets the game run.

## When not loaded

The engine runs the game and nothing else. The reserved `EngineDebugMenu`
action still resolves, and anything else consuming it would still see it
fire; nothing opens. This is precisely what a release build is, so the
unloaded path is the one that ships and is exercised on every release run
rather than being a configuration nobody tries.

## Failure modes

- **A scene overruns the interface budget** — the interface truncates and
  reports it once per frame. The scene is at fault, and the report names the
  shortfall.
- **A scene needs a subsystem that is not running** — it says so and draws
  nothing else. It never reports a zero, because a zero from an absent subsystem
  is indistinguishable from a real one.
- **The reset cannot restart a subsystem** — this is a programmer error in the
  reset, not a recoverable condition, and it panics naming the subsystem.
  Continuing would run every later scene against a half-built engine and blame
  the scenes.
- **The reserved `EngineDebugMenu` action has no viable binding on a
  platform** — prevented at compile time (`tools/actions.py`), the same as
  any other action; not a runtime condition this subsystem needs to detect.
  A guard remains at startup regardless, naming the fault if it is ever
  reached by a mismatched generated table.

## Limits

- **It is not a test runner.** Nothing asserts, nothing passes or fails, and
  nothing is automated. A scene presents what the engine is doing and a person
  decides whether it is right. What "right" looks like per platform belongs in
  that platform's build documentation, not in the scene.
- **Nothing is persisted.** No results, no history, no selected scene across
  runs. The reset is total, in both directions.
- **One scene at a time**, and no scene may open another. The menu is the only
  route between them.
- **It does not inject failures.** Nothing here deliberately overruns a budget,
  corrupts a resource, or panics. A diagnostic that ends the session cannot be
  used to diagnose the next thing.
- **It cannot test the release configuration**, being absent from it. The
  release build is verified by running the game.
