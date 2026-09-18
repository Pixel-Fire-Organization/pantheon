<p align="center">
  <img src="docs/assets/pantheon-logo.svg" width="96" height="96" alt="Pantheon logo">
</p>

# Pantheon

A custom multi-platform game engine written in C++, targeting PlayStation 2, PlayStation Vita, PlayStation Portable, Win32 and Nintendo Switch, leveraging platform SDKs such as `ps2sdk` and `ps2gl` for native 2D/3D graphics, audio, input handling, and hardware acceleration.

Every subsystem carries a Greek-deity codename (Renderer is **Aphrodite**, Memory is **Mnemosyne**, and so on) — see the [Subsystems table](docs/PLATFORMS.md#subsystems) for the full pantheon.

## Requirements

To reliably build this engine from source, you must have the toolchain for at least one target platform properly installed and exported to your environment.

1. **PS2 Toolchain:** 
   - Follow the official instructions to install the modern [`ps2dev` toolchain](https://github.com/ps2dev/ps2dev).
   - **Environment:** Ensure your shell exports the `PS2DEV` environment variable (e.g. `export PS2DEV=/usr/local/ps2dev`).
   - **IDE Setup (Automatic):** Simply running `python3 ./tools/build.py` (see below) will automatically generate a `.clangd` file that configures your editor's highlighting for both WSL and Windows.

2. **Win32 Toolchain (optional, for Windows builds):**
   - Cross-compiled from WSL/Linux with MinGW-w64: `sudo apt install mingw-w64`.
   - Nothing else is installed by hand — the default renderer's graphics library is a pinned prebuilt fetched at configure time. See [docs/win32/BUILD.md](docs/win32/BUILD.md).

3. **PS Vita Toolchain (optional, for Vita builds):**
   - Install [VitaSDK](https://vitasdk.org) via `vdpm`, then `vdpm install vitaShaRK taihen libmathneon`.
   - **Environment:** export `VITASDK` (e.g. `export VITASDK=/usr/local/vitasdk`) and add `$VITASDK/bin` to `PATH`.
   - The default Vita renderer also needs an offline shader compiler (`psp2cgc`) placed in `external/psp2cgc/`.
     It is not committed. See [docs/vita/BUILD.md](docs/vita/BUILD.md).

4. **PSP Toolchain (optional, for PSP builds):**
   - Install a prebuilt [`pspdev`](https://github.com/pspdev/pspdev) release (or build `psptoolchain` from source).
   - **Environment:** export `PSPDEV` (e.g. `export PSPDEV=/usr/local/pspdev`) and add `$PSPDEV/bin` to `PATH`.
   - Nothing else is needed — unlike the Vita, this platform has no third-party graphics submodule. See [docs/psp/BUILD.md](docs/psp/BUILD.md).

5. **Nintendo Switch Toolchain (optional, for `nx` builds):**
   - **Easiest:** Docker Engine in WSL (`sudo apt install docker.io`, then add yourself to the `docker` group).
     `tools/build.py` builds `nx` inside devkitPro's pinned image when no host toolchain is installed.
   - **Or on the host:** install devkitPro pacman, then `sudo dkp-pacman -S switch-dev switch-mesa switch-glad`
     (`DEVKITPRO=/opt/devkitpro` is exported by the package manager's own profile script).
   - Nothing is vendored and no submodule is needed. See [docs/nx/BUILD.md](docs/nx/BUILD.md).

6. **Dependencies:**
   - **CMake (3.10+)**
   - **genisoimage** (Provides the `mkisofs` utility required for automatically bundling bootable `.iso` files):
     ```bash
     # Ubuntu / Debian / WSL environments
     sudo apt-get update
     sudo apt-get install genisoimage
     ```

7. **Submodules (Third-Party Dependencies):**
   - The engine links statically with custom local compilations of `ps2gl` and `ps2stuff` located within the `external/` directory to ensure perfect compatibility.
   - Vita builds additionally use `external/vitaGL` for the fallback renderer. Fetch all of them with
     `git submodule update --init --recursive`.

## Build Instructions

We provide a convenient script in the `tools/` directory to effortlessly wipe old caches and cleanly rebuild the toolchain via CMake.

### Windows / WSL / Linux
`tools/build.py` detects Windows and automatically re-invokes itself inside WSL, so the same command works everywhere:

```bash
# Every platform the toolchain supports (PS2: both PAL and NTSC)
python3 ./tools/build.py [debug|release]

# One region only
python3 ./tools/build.py debug pal
python3 ./tools/build.py debug --platforms PS2NTSC

# Other platforms
python3 ./tools/build.py debug --platforms WIN32          # -> dist/win32/
python3 ./tools/build.py debug --platforms VITA,VITATV    # -> dist/vita/, dist/vitatv/
python3 ./tools/build.py debug --platforms NX             # -> dist/nx/game.nro
```

Or drive CMake directly. `PLATFORMS_TO_SUPPORT` defaults to every known platform and is
filtered against the toolchain, so it usually needs no flag:

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/ps2dev.cmake -B build/ps2
cmake --build build/ps2 --target dist
```

## Running the Emulator

Every platform has a `run-<platform>` target that launches its own artifact -
PCSX2 for a PS2 disc image, Vita3K for a Vita package, Ryujinx for an nx container,
the executable itself on Windows. The emulator is chosen from the artifact, so the command is the same
shape everywhere:

```bash
cmake --build build/ps2dev-debug  --target run-ps2pal
cmake --build build/vitasdk-debug --target run-vita
cmake --build build/mingww64-debug --target run-win32
python3 ./tools/run_target.py dist/nx/game.nro   # nx builds in a container, so launch from WSL
```

The launcher can also be called directly, and takes an explicit emulator path as
an optional second argument:

```bash
python3 ./tools/run_target.py dist/ps2pal/engine.iso
```

Set `PCSX2_PATH`, `VITA3K_PATH` or `RYUJINX_PATH` if an emulator is installed somewhere the
search paths do not cover.

## Build Artifacts

Each platform gets its own self-contained bundle under `dist/`, so builds never mix:

```
dist/ps2pal/    main.elf  engine.iso  main.sym     SYSTEM.CNF VMODE=PAL
dist/ps2ntsc/   main.elf  engine.iso  main.sym     SYSTEM.CNF VMODE=NTSC
dist/win32/     game.exe  RASSETS.PS2R
dist/nx/        game.nro  main.elf     (the archive is inside game.nro)
```

- `main.elf` - the raw PS2 executable (handy for rapid testing over the network with `ps2client`).
- `engine.iso` - a self-bootable disc image for PCSX2 or real hardware.

### Custom ISO Assets

Any loose file you want to include verbatim in the generated `.iso` should be placed directly under `assets/` — not
inside `assets/maps/`, `assets/textures/`, or `assets/models/`, which are consumed by the level and cook pipelines
instead. Loose files are automatically bundled at the **root** of the ISO filesystem during the build process.

For example, a file at `assets/config.txt` will be accessible on the PS2 as `cdrom0:\\CONFIG.TXT;1`.

## Documentation

[docs/PLATFORMS.md](docs/PLATFORMS.md) indexes everything. Start there, or jump to:

| | |
|---|---|
| [Architecture](docs/ENGINE.md) | Layers, startup order, subsystems, the constants rule |
| [Guidelines](docs/guidelines/) | How to add a system, a platform or an example; how to write code that reads untrusted bytes, and how to audit for it; what a change on the frame path must obey, and how to check it — read before starting |
| [Game API](docs/APP_API.md) | The surface game code uses |
| [Build pipeline](docs/PIPELINE.md) | Compile, cook, package, distribute |
| [Authoring assets](docs/ASSET_AUTHORING.md) | Adding content |
| [Examples](docs/EXAMPLES.md) | Standalone per-capability example binaries, under `examples/` |
| [Testbed](docs/TESTBED.md) | The debug scenes, and how to reach them |
| [PlayStation 2](docs/ps2/PLATFORM.md) | Platform spec, budgets, renderers |
| [Win32](docs/win32/PLATFORM.md) | Platform spec, budgets, renderers |
| [PlayStation Vita](docs/vita/PLATFORM.md) | Platform spec, budgets, renderers |
| [PlayStation Portable](docs/psp/PLATFORM.md) | Platform spec, budgets, renderers |
| [Nintendo Switch](docs/nx/PLATFORM.md) | Platform spec, budgets, renderers |

Specs carry the reasoning that is deliberately not in the source — hardware
quirks, race conditions, renderer limits, budget ceilings. Read the relevant one
before changing what it describes.
