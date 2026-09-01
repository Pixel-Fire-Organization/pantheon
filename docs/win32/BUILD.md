# Building for Win32

Cross-compiled from Linux or WSL with MinGW-w64. Cross-compiling is deliberate:
the same command in the same environment produces both a console disc image and a
Windows executable, and every compiler extension the engine already relies on
carries over unchanged.

## Prerequisites

```bash
sudo apt install mingw-w64
```

Nothing else is installed by hand. The graphics library the default renderer
needs is a pinned prebuilt, fetched at configure time and verified by checksum —
not vendored source and not a submodule, because it is a binary release rather
than something this project builds.

## Build

```bash
python3 tools/build.py debug --platforms WIN32      # -> dist/win32/
python3 tools/build.py release --platforms WIN32
```

Presets `win32-debug` and `win32-release` configure the same builds directly.

Two toolchain settings are not optional and are worth knowing about, because both
produce confusing symptoms if changed:

- The engine logs sizes throughout using conversions the default C runtime here
  does not understand; the toolchain selects the standards-conforming runtime
  instead. Without it, logging prints garbage rather than numbers.
- The compiler runtime is linked statically, so the executable runs on a machine
  with no toolchain installed. A distribution directory that needs a runtime
  installed is not self-contained.

## Running

```
dist/win32/game.exe
```

The directory is self-contained: copy it anywhere and run it. Nothing refers back
into the build tree or into another platform distribution.

Launch options select a renderer, which is the primary debugging lever:

```
--renderer webgpu            the default
--renderer opengl            fallback; two internal paths
--gl-version 2.1 | 3.3       choose an OpenGL path explicitly
--renderer null              headless
--no-keyboard-pad            disable the keyboard-to-virtual-pad map
--help                       options, and what this build actually contains
```

Requesting a renderer this platform does not have reports the backends it
actually supports, not every backend the engine knows about.

## Debugging

Log output goes to the console, and also to an attached debugger when one is
present, so the engine is diagnosable without a console window. Output is flushed
as it is produced — a buffered log tells you nothing about a process that was
killed, which is exactly when it is needed.

Panics in a development build report where they happened and write a dump beside
the executable; a shipping build reports something a player can forward on. See
[DEBUG.md](../subsystems/DEBUG.md).

## Verification

The build must be clean with warnings treated as errors. A different compiler
target surfaces warnings the console build does not, and they are fixed rather
than suppressed.

Beyond that, the build boots into the game. Open the debug testbed with its
chord — Tab and Escape together on the keyboard, Back and Start on a controller —
and work [TESTBED.md](../TESTBED.md), which lists every scene and what correct
looks like. This platform's own checks are:

- The testbed should be navigable **by keyboard, with no game-code change** —
  that is the plug-and-play test, since the content was written for a gamepad —
  and also by a connected controller. Repeat with `--no-keyboard-pad`, where the
  chord and the menu should be reachable only from a controller.
- The cursor should follow the mouse, and the left stick should drive it as well.
  This is the only platform with a mouse, so it is the only one where that source
  is exercised.
- Resize the window while the screen-and-aspect scene is showing. The reported
  framebuffer must follow it, and the square must stay square.
- Both renderers should produce the same frame, interface included; a difference
  between them means one of the two is wrong.
- Load a level and return from it in the level-streaming scene, which exercises
  the container and model release paths.
- Build release as well. The testbed must be absent from that binary, not merely
  switched off.
