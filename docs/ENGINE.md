# Engine architecture

A small, fixed-budget game engine that targets constrained console hardware and
desktop equally. Gameplay is written once against one game-facing surface and
built for a chosen platform; nothing in game code names a platform.

This document covers the shape of the engine. Each subsystem has its own spec
under [subsystems/](subsystems/); each platform under its own directory. The
index is [PLATFORMS.md](PLATFORMS.md).

## Principles

**Fixed budgets, decided before the game runs.** Tables are fixed-capacity, memory
is reserved once at startup, and exceeding a budget is an error to fix rather
than a condition to survive. An engine that grows its tables silently hides a
content problem until it fails on the smallest target — which is the target that
matters.

**Fail loudly and early.** Programmer errors and exhausted budgets stop the
engine with an actionable message. Content errors are logged and rendered
visibly wrong. Neither is silently absorbed.

**The platform owns everything hardware-shaped.** Memory, storage, input, timing,
threading, windowing and available renderers are all platform decisions. Shared
engine code contains no conditional compilation selecting a platform, and no
operating-system calls.

**Ports are additive.** Adding a platform is adding one same-named subdirectory
under each of the platform headers, sources and build config, not editing the
engine.

## Layers

```
        game/**                    gameplay, written against one surface
   -----------------------------
        game API                   the only surface game code uses
   -----------------------------
        engine subsystems          memory, io, archive, resource, level,
                                   sector, input, debug, renderer
   -----------------------------
        platform contract          abstract; one implementation per platform
   -----------------------------
        platform implementation    hardware, os, and that platform's renderers
```

Each layer may use the one below it and no other. The one deliberate exception is
noted in [IO.md](subsystems/IO.md).

## Startup

```
  entry point                   platform-supplied, one per platform
    -> engine main              platform-neutral from here on
         parse arguments
         create the built-in platform
         initialise the platform
         ask the game which subsystems it wants
         reserve memory                       <-- before any renderer exists
         open the window
         build a renderer, falling back on failure
         start the engine and the requested subsystems
         game initialisation
         frame loop
         stop, then shut the platform down
```

Two orderings are load-bearing and have both been got wrong before:

- **Memory is reserved before any renderer is constructed**, because every
  renderer takes its geometry staging from an arena slot as it is built.
- **Subsystems are chosen before the engine starts**, because the engine brings
  up exactly the requested set and asserts their dependencies.

Shutdown reverses the order.

There is also a **runtime reset**, which is neither startup nor shutdown: it
returns the engine to the state it had just after startup — no level, no loaded
resources, only the boot archive mounted, the config and level-data arenas and
the main pool empty — without stopping it. The platform, the memory reservation,
the renderer and the renderer's arena are untouched, because a renderer is built
once for the life of the process and cannot be rebuilt on every platform. It
exists so a diagnostic can exercise one subsystem from a known-clean state
rather than from whatever the previous one left behind. See
[MEMORY.md](subsystems/MEMORY.md).

## Selection

The platform compiled into a binary is fixed at build time, and each platform
produces its own executable and its own distribution — so a build for one target
cannot be launched on another. The **renderer**, by contrast, is genuinely
selectable at launch: every backend a platform supports is compiled into it. That
makes the renderer the primary debugging lever, and comparing two backends
rendering the same frame is the most reliable way to locate a rendering bug.

Arguments are parsed by a general parser, documented in
[COMMAND_LINE.md](COMMAND_LINE.md). Unknown options are retained rather than
rejected, so a game may define its own without touching the engine.

## Subsystems

The game declares which subsystems it wants before the engine starts. The engine
brings up that set in dependency order and refuses a set whose dependencies are
not satisfied, naming both sides.

| Subsystem                          | Purpose                                                   | Optional                             |
|------------------------------------|-----------------------------------------------------------|--------------------------------------|
| [Memory](subsystems/MEMORY.md)     | Arenas, pool, platform allocations                        | No                                   |
| [Debug](subsystems/DEBUG.md)       | Logging, performance snapshot, panic                      | Logging no, snapshot yes             |
| [Renderer](subsystems/RENDERER.md) | Frame submission and drawing                              | No — select the null backend instead |
| [IO](subsystems/IO.md)             | Asynchronous reads                                        | Yes                                  |
| [Archive](subsystems/ARCHIVE.md)   | Container mounting and resolution                         | Yes                                  |
| [Resource](subsystems/RESOURCE.md) | Asset lifetime, handles, eviction                         | Yes                                  |
| [Level](subsystems/LEVEL.md)       | World core, materials, entities                           | Yes                                  |
| [Sector](subsystems/SECTOR.md)     | Streamed world geometry                                   | Yes                                  |
| [Input](subsystems/INPUT.md)       | Gamepad, keyboard, mouse                                  | No — force-enabled                   |
| [Action](subsystems/ACTION.md)     | Player intent, resolved from Input by a declared map      | No — force-enabled                   |
| [Scene](subsystems/SCENE.md)       | One `game::Scene` active at a time, with a loading screen | Yes                                  |

Every optional subsystem must behave correctly when a subsystem it does not
depend on is absent. That is what makes a headless configuration — no
renderer output, no world geometry — genuinely useful for logic tests rather
than merely expressible. Input and Action are force-enabled rather than
optional (see [ACTION.md](subsystems/ACTION.md)), but "no input" is still
reachable for a headless test: the platform under it answers every capability
query negatively, which every Input and Action query already treats
identically to the subsystem being absent.

## Constants

A value is a **format** constant if an on-disc byte layout or a build tool
depends on it; everything else is a **platform** capability.

Format constants live beside the structure they describe and are identical
everywhere — a platform choosing its own would produce files no other platform
could read. Platform constants live with that platform and may differ freely.
This single rule decides where any new constant belongs.

## Keys

Anything a caller looks up by name uses an enumerated key with an explicit
underlying type: platform and renderer identity, platform constants and
capabilities, device buttons and axes, log levels, file modes, subsystems. A
mistyped key is then a compile error rather than a control that silently does the
wrong thing — which is not hypothetical, as the hand-written device masks this
replaced had four buttons transposed and compiled cleanly.

## The game boundary

Game code includes one header and uses one surface, described in
[APP_API.md](APP_API.md). It names no platform, no renderer, and no subsystem
internals. Keeping to that surface is what makes the same gameplay build for
every target.

`examples/` is a second consumer of that same surface, at the same layer as
`game/`: each example is its own `GameConfigure`/`GameInit`/`GameUpdate`
module, built into its own binary per platform, isolating one capability (or
a small composite) from every other. See [EXAMPLES.md](EXAMPLES.md).

## Build

Compiling the engine, compiling the game, cooking assets, packaging them and
producing a distribution are five distinct stages with distinct artefacts. See
[PIPELINE.md](PIPELINE.md).
