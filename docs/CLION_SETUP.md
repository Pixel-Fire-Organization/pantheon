# CLion Setup Guide

Configures CLion to build, run and analyse every platform through the WSL
toolchain. Anything the terminal can do, CLion can do — the presets and targets
below are the same ones `tools/build.py` and `cmake --build` drive.

---

## Prerequisites

| Requirement | Details |
| :--- | :--- |
| **CLion** | 2023.1 or newer (CMakePresets.json v6 support required) |
| **WSL2 + Ubuntu** | With the toolchains for the platforms you build |
| **python3** | In WSL, for the asset pipeline and the tool tests |

Per platform, only what you actually build:

| Platform | Needs |
| :--- | :--- |
| PS2 | `PS2DEV` toolchain at `/usr/local/ps2dev` ([PS2SDK_SETUP.md](ps2/PS2SDK_SETUP.md)), `genisoimage` for the ISO, PCSX2 to run |
| Win32 | `sudo apt install mingw-w64` |
| Vita | `VITASDK` at `/usr/local/vitasdk`, plus `vdpm install vitaShaRK taihen libmathneon` and `psp2cgc` in `external/psp2cgc/` ([vita/BUILD.md](vita/BUILD.md)), Vita3K to run |
| PSP | `PSPDEV` at `/usr/local/pspdev` ([psp/BUILD.md](psp/BUILD.md)), PPSSPP to run |
| nx | Docker Engine in WSL for `tools/build.py`'s container build, or `DEVKITPRO` at `/opt/devkitpro` with `switch-dev switch-mesa switch-glad` for the presets ([nx/BUILD.md](nx/BUILD.md)), Ryujinx to run |

> `PS2DEV` and `VITASDK` do **not** need to be in your `~/.bashrc` for CLion —
> the default paths are set in `CMakePresets.json`. You only need them exported
> to build from a terminal. For a non-standard path see
> [Troubleshooting](#troubleshooting).

---

## 1. Configure the WSL Toolchain

1. **Settings** → **Build, Execution, Deployment** → **Toolchains**.
2. **+** → **WSL**, and select your distribution (e.g. `Ubuntu`).
3. CLion auto-detects `cmake`, `make` and `gdb`. Verify they appear in green.
4. Rename the entry to exactly **`WSL`** — it must match the `vendor` block in
   `CMakePresets.json`.
5. Move it **to the top** of the list.

Presets ending in `-linux` are the same builds bound to CLion's `Default`
toolchain, for running CLion on a Linux host rather than Windows + WSL.

---

## 2. CMake Profiles

`CMakePresets.json` is built from hidden base presets — one per toolchain, one
per configuration, one per host — that the concrete presets inherit. Adding a
platform is a base plus its rows, not another block of copied JSON.

Which platforms a configure produces comes from `PLATFORMS_TO_SUPPORT`, filtered
against the active toolchain.

| Preset | `PLATFORMS_TO_SUPPORT` | Build directory |
| :--- | :--- | :--- |
| `ps2-debug` / `ps2-release` | `PS2PAL;PS2NTSC` | `build/ps2dev-<cfg>` |
| `ps2-<cfg>-pal` | `PS2PAL` | `build/ps2dev-<cfg>-pal` |
| `ps2-<cfg>-ntsc` | `PS2NTSC` | `build/ps2dev-<cfg>-ntsc` |
| `win32-debug` / `win32-release` | `WIN32` | `build/mingww64-<cfg>` |
| `vita-debug` / `vita-release` | `VITA;VITATV` | `build/vitasdk-<cfg>` |
| `vita-<cfg>-handheld` | `VITA` | `build/vitasdk-<cfg>-handheld` |
| `vita-<cfg>-tv` | `VITATV` | `build/vitasdk-<cfg>-tv` |
| `psp-debug` / `psp-release` | `PSP` | `build/pspdev-<cfg>` |
| `nx-debug` / `nx-release` | `NX` | `build/devkita64-<cfg>` |

Each has a `-linux` twin. `<cfg>` is `debug` or `release`.

Build directories match what `tools/build.py` uses, so a terminal build and an
IDE build of the same preset share one tree instead of compiling twice.

The retired `REGION` option no longer exists: broadcast region is platform
identity, so `ps2pal` and `ps2ntsc` are separate platforms rather than one
platform with a switch. See [PLATFORMS.md](PLATFORMS.md).

All fields in these profiles (including the toolchain dropdown) are **grayed
out** — expected CLion behaviour for preset-based profiles. The toolchain comes
from the `vendor` block.

---

## 3. Building, running and testing

Select a profile from the dropdown, then a target next to the hammer (▲).

| Target | Does |
| :--- | :--- |
| `dist` | The whole pipeline for every platform in the profile, into `dist/<platform>/` |
| `app_<platform>` | Just that platform's executable |
| `run-ps2pal`, `run-ps2ntsc` | Launch the disc image in PCSX2 |
| `run-vita`, `run-vitatv` | Launch the package in Vita3K |
| `run-psp` | Launch the memory-card container in PPSSPP |
| `run-nx` | Launch `dist/nx/game.nro` in Ryujinx |
| `run-win32` | Launch `dist/win32/game.exe` |
| `test-tools` | Run the tool test suite (`pytest tools/tests`) |
| `cook-<platform>` / `package-<platform>` | Individual pipeline stages |
| `clean-all` | Wipe every build artefact |

`run-*` targets exist in **release as well as debug**, so a release build can be
launched without dropping to a terminal.

The build presets in the preset dropdown cover the same ground: one per
configure preset targeting `dist`, plus `run-ps2pal`, `run-ps2ntsc`,
`run-win32`, `run-vita`, `run-vitatv`, `run-psp`, `run-nx` and `test-tools`.

### How launching works

Every `run-*` target calls [tools/run_target.py](../tools/run_target.py), which
picks the launcher from the artifact extension — `.iso` → PCSX2, `.vpk` →
Vita3K, `.pbp` → PPSSPP, `.nro` → Ryujinx, `.exe` → run it directly. It converts the path for a Windows-side
emulator when the build ran under WSL, launches detached, and if the emulator is
missing prints every path it searched.

From a terminal it is the same one command:

```bash
python3 tools/run_target.py dist/ps2pal/engine.iso
python3 tools/run_target.py dist/vita/PSEN00001.vpk
python3 tools/run_target.py dist/win32/game.exe
python3 tools/run_target.py dist/nx/game.nro

# or name the emulator explicitly
python3 tools/run_target.py dist/ps2pal/engine.iso "C:/path/to/pcsx2-qt.exe"
```

---

## 4. Build Architecture

The hammer runs `cmake --build <binaryDir> --target <target>`. `CMakeLists.txt`
drives the pipeline in order:

1. **Configure time**: reads each platform's package config, compiles Vita
   shaders, generates `.clangd` for IDE analysis.
2. **Build time**: third-party dependencies → engine → game → cook → package →
   distribution. See [PIPELINE.md](PIPELINE.md).

Third-party libraries are only rebuilt when their `.a` is missing. Incremental
builds recompile only changed sources.

`tools/build.py` is the terminal equivalent and targets the same directories:

```bash
python3 tools/build.py [debug|release] [--platforms VITA,VITATV]
```

---

## 5. First-Time Workflow

1. Pick a profile (e.g. `Vita Debug (Handheld + PS TV)`) and `dist` as the
   target, then click the hammer (▲).
2. **Reload CMake** afterwards: **Tools** → **CMake** → **Reload CMake Project**
   — this picks up `compile_commands.json` and the generated `.clangd` so
   toolchain headers resolve in the editor.
3. To run, select the matching `run-*` target and click the hammer.

---

## Troubleshooting

### `PS2DEV` / `VITASDK` environment variable is not set

The presets set the default paths. For a toolchain elsewhere, create
`CMakeUserPresets.json` at the project root (gitignored) and override just the
environment:

```json
{
    "version": 6,
    "configurePresets": [
        { "name": "ps2-debug",  "inherits": "ps2-debug",  "environment": { "PS2DEV": "/your/path" } },
        { "name": "vita-debug", "inherits": "vita-debug", "environment": { "VITASDK": "/your/path" } }
    ]
}
```

### Emulator not found

The launcher prints every path it searched. Point it at your install:

```bash
export PCSX2_PATH="/mnt/c/Games/PCSX2/pcsx2-qt.exe"
export VITA3K_PATH="/mnt/c/Vita3K/Vita3K.exe"
export RYUJINX_PATH="/mnt/c/riujinx/Ryujinx.exe"
```

CLion does not always inherit a login shell's environment, so set these in
**Settings** → **Build, Execution, Deployment** → **Toolchains** → *Environment*
if a terminal finds the emulator but the IDE does not.

### `psp2cgc not found`

The Vita default renderer compiles its shaders at build time and the compiler is
deliberately not committed. Fetch it into `external/psp2cgc/` or set `PSP2CGC`.
See [vita/BUILD.md](vita/BUILD.md).

### Code analysis does not resolve toolchain headers

1. Confirm CLion ran CMake at least once (check the **Build** tab).
2. `.clangd` is generated during configure — ensure a configure has succeeded.
3. **Tools** → **CMake** → **Reload CMake Project**.
4. If CMake reports missing compilers, verify the WSL toolchain is named exactly
   `WSL`.

### CMake profiles are missing or show an error

1. Open CLion from the **project root**, not a subdirectory.
2. Configure the WSL toolchain (Step 1) before CMake loads.
3. **File** → **Invalidate Caches** → **Invalidate and Restart**.

### `genisoimage` / `mkisofs` not found (no ISO generated)

```bash
sudo apt install genisoimage
```
