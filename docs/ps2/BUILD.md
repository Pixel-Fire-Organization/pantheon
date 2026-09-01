# Building for PlayStation 2

Two platforms build from this toolchain: `ps2pal` and `ps2ntsc`. Each produces
its own executable, its own disc image and its own distribution directory, so a
build can never boot in the wrong video mode.

Toolchain installation is in [PS2SDK_SETUP.md](PS2SDK_SETUP.md). Disc image
details are in [ISO_GENERATION.md](ISO_GENERATION.md). Debugging under emulation
is in [PCSX2_DEBUGGING.md](PCSX2_DEBUGGING.md).

## Build

```bash
python3 tools/build.py debug pal        # -> dist/ps2pal/
python3 tools/build.py debug ntsc       # -> dist/ps2ntsc/
python3 tools/build.py debug            # both, one configure
python3 tools/build.py release pal
```

The region words are shorthand for selecting a platform. With none given, every
platform the active toolchain can build is built, each into its own distribution.

Presets are available for editors and for direct use: `ps2-debug-pal`,
`ps2-debug-ntsc`, `ps2-release-pal`, `ps2-release-ntsc`, and combined `ps2-debug`
and `ps2-release`. Variants ending in `-linux` configure the same builds for a
Linux host.

The set of platforms a configure produces is chosen by a build option that
defaults to every known platform and is then filtered against the active
toolchain. A build naming a platform this toolchain cannot produce is an error
rather than a silent omission.

## Which platforms exist

The abstract PS2 platform is not selectable. Only the two regional variants are,
because every region-varying value has exactly one correct answer in a given
build. See [PLATFORM.md](PLATFORM.md).

## Running

```bash
cmake --build build/ps2dev-debug --target run-ps2pal
```

Or invoke the launcher directly, which is what that target does:

```bash
python3 tools/run_target.py dist/ps2pal/engine.iso
```

Launch arguments select a renderer, which is the primary debugging lever:

```
--renderer giftag      the default
--renderer ps2gl       required for level geometry; see below
--renderer null        headless
--help                 options, and what this build actually contains
```

On hardware and in emulators, arguments come from whatever launched the
executable. A plain disc boot passes none, so every option falls back to its
default and behaves as a shipped build would.

## The vector-microcode assembler

**The assembler shipped with the standard toolchain faults on every input**,
including an empty file, so the third-party microcode library cannot build with
it and no executable can be linked. The repository carries a working rebuild and
the external build is pointed at it explicitly rather than relying on search
order. Without that file present the build falls back to the toolchain's own and
fails at the same place. Do not work around this by editing the third-party
library.

## Verification

Both variants must build clean with warnings treated as errors. Beyond that, the
performance snapshot is the best smoke test the engine has — it reports timing
against the frame budget, memory occupancy, texture budget, input state and the
active platform and renderer in one screen. Sane numbers there mean the engine is
working.

Confirm the two distributions are genuinely independent: each holds a complete
disc image, their boot configurations differ only in video mode, and each boots
into its own mode with the snapshot reporting the matching screen height and
frame budget.

The build boots into the game. Open the debug testbed with its chord — Select
and Start together — and work [TESTBED.md](../TESTBED.md). This is the platform
the testbed was budgeted against, so two of its checks matter more here than
anywhere else:

- **No interface overflow in the log.** A single dropped-quad or dropped-rect
  line means the scene showing at the time does not fit this framebuffer and
  must be paged. Neither desktop nor handheld can catch this: only this platform
  has a fixed screen-space ceiling.
- **The frame-pacing scene should differ between the two regions in exactly one
  way.** The target reads 20.00 ms against 16.67 ms, and the moving bar and the
  one-second pulse should still run at the same real speed in both. If they do
  not, something is counting frames where it should be counting time.

Also compare the two backends on the same scene — the two should be equivalent,
so **any** difference between them is a defect in one of them. Do this with a
capture rather than by eye: a defect on this platform can leave a scene that
still reads as plausible, and the one that cost the most to find rendered every
triangle as a screen-aligned rectangle while keeping the horizon in the right
place. See "Capturing a run" below.

## Capturing a run

Rendering defects here are found by looking at a frame and at the console log,
and by putting one scene through both backends and comparing. `emu_capture`
(`tools/ps2/emu_capture.py`) does that without anyone driving the emulator by
hand, and it is where the awkward parts of doing so are recorded:

    python3 tools/ps2/emu_capture.py dist/ps2pal/engine.iso --log run.log
    python3 tools/ps2/emu_capture.py dist/ps2pal/engine.iso --shot giftag.png --native-shot
    python3 tools/ps2/emu_capture.py dist/ps2pal/engine.iso --shot ps2gl.png --native-shot --renderer ps2gl

It finds the emulator the same way the run target does, including the
environment override, so there is one answer to where the emulator is. A log
capture works on any host; a frame capture and sending input need the Windows
desktop, and are refused elsewhere with that reason rather than a black image.

Three things about capturing are worth knowing before trusting a frame:

- **A launch argument needs a placeholder in front of it.** The emulator hands
  the executable its arguments without prepending the path it booted, so the
  first token is taken as the program name and the first real option is eaten
  silently — the engine then runs with defaults and the capture quietly shows
  the wrong backend. The tool adds the placeholder; anyone driving the emulator
  by hand must too.
- **Never capture a fullscreen surface.** An exclusively-fullscreen window reads
  back as a black rectangle, which looks exactly like a backend that drew
  nothing. This has already cost one wrong diagnosis. The tool never requests
  fullscreen.
- **`--native-shot` asks the emulator for the frame** rather than reading the
  window, which yields the display output at its own size with no window border
  and no overlay across it. That is what makes two captures comparable. It costs
  a dependency on the emulator's screenshot key and on where it files snapshots,
  so the tool reads the location out of the log rather than assuming it. Without
  the flag the window is captured instead, which always works.

Give a capture long enough to settle (`--settle`, twenty seconds by default):
a frame taken during boot shows a loading screen, and content that streams in —
level materials among it — may not have arrived yet.
