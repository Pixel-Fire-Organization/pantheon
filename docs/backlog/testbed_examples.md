# Backlog — examples and the Testbed migration

Working notes for future sessions, not a commitment made in the change that
added them. First of what's expected to be several files under
`docs/backlog/` — each backlog file tracks one initiative's remaining work,
rather than one growing list for everything.

Companion to [EXAMPLES.md](../EXAMPLES.md) and
[subsystems/TESTBED.md](../subsystems/TESTBED.md). Pick items up via
[guidelines/NEW_EXAMPLE.md](../guidelines/NEW_EXAMPLE.md).

## Remaining examples

From the original list of what this directory should eventually cover, not
yet built:

| Example | Source |
|---|---|
| `textures` | New — or split out of `primitives`' textured scene; also worth exercising the cook pipeline end to end (source art -> `.ps2a` -> resource load), which no current example does |
| `ui_layout` (layout showcase) | New — `ui_gallery` already covers widgets, theming and budget; a dedicated layout-composition example (panels, containers, responsive sizing) is the remaining piece |
| `levels` | Migrate `engine/src/debug/scenes/SceneLevelStream.cpp` into a real `game::LevelScene` |
| `action_system` | New — Testbed's device scenes (`Gamepad`/`KeyboardMouse`/`Chords`) deliberately bypass Action to show raw hardware state, so there's no existing scene to migrate; author this one fresh against `game/config/actions.json`'s grammar (contexts, composites, rebinding) |
| `scene` (the Scene subsystem itself) | New — the one place switching between several unrelated sub-scenes in one process is the point rather than a contamination risk, since it's demonstrating the switch/loading-screen mechanism itself |

## Remaining Testbed scenes

Every scene not yet migrated or reworked, with a recommendation and why:

| Scene | Recommendation | Why |
|---|---|---|
| `Gamepad`, `KeyboardMouse`, `Touch`, `Pointer`, `Chords` | Stay, light rework | Already close to tool-like — they show live hardware state, which only means something while a real device is being poked |
| `PlatformInfo`, `Performance`, `FramePacing` | Stay | Inherently about the running process's own numbers |
| `AssetBrowser` | Stay — **correction, was previously planned to migrate** | An earlier pass of this backlog listed `asset_and_level_browser` as a future example; that was a mistake. What it browses is this build's own mounted archive and the live Resource Manager's table and texture budget — the running process's actual state, the same reason `Memory`/`Resources` stay, not a static showcase. It already acts on that state (pick an archive, browse entries, load and preview an asset) rather than only rendering it. |
| `ScreenAspect`, `DepthRange` | Migrate to `examples/` | Rendering/display showcases, not live diagnostics — nothing about them needs a real game running |
| `Achievements` | Migrate | Fold into whichever example ends up demonstrating the Achievement subsystem |

Already handled, for reference:

| Scene | What happened |
|---|---|
| `Primitives`, `DrawLoad` | Moved to `examples/primitives` |
| `UiGallery`, `Style`, `UiBudget` | Moved to `examples/ui_gallery` |
| `Memory` | Reworked in place into a tool: a TOOLS section targets one of CONFIG/LEVEL DATA/MAIN POOL and forces an allocation, a free (pool only) or a reset against it, showing the before/after byte delta. Arena allocation goes through `Engine_LoadToSlot` (a reserved-but-empty slot), not the unused `Engine_AddToArena` bump path, whose bytes `Engine_GetArenaStats` cannot see — the renderer arena is deliberately excluded from the tool since it is never emptied while a renderer is live. |
| `Resources` | Reworked in place into a tool: a free-form path field and type selector drive `Engine_Resource_Load`/`LoadAuto`, and the live table is selectable — picking a row inspects it (key, type, state, refs, dims) and offers PIN/UNPIN/UNLOAD, replacing the old four hardcoded texture slots. |
