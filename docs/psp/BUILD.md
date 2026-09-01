# Building for PlayStation Portable

Cross-compiled from Linux or WSL, alongside the other console and desktop
targets, so one command in one environment produces every distribution this
project ships.

## Prerequisites

The toolchain is installed by its own distribution rather than from system
packages. A prebuilt release is the fastest route:

```bash
sudo apt install build-essential cmake git python3 curl xz-utils
curl -L -o pspdev.tar.gz \
  https://github.com/pspdev/pspdev/releases/latest/download/pspdev-ubuntu-latest-x86_64.tar.gz
sudo tar -xzf pspdev.tar.gz -C /usr/local
export PSPDEV=/usr/local/pspdev
export PATH=$PSPDEV/bin:$PATH
```

Building from source instead takes about an hour:

```bash
git clone https://github.com/pspdev/psptoolchain && cd psptoolchain && ./toolchain.sh
```

Export the two variables permanently, in the same place the other console
toolchain variables are already exported. The build refuses to configure without
`PSPDEV`, with a message naming the variable — the same treatment the other
console toolchains give a missing root.

Verify before building anything:

```bash
psp-gcc --version
ls $PSPDEV/psp/share/pspdev.cmake
which mksfoex pack-pbp psp-fixup-imports
```

**Nothing else is needed.** Unlike the Vita, this platform has no third-party
submodule, no offline shader compiler and no dependency the player has to
install. The fallback renderer's library ships inside the toolchain as
`psp/lib/libGL.a` alongside `psp/lib/libpspvram.a` and the `GL/` headers, so it
is linked the way any other SDK library is. That is checked at configure time and
reported, not assumed.

The disc container is built with `mkisofs`, which this project already requires
for the PlayStation 2 target:

```bash
sudo apt install genisoimage
```

## Build

```bash
python3 tools/build.py debug --platforms PSP
python3 tools/build.py release --platforms PSP
```

Omitting `--platforms` builds every platform the active toolchain supports;
naming a platform this toolchain cannot build is a hard error naming the
toolchain file it would need instead.

Notes on the toolchain settings, and the symptom each produces if changed:

- **The vendor toolchain file is included, not replaced.** `$PSPDEV/psp/share/
  pspdev.cmake` sets the compilers, the sysroot include and link paths and the
  firmware-version definition. This project's own flags are **appended** to what
  it establishes, behind a cache guard, because a toolchain file is re-read on
  every compiler probe and forcing the flag variables there discards the
  vendor's. That is the failure that cost the Vita its relocation table.
- **`-G0` is not optional.** Without it the compiler places small objects in a
  global-pointer-relative section the linker script does not size for, and the
  link fails with a relocation truncation that names an unrelated object file.
- The engine is C++11 with `-fno-exceptions -fno-rtti`, as everywhere, and the
  engine and game targets build under `-Wall -Wextra -Werror`.

## Packaging

Two containers, from one staged tree, both in `dist/psp/`. Packaging is gated on
`tools/validate_cooked.py`, so a bad cook cannot reach either of them. See
[PACKAGING.md](PACKAGING.md) for the layouts and for what each slot holds.

The memory-card container is the one to use on hardware; the disc image exists so
this console can be compared against the PlayStation 2 on equal terms and so the
emulator has something to boot directly.

## Running

```bash
cmake --build build/pspdev-debug --target run-psp
```

`tools/run_target.py` launches the emulator, translating the path when the build
ran under WSL and the emulator is installed Windows-side. Set `PPSSPP_PATH` if it
is somewhere the search paths do not cover:

```bash
export PPSSPP_PATH=/mnt/c/ppsspp/PPSSPPWindows64.exe
```

A disc image and a PlayStation 2 disc image share an extension, so the fragment
passes the launcher explicitly rather than letting it be inferred from the file
name. A `.iso` handed to `run_target.py` with no launcher named still goes to the
PlayStation 2 emulator, which is the existing behaviour and is left alone.

**Set the emulator to the original hardware model with 32 MB before trusting
anything it tells you about memory.** Its default is a later model with the
extended memory region present, which reports well over thirty megabytes free —
memory that does not exist on the hardware this build targets. A reservation that
succeeds under the default and fails on an original console is the single most
likely way to waste an afternoon here. See [PLATFORM.md](PLATFORM.md#memory).

On hardware, copy `dist/psp/` to `PSP/GAME/<name>/` on the memory card. The
folder is self-contained: it carries the executable, the asset container and the
compiled worlds, and refers back to nothing in the build tree.

Launch options select a renderer, which is the primary debugging lever:

```
--renderer gu                the default; the native graphics interface
--renderer pspgl             fallback; a simpler fixed-function drawing model
--renderer null              headless
--help                       options, and what this build actually contains
```

Requesting a renderer this platform does not have reports the backends it
actually supports, not every backend the engine knows about.

Launch options reach the console through the emulator command line. On hardware
there is no argument vector to speak of, so a build that must be tested with a
non-default renderer is built with that default changed.

## Debugging

Console output goes to the emulator's log window, and on hardware to a log file
in the title's directory on the memory card. A title that fails before the log
file is open has nothing to show for it on hardware, which is why the emulator is
the first place to reproduce anything.

Debug builds leave an unstripped executable beside the packaged one, which is
what the emulator needs to resolve a symbol from an address. Release builds strip
it.

A panic draws its message through the interface, so it is visible on the console
itself rather than only in a log — the same treatment the PlayStation 2 gives it.

## Known blockers

- **Argument vectors do not really exist on hardware.** The launch options above
  work under the emulator and over the development link, not from the system
  menu. Testing a non-default renderer on hardware means changing the default and
  rebuilding.
- **The disc image carries an unencrypted executable.** The emulator runs it, and
  so do loaders that tolerate it; retail hardware does not. The memory-card
  container is the one that works everywhere.
- **No sound**, as on every platform.

## Verification

A build is not finished because it linked. Beyond `-Wall -Wextra -Werror`
cleanliness alongside every other platform:

- `dist/psp/` runs by copying the folder alone onto a memory card, and the disc
  image boots — both, since both ship.
- Each renderer produces the **same frame**; a difference means one of them is
  wrong.
- `--renderer null` runs headless.
- The performance snapshot prints sane timing, memory and budget figures and
  names this platform and the active renderer. It is the best single smoke test
  here, and on this platform its memory figures are the ones worth reading
  closely.
- A level loads and unloads cleanly — that exercises the sector-streaming and
  model release paths, which little else reaches.
- The hardware menu button ends the title through the ordinary shutdown path
  rather than appearing to hang.
- The debug testbed's dialog scenes behave identically on both renderers, which
  is what proves the frame-servicing hazard in
  [PLATFORM.md](PLATFORM.md#system-dialogs-are-drawn-into-the-titles-own-frame)
  is handled in both.
