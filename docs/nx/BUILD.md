# Building for Nintendo Switch (`nx`)

Cross-compiled from Linux or WSL, alongside the other console and desktop
targets, so one command in one environment produces every distribution this
project ships.

## Prerequisites

The toolchain is devkitPro's devkitA64 with libnx, Mesa and glad. There are two
ways to have it, and `tools/build.py` picks between them on its own.

### In a container (the default when the host has no devkitPro)

devkitPro publishes the toolchain as a container image with every package this
platform needs already installed. The build uses it, pinned by tag and digest,
with the Python modules the cook and packaging tools import added on top — see
`tools/docker/devkita64/Dockerfile`. All the host needs is Docker Engine in WSL:

```bash
sudo apt install docker.io
sudo systemctl enable --now docker     # WSL needs systemd=true in /etc/wsl.conf
sudo usermod -aG docker $USER          # then open a new WSL shell
docker run --rm hello-world
```

The first build pulls devkitPro's image and builds the local one, which takes a
few minutes and a few gigabytes; later builds reuse it. The local image is
tagged with a hash of the Dockerfile, so changing the Dockerfile builds a new one
rather than silently reusing a stale image.

The checkout is mounted into the container at the same path it has in WSL, and
the container runs as your user. That keeps the build tree and `dist/nx/` exactly
where a host build would put them, owned by you, and it keeps the paths cached in
the build tree meaningful on both sides.

This is also the route that works where the next one does not: **devkitPro's own
servers refuse some networks**, answering the installer and the package
repository with a block page, and then the installer downloads as an empty file
and does nothing, silently. The image comes from a container registry instead.
Do not work around the block.

### On the host

```bash
wget https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x ./install-devkitpro-pacman
sudo ./install-devkitpro-pacman
sudo dkp-pacman -S switch-dev switch-mesa switch-glad
```

`switch-dev` brings the compiler, libnx, the default renderer's library and its
offline shader compiler, the packaging tools and the CMake support files.
`switch-mesa` and `switch-glad` bring the reference renderer's driver and its
function loader; `switch-mesa` pulls in the kernel-driver shim it needs.

The package manager installs `/etc/profile.d/devkit-env.sh`, which exports
`DEVKITPRO=/opt/devkitpro`. `tools/build.py` enters WSL through a login shell, so
that export is seen without anything added to a shell startup file. The build
refuses to configure without `DEVKITPRO`, naming the variable.

A host install is recognised by `$DEVKITPRO/cmake/Switch.cmake` existing, and is
used in preference to the container. `--container always` or
`--container never` overrides the choice.

Verify a host install before building anything:

```bash
$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc --version
ls $DEVKITPRO/cmake/Switch.cmake $DEVKITPRO/portlibs/switch/lib/libEGL.a
ls $DEVKITPRO/tools/bin/uam $DEVKITPRO/tools/bin/elf2nro $DEVKITPRO/tools/bin/nacptool
```

**Nothing is vendored.** Both renderers are served by libraries the toolchain
distributes, so this platform has no submodule, no third-party build step and no
component the player has to install.

## Build

```bash
python3 tools/build.py debug --platforms NX
python3 tools/build.py release --platforms NX
```

Or through the presets, `cmake --preset nx-debug` and
`cmake --build --preset nx-debug` — on a host install only. A preset runs CMake
directly and cannot enter the container; an IDE that should build through the
container needs the image configured as its own Docker toolchain.

**Do not mix the two routes in one build tree.** A tree configured in the
container caches the container's compiler paths, and a host CMake reading it
finds nothing there (and the reverse). Delete `build/devkita64-<cfg>` when
switching.

Omitting `--platforms` builds every platform the active toolchain supports;
naming a platform this toolchain cannot build is a hard error naming the
toolchain file it would need instead.

Notes on the toolchain settings, and the symptom each produces if changed:

- **The vendor toolchain file is included, not replaced — and this project's
  flags are appended from a rules override, not from the toolchain file.**
  devkitPro sets its architecture flags, including the thread-pointer model
  libnx depends on, from its *platform* file, which CMake loads after the
  toolchain file has already run. Flags set from the toolchain file therefore
  either shadow those defaults or are shadowed by them, depending on the cache,
  and a build that loses the thread-pointer flag compiles and then faults on its
  first thread-local access. The rules override is the one hook CMake loads
  between the platform file and the cache being seeded; it includes devkitPro's
  own override first, since devkitPro installs that one only when none is set.
- **The toolchain's headers are system headers.** libnx trips
  `-Wmissing-field-initializers` inside its own inline functions under C++, and the
  default renderer's header declares a nested namespace that C++11 only accepts as
  an extension. Under `-Wall -Wextra -Werror` either one stops the build inside a
  file this project does not own. The toolchain file adds both include roots as
  system directories, which silences warnings from inside them and nowhere else.
- **devkitPro's CMake owns every name beginning `NX_`.** Its toolchain support
  defines the tool paths and flag variables under that prefix. This project's own
  variables for the platform use `PLATFORM_NX_` and `ENGINE_NX_` instead; a bare
  `NX_` variable in the fragment would silently shadow one of the vendor's.
- The engine is C++11 with `-fno-exceptions -fno-rtti`, as everywhere, and the
  engine and game targets build under `-Wall -Wextra -Werror`.

## Packaging

One container: `dist/nx/game.nro`, the executable with the title's name, author,
version and icon, and a read-only file system carrying the resource archive.
Packaging is gated on `tools/validate_cooked.py`, so a bad cook cannot reach it.
See [PACKAGING.md](PACKAGING.md).

## Running

```bash
python3 tools/run_target.py dist/nx/game.nro
```

Run it from WSL, not from the container: the emulator is a Windows program the
container cannot reach. On a host install the `run-nx` target
(`cmake --build build/devkita64-debug --target run-nx`) does the same thing.

`tools/run_target.py` launches Ryujinx, translating the path when the build ran
under WSL and the emulator is installed Windows-side. It looks in
`C:\riujinx\` and `C:\Ryujinx\`; set `RYUJINX_PATH` if it is somewhere else:

```bash
export RYUJINX_PATH=/mnt/c/riujinx/Ryujinx.exe
```

The emulator needs the console's keys and firmware installed through its own
interface before it will start anything.

**The engine's console output is in the emulator's log, not its window.** Enable
*Guest* logging in the emulator's logging settings (and file logging, to keep it);
every engine log line then appears in the newest file under the emulator's
`Logs` directory. That is where the heartbeat and the performance snapshot show
up when no debugger is attached.

**An emulator lies about time and memory.** It launches a homebrew title as an
application, so it never shows the smaller applet allowance a player launching
from the album gets, and it holds the refresh rate whatever the frame cost. Treat
its figures as proof that the engine runs and that its accounting is sane, never
as proof that content fits. See
[PLATFORM.md](PLATFORM.md#memory).

On hardware, copy `game.nro` anywhere under `switch/` on the SD card and start it
from the Homebrew Launcher. It is self-contained: it refers back to nothing in the
build tree. Its log file is written to the title's directory under `switch/`.

Launch options select a renderer, which is the primary debugging lever:

```
--renderer deko3d            the default; the direct interface
--renderer opengl            reference; desktop OpenGL through Mesa
--renderer null              headless
--help                       options, and what this build actually contains
```

Requesting a renderer this platform does not have reports the backends it
actually supports, not every backend the engine knows about.

The Homebrew Launcher passes no options of its own. To test a non-default
renderer on hardware, send the executable over the network with
`nxlink -s game.nro --renderer opengl`, which also returns the console output.

## Debugging

- Console output: the emulator's guest log; on hardware, the log file in the
  title's SD card directory, or the network link's output when launched with it.
- A panic writes its message to the log and shows it in the system error applet
  before the title exits, so it is visible on the console itself.
- Debug builds leave an unstripped executable, `main.debug.elf`, beside the
  container, which is what resolves an address from a crash report to a symbol.

## Known blockers

- **The toolchain cannot be installed from every network.** See
  [Prerequisites](#prerequisites).
- **The emulator does not model the applet memory allowance**, so the tightest
  launch configuration is only tested on hardware.
- **No sound**, as on every platform.

## Verification

A build is not finished because it linked. Beyond `-Wall -Wextra -Werror`
cleanliness alongside every other platform:

- `dist/nx/game.nro` runs copied alone, in the emulator and from the Homebrew
  Launcher.
- Each renderer produces the **same frame**; a difference means one of them is
  wrong.
- `--renderer null` runs headless.
- The performance snapshot prints sane timing, memory and budget figures and names
  `nx` and the active renderer.
- Docking and undocking (the emulator's docked-mode toggle) changes the
  framebuffer between 1280 x 720 and 1920 x 1080 in the screen-and-aspect testbed
  scene without a stall, on both renderers.
- A level loads and unloads cleanly.
- The text dialog opens the software keyboard, and returning from the HOME menu
  does not produce a hitch in the frame-pacing scene.
- Every example under `examples/` builds to its own `.nro` and runs.
