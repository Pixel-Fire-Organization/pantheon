# Building for macOS

Built natively on a Mac with Apple's clang. There is nothing to cross-compile and no
container, and the build needs **no submodule**: a macOS build never clones the console SDK
sources the other platforms use.

## Prerequisites

- **Xcode or the Command Line Tools**, for the compiler, the framework headers and the
  `lipo`, `otool` and `codesign` tools the packaging step runs.
- **CMake 3.19 or newer.** The platform reads the title declaration with CMake's JSON support and
  builds Objective-C++.
- **Python 3.10 or newer**, with the packages in `tools/requirements-ci.txt`. The pinned imaging
  library does not support the Python 3.9 that some Macs still carry as the system interpreter. A
  virtual environment is the simplest route:

```bash
python3 -m venv .venv && . .venv/bin/activate
python3 -m pip install -r tools/requirements-ci.txt
```

- **Git LFS**, for the textures. Without it the source art is a pointer file and cooking fails
  naming it.

The graphics runtime library is a pinned prebuilt, fetched at configure time for each architecture
being built and verified against its own digest. It is not vendored source and not a submodule.

## Build

```bash
python3 tools/build.py debug      # -> dist/macos/<Title>.app
python3 tools/build.py release
```

On a Mac, asking for no platform builds `macos`: it is the only platform whose toolchain exists
here. Presets `macos-debug` and `macos-release` configure the same builds directly.

**Architectures.** A build carries both Intel and Apple Silicon code unless told otherwise, which is what
`tools/build.py` produces in either configuration. The **debug preset builds only the host's own
architecture**, which halves the compile time and fetches one library instead of two; the release preset builds
both. Ask for either explicitly:

```bash
MACOS_ARCHITECTURES=native python3 tools/build.py debug          # the host's own
MACOS_ARCHITECTURES="arm64;x86_64" python3 tools/build.py debug  # universal
cmake --preset macos-release                                    # universal
```

**The environment variable is read once.** A build directory keeps the architectures it was first configured with, so
to change them, pass `MACOS_ARCHITECTURES` on the CMake command line or configure into a fresh directory. That is also why the
debug preset has a directory of its own, `build/macos-native-debug`, instead of sharing `build/macos-debug` with
`tools/build.py`. A universal executable built on an Intel Mac runs only its Intel half there. **The Apple Silicon half is
compiled, linked and signed on any Mac, but can only be run on an Apple Silicon one.**

The build must be clean with warnings treated as errors. A different compiler surfaces warnings the
other platforms' do not, and they are fixed rather than suppressed.

## Running

```
dist/macos/<Title>.app
```

The bundle is self-contained: copy it anywhere and open it. Nothing refers back into the build tree.
To keep the engine's log on the terminal, run the executable inside it:

```bash
dist/macos/Pantheon.app/Contents/MacOS/game --renderer webgpu
cmake --build build/macos-debug --target run-macos
```

Launch options select a renderer, which is the primary debugging lever:

```
--renderer webgpu            the default; Metal
--renderer opengl            fallback; Apple's OpenGL, a 4.1 core profile
--renderer null              headless
--shaders <directory>        read the shaders from here instead of the bundle's own
--no-keyboard-pad            disable the keyboard-to-virtual-pad map
--log-input                  report every key and pad change
--help                       options, and what this build actually contains
```

Requesting a renderer this platform does not have reports the backends it actually supports.

**Editing a shader needs no build at all.** Run with `--shaders assets/engine/shaders` and a change to a
file there takes effect on the next launch. See [formats/SHADER_ASSETS.md](../formats/SHADER_ASSETS.md).

**Gatekeeper.** The bundle is signed ad hoc, which is enough for it to run on the Mac that built it. A bundle
copied to another Mac over a network is quarantined by the system and refused until it is notarised, which
this build does not do. Clearing the quarantine attribute on a copy is a development convenience, not a
distribution method.

## Examples

```bash
cmake --build build/macos-debug --target examples
```

Each example is staged as its own bundle, named for the example, under `examples/dist/<name>/macos/`.

## Debugging

Log output goes to standard output, flushed as it is produced. A panic in a development build shows the
stack and aborts; a shipping build shows a message a player can forward. Both go through a modal alert only
when raised on the main thread — a panic on the IO worker prints and aborts without one, since the
windowing system may be called from the main thread only. See [DEBUG.md](../subsystems/DEBUG.md).

## Verification

The build must be clean, and the platform's own checks are:

- Run each renderer, `webgpu`, `opengl` and `null`, and confirm the log names the platform and the renderer
  and the heartbeat shows the frame rate. `null` is headless.
- Both real renderers should produce the same frame, interface included; a difference between them means
  one is wrong.
- Open the debug testbed with its chord — Tab and Escape together on the keyboard — and work
  [TESTBED.md](../TESTBED.md). The testbed should be navigable by keyboard with no game-code change, and
  absent from a release binary.
- Resize the window while the screen-and-aspect scene is showing; the reported framebuffer must follow it
  and the square must stay square.
- Load a level and return from it in the level-streaming scene.
- **On Apple Silicon**, run the same checks. The Apple Silicon half of a universal build has not been run on
  hardware by the people who wrote this document, and this is the list to run when it first is.
- `lipo -info` on the executable and on the bundled library shows both architectures, and
  `codesign --verify --deep --strict` passes on the bundle.
- Build release as well.

## Known blockers

- **The system Python is too old** for the pinned imaging library; see the prerequisites.
- **Without Git LFS the cook fails** naming a texture, because the file in the working tree is a pointer.
