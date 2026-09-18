# Pantheon - AI Instructions

This file provides context for AI assistants to understand the unique requirements of this multi-platform game engine
project.

## Project Overview

This is a custom multi-platform game engine (Pantheon) using platform SDKs such as `ps2sdk`, `ps2gl`, and `ps2stuff`.

## Platform Subsystem

The engine targets multiple platforms through one abstract interface — the same pattern as `Renderer`.

- **Interface**: `engine/include/platform/Platform.h` (pure abstract, header-only). Shared code reaches the live
  instance via `Engine_GetPlatform()`, exactly as it reaches `Engine_GetRenderer()`.
- **Implementations**: headers in `engine/include/platform/<name>/**`, sources in `engine/src/platform/<name>/**`,
  plain filenames inside either (`Memory.cpp`, `Input.cpp`, …) — the path already says which platform it is.
  Non-C++ per-platform files (`platform.cmake`, `cooklist.json`, PS2's `ClangdConfig.cmake`, Vita's shader `.cg`
  sources) live in the sibling `engine/config/<name>/**` instead, since they are neither a header nor a source file.
  Conditional compilation is allowed **only** inside these directories.
- **PS2 is a base, not a target**: `engine/include/platform/ps2/` + `engine/src/platform/ps2/` hold everything shared
  by the console (`Ps2Platform`, abstract); `ps2/pal/` and `ps2/ntsc/` supply identity and register themselves. There
  is deliberately no bare `PS2` — a build always resolves to one region, and each region ships as its own binary.
- **PSP is a single platform, not a family**: `engine/include/platform/psp/` + `engine/src/platform/psp/` hold
  `PspPlatform`, concrete, with `PlatformConstants.h` at the directory root the way Win32 has it. The later hardware
  models differ only in a memory ceiling, which is not worth a second binary. Its C heap and its engine map are carved
  from the same 24 MB user partition, so the two are sized together — see `docs/psp/PLATFORM.md`.
- **Vita is the same pattern**: `engine/include/platform/vita/` + `engine/src/platform/vita/` hold `VitaPlatform`
  (abstract); `vita/handheld/` and
  `vita/tv/` supply identity. They differ in pad count (1 vs 4), in whether touch surfaces exist, and in the pad
  itself — the handheld reports its two shoulders on the trigger bits and has no second row or stick clicks, so it
  translates them and answers the debug chords differently. All compile-time, which is why they are separate
  binaries rather than one that probes at startup.
- **nx (Nintendo Switch) is a single platform whose framebuffer is a run-time fact**: `engine/include/platform/nx/` +
  `engine/src/platform/nx/` hold `NxPlatform`, concrete. Docking changes the framebuffer between 1280x720 and
  1920x1080 while the title runs, so it follows the Win32 resizable-framebuffer model (`ResizableWindow` answers
  true, renderers re-read the size every frame) rather than a variant per mode, and `Touch` is answered live from the
  operation mode. One C heap owns the whole process allowance, so the engine map is carved from it. Face buttons map
  **by label** (A to `Cross`), which is why the `Nintendo` button-icon family exists apart from `Xbox`. See
  `docs/nx/PLATFORM.md`.
- **The interface is sized by the platform.** `UI_METRIC_SCALE_PERCENT` is a required platform constant: the theme
  declaration is authored against the console reference framebuffer, and a platform with a materially smaller screen
  states the proportion there. It is applied once, where a style is installed, so no widget and no theme carries a
  second set of numbers. See `docs/subsystems/UI.md`.
- **Keyed accessors**: every generic getter takes an `enum class` key from `engine/include/platform/PlatformKeys.h` —
  `PlatformConstant`, `PlatformCapability`, `GamepadButton`/`GamepadStick`/`GamepadTrigger`, `DebugChord`,
  `KeyboardKey`, `MouseButton`. Never a string or a bare index.
- **Debug chords come from the platform**: engine tooling asks `GetDebugChord()` for an intent and gets back a button
  mask, because pads do not agree on which buttons exist. Never hard-code a button combination in shared code — it
  is unpressable on the first platform missing one of them, and fails silently.
- **Input is four separate device groups**: `Gamepad_*`, `Keyboard_*`, `Mouse_*`, `Touch_*`. A platform that lacks a
  device returns honest stubs (false/zero) and reports it through `HasCapability` — it never emulates one device as
  another, and in particular **a mouse is not a touchscreen** in either direction. `PollInput()` fills a snapshot once
  per frame; all queries read that snapshot. Touch positions are normalised to [0,1] over their own surface, never
  pixels — a rear touch surface has no pixel correspondence to anything on screen.
- **Registration**: each concrete platform calls `PLATFORM_DEFINE_BUILTIN(id, "name", Type)` at file scope in its
  `Platform.cpp`, which defines `Platform_CreateBuiltin()`. `Engine_Main` calls that **directly** — do not replace it
  with static-initialiser self-registration: the platform lives in a static library, and a linker only extracts an
  archive member that resolves an undefined symbol, so a self-registering object would be silently dropped and the
  registry would come up empty.
- **Entry point**: the engine owns `main()`. `engine/src/platform/<name>/Entry.cpp` supplies the OS entry symbol and
  calls `Engine_Main` (`engine/src/core/EngineMain.cpp`), which parses argv, creates and initialises the platform,
  builds a renderer (walking `GetFallbackRenderer()` if one fails), then runs `EngineStart` / `EngineUpdate` /
  `EngineStop`. `game/**` supplies only `GameInit()` / `GameUpdate(dt)`.
- **Startup flags**: `engine/include/core/CommandLine.h` parses argv into a fixed static table (no heap, no STL) and
  is the reusable place for engine and game launch flags alike.
- **Worker threads run above the main thread, not below.** A platform whose kernel does not time-slice between
  priorities (the PS2's does not) will only run a lower-priority worker when the main thread blocks — and a frame
  loop that spins on display hardware may not block for a whole second. The failure is silent: IO still completes,
  just orders of magnitude slower than the medium, which reads as a stall somewhere else entirely. PS2 sets
  `PLATFORM_MAIN_THREAD_PRIORITY` / `PLATFORM_WORKER_THREAD_PRIORITY`; see `docs/ps2/PLATFORM.md`.

**Current state**: PS2, Win32, Vita, PSP and nx are all live. `dist/nx/game.nro` builds in devkitPro's container and
boots to the main menu under Ryujinx on the deko3d backend, not yet on hardware (see `docs/nx/BUILD.md`). The engine runs entirely through `Platform` - memory map, clock,
threads/semaphores, file access, input, console/panic and renderer construction - and `engine/src/` contains no OS
calls. `dist/win32/game.exe` opens a real window and boots into the game through WebGPU at vsync, from the
same unmodified `game/**` sources the PS2 build uses. `dist/vita/` and `dist/vitatv/` produce installable `.vpk`
packages carrying the executable, assets, worlds and store-front metadata. `dist/psp/` produces **two** containers from
one staged tree — an `EBOOT.PBP` plus loose files for a memory card, and a UMD-shaped `game.iso` — and one binary boots
from either, resolving its asset root from whichever it was started from.

Remaining: the skybox and far-field paths in both desktop backends. The giftag backend now renders the same scene
as ps2gl — primitives, models, sky, interface and streamed world sectors, textured — verified by capture under
emulation, not yet on hardware. See `docs/ps2/renderers/GIFTAG.md`.

**On the PS2, a batch names its primitive in a register write, never in the transfer tag.** The tag's primitive
field is not honoured, so a batch relying on it silently inherits the previous primitive and its attributes — which
draws the scene as screen-aligned rectangles that still cover roughly the right pixels, and never textures. Compare
backends with `tools/ps2/emu_capture.py` rather than by eye.

**The texture ceiling is a renderer question, the texture cost is a platform one.** `Renderer::GetTextureBudgetBytes()`
defaults to the platform constant; a backend left with less by its own frame and depth buffers overrides it, and
`EngineResource` enforces what the backend reports. The PS2 page budget is ps2gl's layout; giftag renders full-height
32-bit and has roughly an eighth of it.

**Known bug, pre-existing**: `EngineInput.h`'s `GamePadButton` has all four shoulder masks transposed relative to
ps2sdk's `libpad.h` (`R1=0x0800 L1=0x0400 R2=0x0200 L2=0x0100`). `PlatformKeys.h` carries the correct values; the
legacy enum dies with `EngineInput.cpp`.

## Debug Testbed and UI

Two engine subsystems arrived together and are easiest to understand as a pair.

- **UI** (`engine/src/ui/**`, `engine/include/ui/EngineUi.h`) is an immediate-mode interface in the Dear ImGui style:
  widgets are calls made fresh every frame, identity comes from the label, nothing is retained and nothing is
  allocated. It fills in `class UI` and `Renderer::AddUIToDrawList`, which had been reserved and empty. Text is drawn
  from a cooked font atlas as **one quad per glyph**, falling back to a built-in 5x7 bitmap font that costs several
  quads per glyph where no cooked font is available. Either way a screen is budgeted in quads (`UI_MAX_QUADS`, a
  platform constant); a container too tall for its budget is scrolled rather than paged, and work past the frame's
  own quad ceiling is dropped and reported once, never per item. `AddUIToDrawList` is implemented once in the base
  class so every backend draws an identical interface. A font's atlas may also carry a cell set of icons and
  controller glyphs, addressed by `UiIcon` rather than by codepoint (`Ui_Icon`/`Ui_HintBar`); an icon the atlas has
  no cell for falls back to a short piece of text, and `PlatformConstant::ButtonIconFamily` is what a controller
  prompt reads to draw the shape or letter the hardware in front of the player actually has. Dialogs and text entry
  (`Ui_MessageDialog`/`Ui_ConfirmDialog`/`Ui_TextDialog`/`Ui_TextInput`) pick their mechanism from the platform
  contract in order — `PlatformCapability::SystemDialog`, then (text only) `PlatformCapability::TextCharacters`, then
  the interface's own drawn modal, which is unconditional and is what PS2 always uses; the two capabilities and
  `Keyboard_PopCharacters`/`Dialog_Open`/`Dialog_Poll`/`Dialog_Cancel` are platform-contract additions every platform
  must answer honestly, the same as every other `PlatformCapability` key. `Ui_Image3D` draws a model or primitive as
  an image the same way `Ui_Image` draws a loaded texture, via `Renderer::RenderToImage3D` — a synchronous,
  one-object-at-a-time render into one scratch target per backend, distinct from the ordinary deferred draw-list
  path because its result must be sampleable by the interface the same frame it is requested. Not every backend
  implements it (PspGl and vanilla Ps2Gl answer honestly with the invalid handle; see their renderer specs for why);
  a caller treats that exactly like an unresolved `Ui_Image` handle, a placeholder frame rather than a stall.
- **Testbed** (`engine/include/debug/**`, `engine/src/debug/**`) is the scene catalogue reached by the
  `DebugChord::DebugMenu` chord. It is
  engine-owned: `game/**` does not know it exists, and the engine turns both subsystems on itself regardless of the
  game's list. Every transition through it calls `Engine_ResetRuntimeState()`, so a scene starts from a known state.
  A scene gates on capability, never on platform identity.

See `docs/subsystems/UI.md`, `docs/subsystems/TESTBED.md` and `docs/TESTBED.md`.

## Examples

`examples/` holds standalone per-capability binaries, sibling to `game/` and
built the same way — `GameConfigure`/`GameInit`/`GameUpdate` against
`GameAPI.h`, one or more `game::Scene`s, one executable per active platform.
Unlike a Testbed scene, an example shares nothing with any other example or
with the game: its own process, its own memory, its own runtime-reset
boundary. That isolation is the reason this directory exists — Testbed's
scenes share one process, and that shared state has masked bugs. A capability
showcase belongs here; a scene that only means something against a real
running game's live state (hardware input, memory occupancy, frame timing)
stays in Testbed. See `docs/EXAMPLES.md` and
`docs/guidelines/NEW_EXAMPLE.md` before adding one, and
`docs/subsystems/TESTBED.md` for the split's full rationale.

Building an example is expected to surface engine defects a contaminated
Testbed process was masking. Fix those in the same change, never a
follow-up, and log them in `docs/fixed_issues/issues.json`
(`tools/schemas/fixed_issues.schema.json`) for regression lookup.

Examples build to `examples/dist/<name>/<platform>/`, a tree separate from
the game's own `dist/<platform>/`, via the `examples` CMake target —
`cmake --build . --target dist` (the shipping path) never touches them.

## Scenes

`game::Scene` and its siblings (`engine/include/scenes/Scene.h`, `LevelScene.h`, `UIScene.h`, `FreeFlowScene.h`,
`SceneResource.h`; engine-side plumbing in `engine/include/scenes/EngineScene.h` and the matching `engine/src/scenes/`
sources — one class per file, mirroring the headers) is how the engine — not `GameUpdate` — owns switching between
what a game is doing. `game::LevelScene` and `game::UiScene` layer a fixed resource/render
pipeline over the base contract (mount/stream/draw a level; guarantee declared resources before drawing); a game
wanting to load and render entirely by hand derives from `game::FreeFlowScene` (or `game::Scene` directly), same
freedom `GameUpdate` always had. `game::SetMainScene`/`SwitchScene`/`ReloadScene` switch by C++ reference, never by
name or index, and every switch reuses `Engine_ResetRuntimeState()` — the same "between scenes" reset
`docs/subsystems/UI.md` already documented before this subsystem existed. A scene's `GetResources()` declares what it
needs; the engine tries to load all of it eagerly and only falls back to letting the scene stream for itself if that
does not fit, driving a built-in loading screen (`game/config/loading_screen.json`) meanwhile. See
`docs/subsystems/SCENE.md`. `game/src/Game.cpp` is unaffected if a game never registers a scene: `GameUpdate(dt)` is
still called directly in that case, exactly as before this subsystem existed.

## Engine / Game Boundary

Game code lives in `game/**` and is authored against `engine/include/GameAPI.h`
(`GameConfigure(config)` / `GameInit()` / `GameUpdate(dt)`).

**The game chooses its subsystems.** `GameConfigure` runs first — before the engine, its memory, or the renderer
exist — and fills `EngineConfig::subsystems` from `EngineSubsystem` (`EngineSubsystems.h`). The engine brings up
exactly that set, in dependency order, and panics naming both sides if a requested subsystem's dependency is absent.
Leaving the list null selects everything.

Memory, Debug logging and the Renderer are **not** selectable — they are preconditions of the engine existing.
Running without graphics is a renderer choice (`--renderer null`), not a subsystem being switched off. `Input` and
`Action` are also not really selectable: `Engine_Subsystems_Set` force-enables both regardless of what a game requests,
because Action is the only way anything in the engine (Debug, UI, Testbed) checks for input — see
`docs/subsystems/ACTION.md`. Every other optional subsystem must behave correctly when one it does not depend on is
absent; each spec in `docs/subsystems/` states what that looks like.

There is **no build-time fence**. `engine/include/app_public/` and the `ENGINE_SANDBOX_MODE` CMake option were removed
along with Lua — they existed to isolate a scripted app layer, and with native gameplay they were forwarding shims
around an indirection. The boundary is now convention:

> Game code should include `GameAPI.h`. Reaching into `EngineMemory.h`, `EngineIO.h`, `EngineResource.h` or the
> renderer from `game/**` means `GameAPI.h` is missing something — extend it rather than bypassing it.

## Renderer Backends

Both PS2 backends are compiled into every PS2 binary so the backend can be chosen at run time. They used to be
mutually exclusive via `#ifdef RENDERER_BACKEND_PS2GL` / `RENDERER_BACKEND_GIFTAG` wrapping the whole of
`GLRenderer.cpp`, `TagRenderer.cpp` and `TagRenderer.h`; those guards are gone. `Ps2Platform::CreateRenderer` picks
one, `GetDefaultRenderer()` returns it, and `GetFallbackRenderer()` defines the degradation order.

`Ps2GlRenderer` is **ps2gl** (a GL-1.1-subset library over the GS), not desktop OpenGL — they are unrelated
backends under different `RendererId`s. The classes were renamed from `GLRenderer`/`TagRenderer` and moved to
`engine/src/platform/ps2/renderer/` precisely so that distinction is visible at the call site.

The Vita has two backends on the same pattern: `GxmRenderer` drives the console graphics API directly and is the
default, `VitaGlRenderer` is a fixed-function subset over the same API kept as a known-good reference. `vitaGL` is
**unrelated** to the desktop OpenGL backend despite the name, exactly as `ps2gl` is.

nx has two on the same pattern again: `Deko3dRenderer` drives the graphics processor through deko3d and is the
default, `OpenGlRenderer` is real desktop OpenGL through the Mesa port, kept as the reference. That second one
reuses `RendererId::OpenGl` because it is the same API, but it is a **separate implementation** under
`engine/src/platform/nx/renderer/` that shares no code with Win32's. Both nx backends compile **one** GLSL source
(`engine/config/nx/renderer/shaders/`, no `#version`; `tools/nx_shader.py` prefixes the version each needs), so a
frame difference between them is a backend bug.

`StagedGeometry` (`engine/include/graphics/StagedGeometry.h`) is the shared processor-side geometry stager used by
every backend that rebuilds its vertex data each frame and uploads it once — both desktop backends, both Vita
ones, both PSP ones and both nx ones. Because they stage identically, a frame difference between two of them is a bug in one, not a difference in
what was submitted. The PS2 backends do NOT use it: they build a stride-0 layout straight into a transfer packet.
`Gfx_ExpandToRgba8` (`engine/include/graphics/TextureExpand.h`) is likewise shared — every non-PS2 backend expands
cooked console pixel formats to RGBA8 on upload, and it was duplicated per backend before.

`NullRenderer` (`engine/src/graphics/NullRenderer.cpp`) is platform-neutral and last in every fallback chain: it
accepts every call, records draw-list counts so the perf snapshot still works, and draws nothing. It is what lets a
new platform boot and be validated before any graphics code exists.

## Toolchain & Environment

- **Environment Variables**: `PS2DEV` must be set to the root of the PS2 toolchain (e.g. `/usr/local/ps2dev`),
  `VITASDK` to the root of the Vita toolchain (e.g. `/usr/local/vitasdk`), with `$VITASDK/bin` on `PATH`, and `PSPDEV`
  to the root of the PSP toolchain (e.g. `/usr/local/pspdev`), with `$PSPDEV/bin` on `PATH`, and `DEVKITPRO` to the
  root of devkitPro (`/opt/devkitpro`, exported by its own `/etc/profile.d` script, which the login shell
  `tools/build.py` enters WSL through reads).
- **A toolchain may build in a container.** `tools/build.py`'s `CONTAINERS` map names a pinned image per toolchain;
  when the host has no install of its own (`--container auto`, the default) the whole configure-and-build runs in
  it, with the checkout mounted at its own path and the container running as the host user, so the build tree and
  `dist/` land where a host build puts them. nx is the one user: devkitPro's servers refuse some networks, and its
  published image (`tools/docker/devkita64/Dockerfile`, pinned by digest, plus Pillow and jsonschema) needs only
  Docker Engine in WSL. Never unpin the base image, never mix a container-configured and a host-configured build
  tree, and run emulators from WSL, not the container. See `docs/nx/BUILD.md`.
- **Cross-Compilation**: `toolchains/ps2dev.cmake` targets `mips64r5900el-ps2-elf`; `toolchains/vitasdk.cmake` targets
  `arm-vita-eabi` and **appends** its flags rather than forcing them, because the SDK adds the linker flag that keeps
  the relocation table and the executable conversion fails without it. `toolchains/devkita64.cmake` targets
  `aarch64-none-elf` for nx through devkitPro's own `Switch.cmake`, appends likewise, and adds libnx and the portlibs as
  `-isystem` roots: libnx trips `-Wmissing-field-initializers` inside its own headers and deko3d's header uses a
  C++17 nested namespace, either of which stops a `-Werror` build in a file this project does not own.
- **devkitPro's CMake owns the `NX_` prefix** (`NX_ELF2NRO_EXE`, `NX_UAM_EXE`, `NX_ROOT`, ...). The nx fragment's own
  variables are `ENGINE_NX_*` / `PLATFORM_NX_*`; a bare `NX_` variable would silently shadow a vendor one.
- **`tools/ps2/masp` overrides the toolchain's own**: the PS2 toolchain ships a `masp` whose bundled `memmove`
  compiles into infinite self-recursion, so it segfaults on every input including an empty file and no PS2 binary can
  be linked. The repo carries a working rebuild and `external/CMakeLists.txt` points ps2gl at it; without that file
  present the build falls back to the toolchain's and fails. Do not "fix" this by editing ps2gl. See
  `docs/ps2/MASP.md`.
- **Toolchain identity**: every toolchain file sets `ENGINE_TOOLCHAIN_ID`, and `cmake/Platforms.cmake` filters
  platforms on that rather than on `CMAKE_SYSTEM_NAME` — both console toolchains report `Generic`, so the system name
  alone cannot tell them apart and a Vita configure would try to build PS2 with an ARM compiler.
- **Compiler/Linker**:
    - The engine is C++ throughout.
    - CMake property: `set_target_properties(<target> PROPERTIES LINKER_LANGUAGE CXX)`.

## NEVER DO

- Do not modify ps2gl directly or commit the changes there.
- Do not modify ps2stuff directly or commit the changes there.
- Do not use `git` to add/commit/push changes in the **whole** repository.
- Do not reintroduce a scripting VM (e.g. Lua) into the engine — it was removed in favor of native C++ via `GameAPI.h`.
- Do not call OS or hardware APIs from shared engine code. Anything touching the EE kernel, the GS, pads, threads,
  files, or wall-clock time goes behind the `Platform` interface, implemented in `engine/include/platform/<name>/**`
  and `engine/src/platform/<name>/**`.
- Do not write `#ifdef PLATFORM_*` outside a platform's own directory. Platform choice is expressed by which sources
  CMake compiles, never by conditionals in shared code. **The same goes for the debug configuration**: the testbed in
  `engine/src/debug/` is compiled only when `DEBUG` is on, with `engine/src/debug/Stub.cpp` supplying the same entry
  points as empty bodies otherwise. Shared code calls those entry points unconditionally.
- Do not write a comment that duplicates a spec. Behaviour, rationale, hardware quirks and renderer limits belong in
  `docs/`; the source carries no copy of them, and no pointer to them either. See "Documentation" below.
- Do not write inline comments at all. **The only comment a source file carries is a doc comment on a declaration**,
  giving the summary, parameters and return value so an editor can show them to a caller. See the Comments section of
  `.github/instructions/cpp-expert.instructions.md`.
- Do not let a platform answer a query it has no value for. A required constant that is undefined fails the build
  in `engine/src/PlatformContract.cpp`; a missing `case` fails it via `-Wswitch`; reaching the fallback panics
  naming the key. Never return a placeholder — `0` is a legitimate value for several constants, so a guessed zero is
  indistinguishable from a real one.
- Do not cross allocators. Memory from the platform memory contract is released through it
  (`Engine_PlatformAlloc` / `Engine_PlatformFree`, or `PlatformArray<T>`); `malloc`/`calloc` memory through `free`.
  On Win32 the two are different heaps and crossing them is undefined behaviour.
- Do not write code after a panic. `Engine_Panic` and `Platform::Panic` are `[[noreturn]]`.
- Do not log a condition that persists on every frame it persists, and do not allocate, grow or free on the frame
  path. A log line is a synchronous memory-card write on the handhelds and an EE<->IOP round trip on the PS2, so a
  per-frame diagnostic becomes the slowest thing in the frame; growth mid-play fragments a heap the smallest target
  cannot afford to fragment. Report on change, from fixed storage sized by a platform constant. The rules and their
  reasons are `docs/guidelines/PERFORMANCE.md`.
- Do not commit `external/psp2cgc/`. It is Sony's shader compiler, redistributed by third parties rather than
  licensed for redistribution; each developer fetches their own and the build requires it.
- Do not use the VitaSDK's `vita_create_self()` / `vita_create_vpk()` macros. They accumulate their arguments into
  CACHE variables and append on every call, so in a two-variant configure the second variant inherits the first
  variant's title id and file list. The platform fragment calls the underlying tools directly instead.
- Do not publish a view into bytes read off disc before every offset, count and size in it has been checked
  against the blob at a width that cannot wrap a 32-bit `size_t`, and do not clamp one into range — refuse the
  whole blob and report the field. Do not let a completion write to a slot without checking its generation, or a
  shutdown path release state before the IO worker has acknowledged it stopped. The rules and their reasons are
  `docs/guidelines/SECURE_CODING.md`; the check that the tree obeys them is `docs/guidelines/SECURITY_REVIEW.md`.

## Build System

- **Platform selection**: `PLATFORMS_TO_SUPPORT` (see `cmake/Platforms.cmake`) defaults to every known platform and
  is filtered against the active toolchain, so `cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/ps2dev.cmake -B build/ps2`
  needs no other flag and builds PS2PAL + PS2NTSC. Naming an incompatible platform **explicitly** is a `FATAL_ERROR`;
  the default list is filtered with a logged reason.
- **One bundle per platform**: each active platform gets its own engine library (`engine_<platform>`), its own
  executable (`app_<platform>`), and its own self-contained `dist/<platform>/` — `dist/ps2pal`, `dist/ps2ntsc`,
  `dist/win32`. Nothing is shared between bundles, so a PAL ISO can never be handed to another target.
  `cmake --build <dir> --target dist` builds them all.
- **Toolchains**: `toolchains/ps2dev.cmake` (PS2, `mips64r5900el-ps2-elf`), `toolchains/mingw-w64.cmake`
  (Win32, cross-compiled from WSL — needs `sudo apt install mingw-w64`), `toolchains/vitasdk.cmake`
  (Vita, `arm-vita-eabi`), and `toolchains/pspdev.cmake` (PSP, `psp`), which includes the vendor's own
  `$PSPDEV/psp/share/pspdev.cmake` and **appends** its flags behind a cache guard rather than forcing them.
- **Every declarative JSON file the game owns lives under `game/config/`**, one unified space rather than one per
  kind of declaration — but the **schema** that validates each one does not live beside it: schemas are not
  authored by the game, they are the cook system's own contract, so all of them live together under
  `tools/schemas/` instead. A declaration's `$schema` field still points there by relative path, purely as an
  editor hint; nothing reads that field back; the tool that actually validates one takes the schema path as its own
  argument. `game/config/title.json` (schema `tools/schemas/title.schema.json`, reader `tools/title.py`) holds who
  made the title, what it is called, and what each platform files it under. Every platform's packaging and every
  platform's writable-storage location are built from it, so the identity a console shows and the identity a save
  is filed under cannot disagree. `game/config/achievements.json` (schema `tools/schemas/achievements.schema.json`,
  reader `tools/achievements.py`) is the same idea for the achievement set, and `game/config/theme.json` (schema
  `tools/schemas/theme.schema.json`, reader `tools/theme.py`) for the interface's look — its themes are generated
  into the binary *and* cooked as loadable assets from one declaration, and the generator checks colour and font
  roles against `UiColor`/`UiFontRole` so neither can exist in the engine without a matching entry. A theme is
  colours, metrics *and* fonts together: exactly one theme is marked `"default": true`, and every other theme may
  name as few or as many of its own colours/metrics/fonts as it wants — anything it does not name is copied from
  the default theme field by field, so a theme built for a smaller or poorer display can ask for larger text
  without every other theme paying for it. The default theme itself must be complete (every colour role, every
  metric, and a `Default` font) since there is nothing above it to inherit from; an incomplete default theme fails
  at cook time. `tools/theme_editor.py` edits the declaration visually and cannot write an invalid one, since it
  validates through the same `tools/theme.py` checks before saving.
  The PS2 memory card save icon is generated from the same title declaration (`tools/ps2/save_icon.py`): a save
  directory without it is reported as corrupted by the console browser even though its data is intact.
  `game/config/loading_screen.json` (schema `tools/schemas/loading_screen.schema.json`, reader
  `tools/loading_screen.py`) declares the images the built-in loading screen cycles through while a scene's declared
  resources are outstanding (see `docs/subsystems/SCENE.md`); like theme.json it is compiled in, generating
  `LoadingScreenAssets.h`, because it must reach C++ running on a console with no JSON parser.
  `game/config/actions.json` (schema `tools/schemas/actions.schema.json`, reader `tools/actions.py`) is the
  title's action map — see `docs/subsystems/ACTION.md`. It is compiled in like loading_screen.json, but unlike
  every other generated pair here the generated table varies **per platform**: which of an action's candidate
  bindings survive is a capability question (a missing `L3` on Vita handheld), so `tools/actions.py --emit-table`
  takes a `--platform` argument and `game/CMakeLists.txt` generates one `ActionTable.cpp` per active platform
  rather than one shared file. The `ActionIds.h` half is not platform-varying and is generated once. Every
  declaration's three highest-numbered ids must be `EngineDebugPerfSnapshot`, `EngineDebugOverlayToggle`,
  `EngineDebugMenu` in that order (`tools/actions.py` rejects a declaration that omits or reorders them) — Action
  is the only way anything in the engine checks for input, and these three are how Debug/PerfLogger/Testbed do it.
  A platform
  config — `game/config/platform/<name>/package.json`, validated against `tools/schemas/package.schema.json` —
  carries only what is *specific* to that platform's container, and has its identity folded in when it is read; it
  does not restate one. The cook list answers a *hardware* question and stays in `engine/config/<name>/`; a title
  id and an icon answer a question about the *game* and do not. A platform's own non-JSON packaging assets (Vita's
  `sce_sys/`, its trophy icons) move with its `package.json`, under `game/config/platform/<name>/`, since those paths
  are declared relative to it.
- **Adding a platform**: one `<name>` subdirectory under each of `engine/include/platform/`, `engine/src/platform/`
  and `engine/config/` — headers, sources, and the non-C++ `platform.cmake`/`cooklist.json` respectively — plus one
  entry in `ENGINE_KNOWN_PLATFORMS` and its metadata block. **No shared build file names a platform** — the
  root drives, each fragment declares. The fragment defines `platform_configure()` and optionally
  `platform_dependencies()` (third-party deps, before any engine target), `platform_package()` and
  `platform_run()` (a `run-<dist>` target, available in release as well as debug),
  `platform_debug_symbols()` and `platform_example_package()` (a container build appended to an example's staging
  target, for a platform whose executable cannot run from a staged folder); it may set `PLATFORM_<P>_LINK_DEPS`, `PLATFORM_<P>_PACKAGE_TARGET` and
  `PLATFORM_<P>_CLEAN_PATHS`. Anything platform-specific — a disc serial, an image name, a packaging tool — lives in
  the fragment. See `docs/guidelines/NEW_PLATFORM.md`.
- **Five pipeline stages**, with distinct artefacts — see `docs/PIPELINE.md`:
  compile engine (`engine_<platform>`) → compile game (`app_<platform>`) → **cook** (`cook-<platform>`, source art
  → `dist/cooked/<platform>/`) → **package** (`package-<platform>`, validated, then containers) → distribution
  (`dist/<platform>/`).
- **Cooking is per platform.** The right texture encoding is a hardware question, so each platform declares one in
  `engine/config/<name>/cooklist.json` (validated against `tools/schemas/cooklist.schema.json`). Per-asset JSON
  says *what* an asset is; the cook list says how this platform bakes it. Cooked output and the containers built from
  it therefore **differ between platforms by design** — compare a platform against its own previous build, never
  across platforms. Worlds are cooked per platform too, for their textures: `tools/compile_level.py` caps a brush
  material's dimensions against that platform's `cooklist.json` (`level_textures` — a level pins every material for
  its whole lifetime, unlike a standalone `TEXTURE` resource), producing `dist/cooked/<platform>/levels/` via the
  `compile-levels-<platform>` target, same shape as `cook-<platform>`/`rassets/`.
- **Packaging is gated on validation**: `tools/validate_cooked.py` checks a cooked tree against the cook list that
  produced it, so a bad cook cannot reach a container. `tools/inspect_asset.py` and `tools/inspect_archive.py` dump
  cooked assets and containers without running the engine.
- **Looking at what a PS2 build actually drew**: `tools/ps2/emu_capture.py` boots a disc image under the emulator and
  captures a frame and the console log, including running the same scene through both backends for comparison. It
  locates the emulator through `tools/run_target.py`, so `$PCSX2_PATH` works the same way. Read the capture notes in
  `docs/ps2/BUILD.md` before driving the emulator by hand — a launch argument silently loses its first token, and a
  fullscreen surface captures as a black frame that looks exactly like a renderer bug.
- **Vita prerequisites beyond the SDK**: `vdpm install vitaShaRK taihen libmathneon` for the fallback renderer, and
  an offline shader compiler (`psp2cgc`) for the default one. The compiler is **required, not optional** — a silent
  fallback would swap a self-contained title for one needing a player-installed component — and is **gitignored**
  rather than committed. See `docs/vita/BUILD.md`.
- **Entry Point**: Use `python3 ./tools/build.py` for a clean rebuild.
    - **Requirement**: Must be run through **WSL (preferred)** or **Git Bash** in Windows environments; on native Windows, the script re-invokes itself inside WSL automatically.
- **Output Directory**: Binaries and discs are routed to `dist/<platform>/`, one bundle per platform.
- **ISO Generation**:
    - Requires `genisoimage` (provides `mkisofs`).
    - `SYSTEM.CNF` is NOT a static file; it is dynamically generated by `CMakeLists.txt` to ensure the `BOOT2` path
      matches the uppercase name of the executable.
  - **Asset Inclusion**: Loose files placed directly under `assets/` (not inside `assets/maps/`, `assets/textures/`,
    or `assets/models/` — those three are consumed by the cook and level pipelines instead) are automatically
    pulled into the root of the generated `.iso` filesystem during the build.
  - **Action**: When adding a file that must be present verbatim on the PS2 disc, place it directly under `assets/`
    to ensure it is available to the engine on the PS2 target.

## Dependencies (external/)

- Managed as Git Submodules.
- `external/ps2gl`: Graphics abstraction layer. Depends on ps2stuff headers (`ps2s/`) at compile time.
- `external/ps2stuff`: Low-level PS2 hardware utility library. Must be built and installed (`make install`) **before** ps2gl. Its install step copies `include/ps2s/` headers to `$(PS2SDK)/ports/include/ps2s/`. Never modify ps2stuff directly or commit the changes there.
- **Link Order Matters**: Ensure `ps2stuff` is linked when using `ps2gl`.
- `external/vitaGL`: the Vita fallback renderer, pinned to the revision the SDK's own package set is built from —
  its master calls into a newer vitaShaRK than the SDK packages and does not compile. It compiles its shaders at run
  time, so it needs `libshacccg.suprx` on the player's console; the default Vita renderer does not, which is why it
  is the fallback. It also exposes no teardown entry point.
- **PSP vendors nothing.** Both PSP renderers are served by libraries the toolchain already ships — `libGL`/`libGLU`
  (pspgl) over `libpspvram`, plus `libpspvfpu`, which pspgl needs and which must precede it on the link line. There is
  deliberately no `platform_dependencies()` hook and no submodule for this platform. **Never link `pspkernel` into it**:
  that is the kernel-mode stub library and it redefines libc symbols, so a user-mode title fails on a duplicate
  `strtol` rather than on anything naming the real cause.
- **nx vendors nothing either.** deko3d, Mesa (`EGL`, `glapi`, `drm_nouveau`) and `glad` come from devkitPro's
  packages (`switch-dev switch-mesa switch-glad`); no submodule, no `platform_dependencies()`. The distribution is one
  `.nro` with the master archive in its embedded RomFS, and each example gets its own through the optional
  `platform_example_package()` fragment hook.
- **No scripting layer**: Lua was removed from the engine (was `external/lua`). Gameplay and UI are
  authored entirely in C++ against `engine/include/GameAPI.h` (`GameInit()` / `GameUpdate(dt)`). Do
  not reintroduce a scripting VM into the per-frame gameplay path — the PS2 EE is a poor interpreter
  host; a benchmark showed a Lua-driven per-object hot loop alone costing ~130% of the 20ms frame
  budget, resolved by moving that loop to native C++.

## Documentation

### Layout

`docs/PLATFORMS.md` is the index. Everything has one home:

| Kind | Location |
| :--- | :--- |
| Engine architecture | `docs/ENGINE.md` |
| Subsystem | `docs/subsystems/<NAME>.md` |
| Platform | `docs/<platform>/PLATFORM.md`, `docs/<platform>/BUILD.md` |
| Renderer | `docs/<platform>/renderers/<NAME>.md` |
| On-disc format | `docs/formats/<NAME>.md` |
| Build pipeline | `docs/PIPELINE.md` |
| Debug testbed scenes | `docs/TESTBED.md` |

### Building something new

**Read the guideline before implementing a new system or platform** — before
starting, not while reviewing:

| Adding | Read |
| :--- | :--- |
| An engine subsystem | `docs/guidelines/NEW_SYSTEM.md` |
| A platform (console, desktop OS, or a variant) | `docs/guidelines/NEW_PLATFORM.md` |
| A standalone example | `docs/guidelines/NEW_EXAMPLE.md` |
| Code that reads on-disc bytes, completes asynchronously, shares state with the IO worker, hands a buffer to an OS API, or runs in the build or CI | `docs/guidelines/SECURE_CODING.md` — rules, read **before** writing |
| A check that the tree has none of that class of defect | `docs/guidelines/SECURITY_REVIEW.md` — procedure, after such a change and as a periodic pass |
| Code on the frame path — the loop, geometry staging, a backend's frame, the interface, IO dispatch, sector streaming, or a platform's clock, threads or console | `docs/guidelines/PERFORMANCE.md` — rules, read **before** writing; also what every platform spec's *Performance* section must state |
| A check that the tree obeys those rules and each platform spec's *Performance* section matches the platform as built | `docs/guidelines/PERFORMANCE_REVIEW.md` — procedure, after such a change, as a periodic pass, and before and after a platform is added; `docs/backlog/performance_findings.md` is the record |

The three `NEW_*` guidelines follow the same four steps:

1. **Write the spec first.** No code until the contract, dependencies, lifecycle,
   behaviour-when-absent, failure modes and limits are written down.
2. **Decompose the spec** into components and tasks, each traceable to a line of
   the spec. Anything with no spec line behind it is scope creep or a gap in the
   spec — resolve which before building it.
3. **Evaluate off-the-shelf components and their risk**, and record the decision
   in the spec — including the decision to write your own. Most libraries are
   ruled out by C++11 / no-exceptions / no-RTTI / no-STL-containers / fixed
   budgets / two toolchains; name the constraint that applied.
4. **Implement per platform first, then engine-wide.** Anything built against a
   single platform encodes that platform's assumptions into its contract.

Each guideline ends with a definition of done. It is a checklist, not a summary.

### Specs are authoritative, and abstract

- **Read the relevant spec before changing what it describes.** Specs carry the reasoning that is deliberately not in
  the source: hardware quirks, race conditions, renderer limits, budget ceilings. That rationale was removed from the
  code, so reading the code alone means reading it without the reasoning.
- **Write specs abstractly**: contracts, states, guarantees, failure modes, limits. No code excerpts, no function
  signatures, no paths into `engine/src`. A spec should stay true across a refactor that preserves behaviour. Format
  specs are the exception — a byte layout is the contract.
- **Every subsystem spec** states what it depends on, what depends on it, and how it behaves when not loaded.
  **Every renderer spec** states its quirks and what it does not implement.
- **Every platform spec** carries a *Performance* section stating the nine items `docs/guidelines/PERFORMANCE.md`
  lists — budget and pacing, the binding constraint, per-frame ceilings, clock, scheduling, cost of diagnostics,
  what the snapshot measures, a dated baseline naming hardware or emulator, and what is left on the table. A ceiling
  changed in a constants header, a change to what the frame waits on, or a re-measured baseline updates it in the
  same change.
- A platform or renderer whose quirks are undocumented is not finished, and neither is a platform whose
  performance is unmeasured.
- Update any spec a change invalidates **in the same change**, never deferred.

## AI Instruction Files

This file is the **single source of truth** for AI-agent-facing conventions in this repo. Other instruction files
exist only to route to it — never fork or duplicate its content into them:

- `CLAUDE.md` (repo root) — auto-loaded by Claude Code every session; must stay a thin pointer to this file.
- `.github/instructions/c-expert.instructions.md` / `cpp-expert.instructions.md` — C/C++ coding-standard details,
  linked from "Coding Standards" below.

**Rule — keep them in sync, in the same change, never deferred:**

- Build steps, toolchain, the "NEVER DO" list, memory/arena layout, constants standard, resource-management
  philosophy, or a dependency added/removed/changed → update **this file**.
- A C or C++ coding-standard change (naming, memory rules, allowed language features, formatting, etc.) → also update
  the matching `c-expert.instructions.md` or `cpp-expert.instructions.md` directly — this file only links to them, it
  does not restate their content.

This applies to human contributors and AI agents alike (Copilot, Claude Code, or any other tool): whichever one made
the change is responsible for updating the relevant file(s) before considering the change complete.

## Memory Management & Allocation Strategy

- **Master Reference**: each platform's budget and arena layout live in its own constants header
  (`engine/include/platform/<name>/PlatformConstants*.h`); `docs/subsystems/MEMORY.md` is the contract.
- **The memory contract**: all engine allocation goes through `MemoryContract`
  (`engine/include/platform/MemoryContract.h`), reached via `Platform::GetMemory()`. Each platform implements it and
  owns its budget, alignment and backing allocator. Shared engine code never calls a system allocator for aligned
  memory — it uses `Engine_PlatformAlloc`/`Engine_PlatformFree` or `PlatformArray<T>` (`EngineMemory.h`). See the
  allocator-pairing rule in "NEVER DO" and in `cpp-expert.instructions.md`.
- **GFX Resources**: Textures and models are managed by the **Resource Manager** (`EngineResource.h`); note that
  `RES_FONT` is a cooked font: metrics whose atlas is an ordinary texture named as its dependency, so the texture
  budget and upload path serve it unchanged. `RES_SOUND` remains unimplemented on every platform. Never allocate GFX
  resources in engine arenas.
    - Use `Engine_Resource_Load(type, path)` to load, `Engine_Resource_Get(handle)` to access.
    - See `docs/subsystems/RESOURCE.md` for the runtime contract and `docs/formats/ASSET_FORMAT.md` for the `.ps2a` layout.
- **Engine Arenas** (for internal subsystems only) — sizes below are the PS2 values:
    - `ARENA_CONFIG`: 256 KB, 4 slots — Configuration data, cached reads.
    - `ARENA_LEVEL_DATA`: 4 MB, 16 slots — level core (slots 0-1), streamed sectors (2-10), prefetch/spare (11-15).
    - `ARENA_RENDERER`: 3 MB, 1 slot — primitive geometry and renderer scratch.
    - Use `Engine_LoadToSlot(ARENA_TYPE, slot, data, size)` for slot replacement.
    - Slots are **16KB aligned** for DMA/VIF performance.
- **Memory Pool** (`g_MainPool`): 1 MB, 256B chunks — scratch allocator for short-lived temp objects only.
- **Asset Authoring**: one asset tree — `assets/` — serves both TrenchBroom (`maps/`, `textures/`, `models/`) and the
  resource archive. A texture or model that should also be a standalone cooked resource carries a JSON descriptor next
  to it, anywhere under `assets/textures/` or `assets/models/` (`tools/cook_assets.py` walks both recursively);
  `assets/maps/` is level-only and never cooked. `tools/cook_assets.py` cooks descriptor pairs to `.ps2a` under
  `dist/cooked/<platform>/rassets/`, per platform, using that platform's cook list. See `docs/ASSET_AUTHORING.md` and
  `docs/PIPELINE.md`.

## Coding Standards

See `.github/instructions/c-expert.instructions.md` for C rules and `.github/instructions/cpp-expert.instructions.md` for C++ rules.

## IDE & IntelliSense Rules

- **Configuration**: Do NOT manually edit the `.clangd` file. It is automatically generated by `tools/build.py`.
- **Cross-Environment Mapping**: The `build.py` script handles the translation of Linux paths (`/usr/local/ps2dev/...`)
  to Windows UNC paths (`//wsl.localhost/Ubuntu/...`) for the IDE automatically using `wslpath`.
- **Git Strategy**: `.clangd` is ignored by Git (`.gitignore`). Each developer produces their own local copy by running
  the build script.

## Resource Management Philosophy

> **The programmer manages resources. The engine enforces limits.**

This is a hard rule across all low-level subsystems (GS VRAM, arenas, pool, resource table):

- **No silent eviction**: The engine will **never** automatically evict, swap, or reclaim a resource behind the
  programmer's back. Any attempt to load a resource that would exceed a budget fails immediately with an error and
  returns `-1`.
- **No implicit LRU**: Even though ps2gl has internal LRU eviction for GS VRAM slots, the engine's Resource Manager
  shadow-tracks page usage and **rejects** loads that exceed `GFX_GS_TEXTURE_PAGE_BUDGET`. The programmer must call
  `Engine_Resource_Unload()` before loading a replacement. **The same applies to the handle table**, not only to the
  texture budget: a full table is an error naming the stalest releasable entry, never a slot reused behind a handle
  the game still holds. Handles carry no generation, so a reused slot would answer "ready" for whatever moved in.
  Recency is tracked only to make that error actionable. `docs/subsystems/RESOURCE.md` states the same rule.
- **Actionable errors**: When a load is rejected due to budget overflow the error message includes: the texture size and
  page cost, current vs total page budget, and how many pages *could* be freed by unloading evictable resources — so the
  programmer knows exactly what to release.
- **Pinning = permanent**: Pinned resources are never touched by any automatic system. Unpinning is an explicit
  programmer act.
- **Crash loudly on programming errors**: Over-budget loads, invalid handles, and missing resources are programmer
  errors. Use `Engine_LogError` (and `Engine_Panic` for unrecoverable states) so bugs surface immediately in testing
  rather than manifesting as silent corruption on hardware.

## Constants & Configuration Standard

`engine/include/Constants.h` and its `Constants.XXX.h` category files no longer exist. A constant lives next to the
thing it describes, decided by one test:

> **A value is a FORMAT constant if an on-disc byte layout or a `tools/` Python script depends on it.
> Everything else is a PLATFORM capability.**

- **On-disc offsets are checked, never trusted.** Every cooked format the runtime reads — archive TOC, level core,
  sector, model, texture, font, theme — is parsed against the blob's own size before any view is published, with the
  arithmetic widened past `size_t` first so a crafted count cannot wrap a 32-bit target into passing. Chunk and
  geometry offsets are checked for alignment too: an unaligned word access traps on both MIPS targets. A payload that
  fails is refused whole and reported, never clamped into range. Each format spec under `docs/formats/` carries the
  list of what its reader must establish; add to it in the same change as the check. The full rule set —
  widening, alignment, establish-then-publish, refuse-never-clamp, generations on anything that completes later,
  lock discipline against the IO worker — is `docs/guidelines/SECURE_CODING.md`.
- **Format constants** live in the header declaring the matching struct — `EngineArchive.h`, `EngineResource.h`,
  `EngineLevelFormat.h`, `EngineIO.h`, `graphics/PrimitiveGeometry.h`. They are identical on every platform and are
  mirrored by `tools/pack_archive.py`, `tools/cook_assets.py`, `tools/compile_level.py`. **Never make one
  platform-varying**: `IO_FILE_MAX_PATH`, for instance, is baked into `AssetFileHeader.deps[][]`, so a per-platform
  value would make a `.ps2a` unreadable on another platform.
- **Platform constants** live in the platform's own header: `engine/include/platform/<name>/PlatformConstants*.h`. Shared
  engine code reaches them with `#include "PlatformConstants.h"`, which CMake resolves to the selected platform
  variant's directory.
- **Runtime platform values** that shared code must query rather than bake in are served by
  `Platform::GetConstant(PlatformConstant)` with an `enum class` key — never a string or a raw index.
- `ACHV_MAX_ENTRIES` (`EngineAchievement.h`) is a **format** constant by this test, not a platform budget: the
  on-disc trophy container and `tools/vita_package.py` both depend on it, so it is identical everywhere and its two
  copies are checked against each other by `tools/tests/test_vita_package.py`.
- **Naming Rule**: unchanged — `<ENGINE_CATEGORY>_<SUBMODULE>_<ID>` (e.g., `IO_FILE_MAX_PATH`).
- **No Magic Numbers**: any numeric or string literal used for configuration or logic limits must be extracted to one
  of the locations above.
- **Memory Safety**: the memory map is owned by the platform (`Platform::GetMemory().Reserve`), which enforces its own
  ceiling. On PS2 the total allocation (Arenas + Main Pool) MUST NOT exceed **30MB**.
- **Panic System**: Use `Engine_Panic(const char *message)` for unrecoverable errors. This will trigger a Red Screen of
  Death (BSOD) on debug builds.
