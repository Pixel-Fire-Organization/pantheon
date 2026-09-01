# App API (`EngineApp`)

`EngineApp` is the engine's own shell: it brings the subsystems up, advances a frame, and tears
them down. All engine subsystems (memory arenas, async I/O, resource streaming, rendering) are
driven internally.

**Gameplay is authored against `GameAPI.h`** (`GameConfigure(config)` / `GameInit()` / `GameUpdate(dt)`) — that is the
surface game code should reach for; see `engine/include/GameAPI.h`.

---

## No sandbox — convention, not a fence

Game code once had a curated include fence: `engine/include/app_public/` held forwarding shims and
a CMake option (`ENGINE_SANDBOX_MODE`) restricted the app target's include path to that directory
alone. Both are **gone**.

That fence existed to keep a *scripted* app layer out of engine internals. With Lua removed and
gameplay written natively in C++, the shims were forwarding headers whose entire body was
`#include "../GameAPI.h"`, and the option existed only to make that indirection load-bearing.

Game code now uses the normal engine headers. The rule is unchanged, it is simply a convention
rather than a build error:

> Game code should include `GameAPI.h`. Reaching into `EngineMemory.h`, `EngineIO.h`,
> `EngineResource.h` or the renderer from `game/**` means the API is missing something — extend
> `GameAPI.h` rather than bypassing it.

---

## Input

`GameAPI.h` exposes three separately named device groups, mirroring the platform layer:

| Group | Functions |
| :--- | :--- |
| Gamepad | `IsPadPressed`, `WasPadPressed`, `GetJoyAxis` |
| Keyboard | `IsKeyDown`, `WasKeyPressed` |
| Mouse | `IsMouseButtonDown`, `WasMouseButtonPressed`, `GetMousePosition`, `GetMouseDelta`, `GetMouseWheel` |

All of them compile and run everywhere. On a platform with no keyboard or mouse (the PS2) the corresponding calls
return false/zero rather than pretending. Branch on `game::HasInputDevice("keyboard")`, never on which platform is
running.

Pad-only game code needs no change to be playable on desktop: Win32 maps a default keyboard layout onto virtual pad 0
(see `docs/PLATFORMS.md`).

---

## Action

`GameAPI.h` also exposes player *intent*, resolved once per frame from Input's snapshot — see
[docs/subsystems/ACTION.md](subsystems/ACTION.md). An action is declared once, for every platform, in
`game/config/actions.json`; a candidate binding fires only if its every source exists on the running
hardware, so a title reads `game::ActionHeld(ActionId::Sprint)` and never a raw button next to a
platform check.

| Kind | Functions |
| :--- | :--- |
| Digital | `ActionHeld`, `ActionPressed`, `ActionReleased`, `ActionPressedCount`, `ActionRepeatTick` |
| Analog | `ActionAxis1d`, `ActionAxis2d`, `ActionScalar` |
| Prompts / rebinding | `GetActionPrompt`, `RebindAction`, `RestoreActionDefault`, `SaveActionOverlay` |
| Contexts | `PushActionContext`, `PopActionContext` |

`id` comes from the header generated at build time from `game/config/actions.json`
(`tools/actions.py --emit-ids`), the same precedent as `AchievementId`. UI and Debug keep their own
direct Input reads (see `docs/subsystems/UI.md`, `docs/subsystems/DEBUG.md`) — nothing in the engine
itself depends on Action, and a game that wants raw input without this layer uses the Input-mirroring
functions above directly.

---

## API Reference

### `EngineStart(const char *resourceLocationToken) → bool`

Initialises all engine subsystems (arenas, renderer, input, IO, resources), then calls the game
module's `GameInit()` once.

- `resourceLocationToken` selects the active storage device (`"cdrom0:"`, `"mass0:"`, `"hdd0:"`,
  `"host:"`); pass `NULL` to default to `cdrom0:`.
- Returns `false` if engine init fails.

### `EngineUpdate(void)`

Advances one frame:
1. `Engine_Update()` — timing, async IO/resource pumps.
2. If a `game::Scene` has been registered (see below), the active scene's loading screen or
   `OnUpdate(dt)`; otherwise `GameUpdate(dt)` — the game module's per-frame gameplay + draw
   submission, unchanged from before scenes existed.
3. Renderer `BeginFrame()` / `Render()` / debug overlay / `EndFrame()`.
4. Frame stats reporting + perf logger tick.

Must be called every iteration of the main loop.

### `EngineExited(void) → bool`

Returns `true` when the engine should stop — set by `game::Exit()`.

### `EngineStop(void)`

Shuts down all subsystems and releases all memory. Call once after the main loop exits.

---

## Logging & Panic

`EngineApp.h` includes `EngineDebug.h`, so logging and panic come with it:

```c
void Engine_LogInfo(const char *text, ...);
void Engine_LogError(const char *text, ...);
void Engine_Panic(const char *message);
```

It previously hand-copied these three as `extern` prototypes to avoid pulling in `EngineDebug.h`
"and transitively raylib.h". raylib is gone and so is the sandbox, so both reasons for the
duplicate declarations are void — and a duplicated prototype is a prototype that can drift.

---

## Minimal `main.cpp`

```cpp
#include "EngineApp.h"

int main(void) {
    if (!EngineStart(NULL)) return -1;
    while (!EngineExited()) EngineUpdate();
    EngineStop();
    return 0;
}
```

Gameplay itself lives in a separate game module implementing the `GameAPI.h` entry points:

```cpp
#include "GameAPI.h"

void GameConfigure(EngineConfig* config) { /* choose subsystems - see EngineSubsystems.h */ }
void GameInit() { /* load resources, set initial state */ }
void GameUpdate(float dt) { /* input, animation, game::Draw*, game::SetCamera3D, ... */ }
```

See `game/src/Game.cpp` for a complete example.

---

## Internal Flow

```
EngineStart(token)
  └─ Engine_Init()   — arenas, pool, renderer, pad, IO, resources
  └─ GameConfigure(config)       — choose subsystems, before the engine exists
  └─ GameInit()

EngineUpdate()
  └─ Engine_Update()             — dt, IO/resource pumps
  └─ Engine_Scene_Update(dt)     — if a scene is registered: its loading screen or OnUpdate(dt)
  └─ GameUpdate(dt)              — otherwise: gameplay + draw-list submission, as before scenes
  └─ Renderer BeginFrame/Render/EndFrame
  └─ Engine_ReportFrameStats()
  └─ Engine_PerfLogger_Tick()
```

## Scenes

A game may register a `game::Scene` (`game::SetMainScene`, typically from `GameInit()`) instead of
doing everything in one `GameUpdate(dt)`. Once registered, the engine drives it directly — its
loading screen, then its `OnUpdate` — and `GameUpdate(dt)` is no longer called. `game::LevelScene`
and `game::UiScene` add a fixed resource/render pipeline on top of the base contract; a scene
wanting the same freedom `GameUpdate` always had derives from `game::FreeFlowScene` (or
`game::Scene` directly). See `docs/subsystems/SCENE.md` and `game/src/Game.cpp`.

## Drawing an interface

Game code may draw an interface with the same subsystem the engine's own tooling
uses, when it asks for it. Two things are deliberately kept apart:

- A **screen-space rectangle** goes straight to the renderer. It is the
  primitive for a game running without the interface subsystem, and it is
  therefore neither clipped by a container nor layered above one.
- **Interface calls** go through the subsystem: they are clipped, they take part
  in navigation, and they can be drawn above the world in the overlay layer.

Without the subsystem every interface call is a no-op and every query reports
"not interacted with", so game code compiles and runs either way. That is the
same guarantee the subsystem gives the engine's own tooling.
