# Guideline — adding an example

An *example* is a standalone binary under `examples/` that exercises one
engine capability, or a small composite of them, in total isolation: its own
process, its own memory, its own runtime-reset boundary. This is what makes it
different from a Testbed scene — a Testbed scene shares a process with
nineteen others and the game itself; an example shares nothing with anything.
See [subsystems/TESTBED.md](../subsystems/TESTBED.md) for why that isolation
is worth a whole second binary per example, and [EXAMPLES.md](../EXAMPLES.md)
for the catalogue this guideline grows.

---

## 1. Decide where it belongs

Three places look similar. They are not interchangeable:

| | Purpose | Lives |
|---|---|---|
| `game/` | The one title this engine ships | One tree, one binary per platform |
| `examples/<name>/` | One capability, isolated, always runnable on its own | One tree per example, one binary per platform per example |
| `engine/src/debug/` (Testbed) | Live diagnostics that only make sense **inside a running game** | One catalogue entry, shares the debug binary's process with everything else |

If what you're building can be looked at without a game running around it —
a rendering technique, a widget set, a format, a subsystem's happy path — it
is an example. If it only means something while observing real game state
(current memory occupancy, current resident sectors, current hardware input
under whatever the game just pushed) it belongs in Testbed instead. When in
doubt: an example that needs the game's own state to be interesting is a
sign it should not be an example.

## 2. Decompose

Every example needs:

- [ ] `examples/<name>/config/title.json` — its own identity (distinct
      `ids.ps2`/`ids.vita`/`ids.psp`, per `tools/schemas/title.schema.json`;
      nx needs no id, and packages the example under its `name`);
      title identity drives save-path and packaging on every platform, so no
      two examples — and no example and the game — may share one.
- [ ] `examples/<name>/src/Game.cpp` — `GameConfigure`/`GameInit`/
      `GameUpdate` against `engine/include/GameAPI.h`, exactly as `game/`
      has. Request only the subsystems the example actually needs;
      `Memory`/`Debug`/`Renderer`/`Input`/`Action` are never optional (see
      `docs/ENGINE.md`'s subsystem table) but everything else should be
      named deliberately.
- [ ] One or more `game::Scene` subclasses (`game::Scene`,
      `game::FreeFlowScene`, `game::UiScene` or `game::LevelScene` — see
      `docs/subsystems/SCENE.md`) registered via `game::SetMainScene` from
      `GameInit()`.
- [ ] A CMake registration in `examples/CMakeLists.txt` (see §4).
- [ ] Reuses the **shared** `examples/config/actions.json`,
      `achievements.json` and (unless the example is specifically about
      theming) `game/config/theme.json` — see §3. Only `title.json`, and a
      `config/platform/<name>/package.json` where the example is packaged
      for a platform that needs one, are per-example.

Anything with no line above behind it does not belong in a first pass —
extend this list before adding it, the same discipline
[NEW_SYSTEM.md](NEW_SYSTEM.md) asks of a subsystem.

## 3. Shared example config

Adding an example should mean writing `Game.cpp` and a handful of `Scene`
subclasses, not re-declaring a whole title's configuration. `examples/config/`
holds what every example shares:

- **`actions.json`** — the mandatory `EngineDebugPerfSnapshot`/
  `EngineDebugOverlayToggle`/`EngineDebugMenu` trio (in that order, per
  `.github/copilot-instructions.md`), plus the `CycleNext`/`CyclePrev`
  digital pair every multi-scene example binds to move between its internal
  scenes (§5). Extend this file — never fork a per-example copy — if a new
  example genuinely needs an action the shared set doesn't have yet, and
  prefer widening it in place over the fork.
- **`achievements.json`** — empty (`{"achievements": []}`). Examples don't
  earn trophies.
- **`game/config/theme.json`** — reused unmodified. An example about theming
  itself is the one case that authors its own.
- **`platform/vita/`** — one shared placeholder icon/livearea set, reused by
  every example packaged for Vita, with `"trophies": {"enabled": false}`
  (see `game/config/platform/vita/package.json`) since the shared
  achievements declaration is empty.

## 4. CMake registration

`examples/CMakeLists.txt` generalizes `game/CMakeLists.txt`'s
`game_add_cook_target`/`game_add_action_table`/`game_add_executable` into
`example_add_cook_target(PLATFORM NAME)` /
`example_add_executable(PLATFORM NAME SOURCES CONFIG_DIR OUT_TARGET
OUT_DIST_DIR)`. Registering a new example is adding its name and source list
to the example list at the top of that file — the per-platform loop in the
root `CMakeLists.txt` instantiates it for every active platform
automatically, the same way a new platform is one directory rather than a
loop edit.

Artefacts land under `examples/dist/<name>/<platform>/` — a tree separate
from the game's own `dist/<platform>/`, since an example is a dev-tooling
concern, not something that ships. `cmake --build . --target examples`
builds every example for every active platform; `cmake --build . --target
dist` (the shipping path) never touches `examples/`.

## 5. Multi-scene examples

A single static page under-uses the isolation an example buys. Where a
capability has more than one aspect worth looking at, give the example
several real `game::Scene` instances and cycle between them with the shared
`CycleNext`/`CyclePrev` actions (§3) driving `game::SwitchScene` — this
exercises the Scene subsystem's own switching as a side effect, which is
itself worth proving on every example that does it. Reach for a single-scene
example only when the capability genuinely has one aspect.

## 6. Fix issues as you find them

Building an example in isolation is expected to surface engine defects that
a shared, contaminated Testbed process was masking — that is the entire
premise of this directory existing. **Fix the defect as part of the same
change that added or touched the example — never file it away.** Then append
an entry to [`docs/fixed_issues/issues.json`](../fixed_issues/issues.json)
(schema: `tools/schemas/fixed_issues.schema.json`) recording what broke, why,
what changed, and how to tell if it comes back. If the same root cause
resurfaces later, update that entry rather than adding a duplicate.

## 7. Verify

- The example's binary runs standalone, on every platform it targets, with
  no debug-menu chord and no other example or the game running.
- A multi-scene example's `CycleNext`/`CyclePrev` reaches every scene and
  wraps or clamps sensibly at the ends.
- `cmake --build . --target dist` (the game) is unaffected.
- `python3 -m pytest tools/tests -q` passes.
- If the example migrated content out of Testbed, that content's Testbed
  catalogue entry, source file and declarations are gone — moved, not
  duplicated.

## Definition of done

- [ ] `examples/<name>/config/title.json` exists with a distinct identity
- [ ] `Game.cpp` requests only the subsystems it needs
- [ ] At least one `game::Scene`; more than one where the capability has more
      than one aspect worth showing, cycled via `CycleNext`/`CyclePrev`
- [ ] Registered in `examples/CMakeLists.txt`; builds for every active
      platform via the `examples` target
- [ ] Listed in [EXAMPLES.md](../EXAMPLES.md)
- [ ] Any engine defect found while building it is fixed in the same change
      and logged in `docs/fixed_issues/issues.json`
- [ ] If migrated from Testbed: the old scene is gone from
      `engine/src/debug/`, not duplicated
