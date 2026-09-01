# Subsystem — Scene

## Purpose

Give the engine one thing to run at a time, with a known shape, instead of a
single flat per-frame callback the game hand-rolls its own state machine
inside. A scene is a compile-time C++ type the engine constructs, starts,
updates and stops directly — the engine decides when a transition happens and
drives the loading screen while one is in flight, rather than the game
deciding for itself when it is safe to swap out its own state.

## Contract

**A scene is a C++ object, switched by reference, never by name or index.** A
game registers a scene by handing the engine a live `game::Scene*`; there is
no string table and no integer catalogue to keep in sync, so a reference to a
scene that does not exist cannot compile, let alone run.

**Three shapes share one lifecycle.** `game::Scene` is the base contract (`OnStart` / `OnUpdate` / `OnStop`).
`game::LevelScene` and `game::UiScene`
layer a **fixed** resource and render pipeline over it — a level scene loads
its level, keeps the streaming centre and resident sector ring current, and
submits the level's geometry every frame without the game doing any of that
by hand; a UI scene guarantees its declared resources are attached before its
drawing hook ever runs. `game::FreeFlowScene` (or `game::Scene` directly)
opts out of both: the game loads, unloads and renders entirely at will, the
same freedom `GameUpdate` has always had.

**Only one scene is loaded at a time.** Switching to a new scene always stops
the current one first. There is no cross-fade and no second scene resident in
the background.

**A scene declares the resources it uses; the engine decides how to get them
resident.** A scene's declared list is attempted eagerly: if every entry
loads, the engine waits for them all to become ready before starting the
scene — the scene never sees a partially-loaded declared resource. If any
entry cannot be loaded (the resource table or its budget refuses it — see
[Resource](RESOURCE.md), which already fails these loudly rather than
degrading silently), the engine releases whatever it acquired and starts the
scene immediately; from that point the scene is responsible for loading what
it needs itself, the same way world geometry already streams in
[Level](LEVEL.md)/[Sector](SECTOR.md). Declaring no resources is legal and
common — a scene with nothing to preload starts immediately either way.

**Switching reuses the engine's own runtime reset.** A transition — including
a reload — always runs: the outgoing scene's `OnStop()`, then the same
runtime reset every other "between scenes" transition in this engine already
uses (see [Memory](MEMORY.md), and [UI](UI.md) which already documents being
reset "between scenes"), then the declared-resource attempt for the incoming
scene, then its `OnStart()`. Every resource is dropped by that reset,
pinned ones included, so nothing is assumed to survive a switch — including
the loading screen's own images, which are reloaded fresh each time.

**A scene can be reloaded in place.** Reloading the active scene runs the
same `OnStop()` → reset → reattach → `OnStart()` sequence against itself,
so whatever it mounted or loaded is fully torn down and rebuilt rather than
resumed. The scene's own member state is not touched by the engine; a scene
that wants a clean slate resets its own fields in `OnStart()`, exactly as
`GameInit()` already resets the game's globals today.

**Loading progress is observable, and drawable.** While a scene's declared
resources are outstanding, a query reports the fraction ready, and a built-in
loading screen — a predefined, declared set of images and a progress meter —
draws automatically. A scene may draw its own content on top of it every
loading frame; submission order alone puts it above the built-in drawing,
no special layer is needed.

## Depends on

- [Resource](RESOURCE.md) — every declared resource attempt.
- [Renderer](RENDERER.md) — a level scene's geometry submission.
- [Memory](MEMORY.md) — the runtime reset every switch performs.

**Optionally, and never as a dependency row** (mirroring how
[Testbed](TESTBED.md) reads subsystems it does not require): a `LevelScene`
needs [Level](LEVEL.md) enabled and refuses to start, loudly, without it; a
`UiScene`, or the built-in loading screen's own drawing, needs [UI](UI.md)
enabled — without it, loading still completes and scenes still run, they are
simply not drawn.

## Depended on by

Game code — this is the primary way a game is now structured. Nothing in the
engine depends on it: an engine with no scene registered simply keeps calling
`GameUpdate(dt)` exactly as it did before this subsystem existed.

## Lifecycle

Brought up after Resource, since every transition attempts declared
resources immediately. Holds no scene until the game registers one; nothing
runs differently before that beyond `GameUpdate(dt)` continuing to be called
directly. Each frame it either drives the loading screen (declared resources
still outstanding) or the active scene's `OnUpdate`. Shutdown stops whatever
scene is active and forgets it; it does not unload anything itself; the
reset that already runs on every transition is what returns resources.

## When not loaded

No scene can be registered or switched to; the engine calls `GameUpdate(dt)`
every frame exactly as before this subsystem existed. A game that never
registers a scene, whether or not the subsystem is enabled, sees identical
behaviour either way — registering the first scene is what turns this on.

## Failure modes

- **A declared resource cannot be loaded** — logged by Resource itself (table full or over budget), the engine releases
  whatever else it
  eagerly acquired for that scene and starts the scene immediately rather
  than waiting; the scene streams what it needs from then on.
- **A `LevelScene` starts with `Level` not enabled** — logged, and the scene
  does not start. This is a configuration mistake, not a runtime condition to
  route around: a level scene with nowhere to load a level from cannot do its
  job.
- **`ReloadScene()` with no active scene** — a no-op, logged once; there is
  nothing to reload.
- **`SwitchScene()` to the scene already active** — a no-op; use
  `ReloadScene()` to force the same scene to tear down and rebuild.

## Limits

- One scene resident at a time; there is no scene stack and no scene may
  open another directly — switching is the only transition.
- A scene's declared resource list has a fixed capacity (`SCENE_MAX_RESOURCES`, a platform constant); it is sized for a
  handful of
  extras a scene wants preloaded, not a level's whole asset list.
- The eager/stream decision is made once, at the moment a scene is entered.
  A scene that later wants more does its own loading from then on, same as
  today.
- The built-in loading screen is images and a progress meter; it is not a
  general cutscene or animation system.
