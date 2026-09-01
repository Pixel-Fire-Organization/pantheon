# Backlog — automated testing through emulators and windowing

Working notes for future sessions, not a commitment made in the change that
added them. A design for driving every shipped binary under its emulator (or,
on the desktop, under its own window) from a host script, with assertions,
rather than by a person reading a log and looking at a frame.

**Status.** Design recorded 2026-09-17. Nothing is built: no engine code, no
tool, no spec. Awaiting review before step 1 of the sequence at the end. Every
emulator interface named below was checked against upstream documentation or
source on that date, except two items marked *unverified* in
[Open questions](#open-questions).

Companion to [EXAMPLES.md](../EXAMPLES.md), whose binaries are the test
targets, and to the *Verification* sections of each platform's build document,
which are the checks this would automate.

## The gap

Every platform's build document ends with a verification list, and every item
on it is done by a person: boot, open the testbed, read the snapshot, compare
the two backends by eye or by capture. The only automation in the tree is
`tools/ps2/emu_capture.py`, which boots one PS2 image, waits, and hands back a
frame and a log — no assertion, PS2 only, and it cannot press anything more
precise than a key sequence sent to the emulator window. The
[Testbed spec](../subsystems/TESTBED.md) says it is not a test runner and must
not become one, and the [performance findings](performance_findings.md) record
that there is no performance gate in CI.

The pieces that make automation possible already exist, they are just not
joined up:

- The engine parses launch arguments through one parser and retains unknown
  options, so a harness can add flags without touching the platforms.
- Every log line goes through the platform console, and every emulator in use
  exposes that console as a host file.
- Every emulator in use has a scriptable seam beyond its command line: PCSX2
  has an IPC server, PPSSPP has a WebSocket debugger and a headless build,
  Vita3K takes app arguments and has a GDB stub module, and the Win32 window is
  the engine's own, so its message queue is the input device.

## Constraints the design keeps

- **No emulator vocabulary in the engine.** The engine side is a contract
  (flags, a log-line grammar, an input script, frame evidence); which emulator
  is underneath is the host runner's business.
- **No OS calls in shared code.** Anything that reads a framebuffer or a file
  is a platform or renderer contract addition, implemented on every platform,
  with an honest absence where it cannot be done.
- **No per-frame diagnostics.** Harness output is emitted at checkpoints and on
  change, per [PERFORMANCE.md](../guidelines/PERFORMANCE.md). A checkpoint
  every frame is a test of the log path, not of the engine.
- **Capabilities stay honest.** Scripted input never synthesises a device the
  platform lacks. A script that presses a key on the PS2 is a script error.
- **The Testbed stays a testbed.** The isolated targets are `examples/`; the
  game itself is the release-configuration target. Nothing here adds assertions
  to a Testbed scene.
- **Spec first.** This is a new engine facility and follows
  [NEW_SYSTEM.md](../guidelines/NEW_SYSTEM.md): the contract is written and
  reviewed before code.

## Engine side — the harness contract

Five pieces, all platform-neutral. The concrete seams each one sits on are
named so the next session does not have to find them again.

### 1. A harness launch mode

Three options on the existing parser, all ignored when absent:

| Option | Meaning |
|---|---|
| run for N frames | Exit through the same request `game::Exit` uses once the frame counter reaches N. The process exit code is the harness verdict on platforms that have one. |
| fixed delta time | Delta time is computed in one place in the engine update from the platform clock. The override replaces that value, so a scripted run integrates identically every time. Wall-clock pacing (vsync, the GS wait) is untouched — only what the game sees changes. |
| target scene | Which example scene to start in, where an example has several. Examples already cycle scenes through actions; this is a starting index. |

How the arguments arrive:

| Platform | Route |
|---|---|
| PS2 under PCSX2 | `-gameargs`, with the placeholder token `emu_capture` already prepends |
| Vita under Vita3K | `--app-args` (`-Z`), comma-separated |
| Win32 | argv |
| PSP under PPSSPP | *Unverified* — see Open questions. Fallback below. |

**Fallback: an options file.** Where no argument vector reaches the engine,
the harness reads one small text file of options from the resource root at
startup, once, in the same grammar as the command line. Under emulation the
resource root is a host directory on the PSP (the memory-stick folder) and on
the Vita, so the runner drops the file before launch and no rebuild is needed.
On the PS2 the root is inside the ISO, so there the file only helps if the
platform also looks on `host:`; whether it should is a spec decision.

### 2. Structured lines in the log

One fixed word inside the line, after the platform's level prefix, that no
other log line uses. Key-value pairs after it. One line per event, never per
frame. Proposed events, subject to the spec:

| Event | Carries |
|---|---|
| `ready` | platform, renderer that actually started, build configuration, framebuffer size, frame budget |
| `checkpoint` | frame number, logic/render/wait split, heap used, draw and cull counts, texture bytes, interface overflow count |
| `frame` | frame number and a framebuffer hash, or the path the frame was written to, where the platform can produce either (see 4) |
| `assert` | pass or fail, the check's name, the observed and expected values |
| `done` | frame count reached, exit code |

Why the log and not a socket: the log is the one channel every platform and
every emulator already delivers to the host as a file.

| Platform | Where the host reads it |
|---|---|
| PS2 / PCSX2 | `-logfile`; EE console output is what the platform's `printf` becomes. The EE console log source must be enabled in the emulator configuration the runner supplies. |
| PSP / PPSSPP | `--log=FILE` on the GUI build; module stdout is the headless build's compared output. The engine's own `engine.log` in the title's memory-stick directory is a host file as well. |
| Vita / Vita3K | The emulator's own log, plus the engine's `engine.log` under `ux0:data/<TITLE_ID>/`, which is `<pref-path>/ux0/data/<TITLE_ID>/` on the host (`%APPDATA%/Vita3K/` by default, or `portable/fs/` in portable mode). |
| Win32 | stdout, flushed per line already. |

The PS2 console scrubs non-printable bytes and mutes debug-level lines, so
harness lines are info level and printable ASCII.

### 3. Scripted input

Every Input query reads through the live platform pointer, and Action, the
Testbed and game code all query through Input. A **wrapping platform**,
installed at startup only when a script is given, therefore substitutes the
input snapshot for every consumer at once with no per-platform change:

- Forwards everything to the real platform except the input queries.
- Answers the input queries from the script's entry for the current frame,
  deriving rising and falling edges from its own previous and current entries,
  the same way the real platforms do.
- Answers capability queries exactly as the real platform does, and refuses at
  load time a script that names a device the platform reports absent.
- Records rather than replays when asked: writing the real snapshot per frame
  is the same wrapper with the direction reversed, which is how a session on a
  desktop with a real pad becomes a script replayable on a console.

The script is frame-indexed and device-level: per frame, per port, a button
mask, two sticks, two triggers; keys, mouse and touch contacts where the
platform has them. Authored as a small JSON declaration under `tools/` and
compiled to the fixed-size form the engine reads, the same pattern as every
other declaration in `game/config/`. The
[Action spec](../subsystems/ACTION.md) already records that playback is
straightforward to add; this is the device-level half of it, beneath Action,
so raw-device scenes and Action-driven scenes are both testable.

### 4. Frame evidence

Two kinds, both attached to `checkpoint` or `frame` lines:

- **Counters.** Already exist: the frame split, the renderer's per-frame
  statistics, heap and texture occupancy, the interface's overflow report.
  Nothing new to build; the harness reports them at checkpoints.
- **A framebuffer hash.** A renderer contract addition: hash (or copy out) the
  last presented frame, on request, at a checkpoint, never every frame. Cost
  and feasibility differ sharply per backend:

| Backend | Feasible | How |
|---|---|---|
| PSP gu / pspgl | Yes, cheap | Video memory is CPU-mapped; hash the draw buffer |
| Vita gxm / vitagl | Yes, cheap | The display buffer is CPU-visible memory |
| Win32 opengl | Yes | Pixel readback after present |
| Win32 webgpu | Yes | Copy the swapchain image to a buffer and map it |
| PS2 giftag | Possible, costly | A GS local-to-host transfer through the GIF; not worth building first |
| PS2 ps2gl | No | The library has no framebuffer readback path |
| null | No | Nothing was drawn |

A backend that cannot answer says so, the same honest absence
`RenderToImage3D` already uses. On the PS2 the emulator's own screenshot is the
evidence instead (see PCSX2 below); the runner hashes it host-side. A hash
proves "same as last run", which is the regression check. Equivalence between
two backends is an image diff with a tolerance, not a hash comparison, because
the backends legitimately differ by a pixel here and there.

### 5. A host-visible result file

For artefacts that do not belong in a log line: hash lists, a frame copied
out, a recorded input script. Where each platform can put a file the host
reads without a card image or a package being unpacked:

| Platform | Location |
|---|---|
| PS2 / PCSX2 | `host:` — with the emulator's host-filesystem option on, it resolves to the directory of the booted image, so `dist/ps2pal/`. Open, create, write, mkdir and directory reads are all implemented; paths outside that directory are refused. On hardware over the development link the same `host:` works through the host tool. |
| PSP / PPSSPP | The memory-stick directory, which is a host folder; or `host0:` under the headless build's root mount |
| Vita / Vita3K | `ux0:data/<TITLE_ID>/`, a host folder as above |
| Win32 | Beside the executable, or the per-title data directory |

The engine writes it through the existing writable-path and file-write
contract where that already lands in one of these places (PSP, Vita, Win32).
The PS2 is the exception: its writable path is the memory card, so writing to
`host:` needs a platform decision about a second, development-only root.
Secondary to the log; the first version needs only the log.

## Emulator side — what each one offers

### PCSX2

Already used by `emu_capture`: `-batch`, `-nogui`, `-fastboot`, `-logfile`,
`-gameargs`, and the argv[0] placeholder. Additional seams:

| Seam | What it gives |
|---|---|
| **PINE** IPC server — Settings → Advanced → *Enable PINE Server*; TCP `127.0.0.1:28011` on Windows, a Unix socket elsewhere; slot overridable | Read and write emulated memory at 8, 16, 32 and 64 bits and bulk reads up to 4 KB; save and load state by slot; title, serial, CRC, game version, emulator status. With `dist/<platform>/main.sym` the runner reads the harness state directly — frame counter, last checkpoint — instead of waiting on the log. |
| `-statefile <file>` | Boot once, save a settled state, iterate from it. |
| `-datapath <dir>` | The runner hands the emulator its own data directory holding a prepared configuration: PINE on, host filesystem on, EE console logging on, never fullscreen. The developer's own configuration is never modified. |
| `-elf <file>` | Boot the ELF directly; `host:` then resolves to the ELF's directory. |
| Host filesystem option | See result file above. |
| Native screenshot (already in `emu_capture` as `--native-shot`) | The display output without window chrome, comparable between runs. |
| **GS dump runner** | Records the GS packet stream of a frame and re-renders it under any of the emulator's GS renderers, writing frames and diffing them. Proves a captured frame is stable independent of the EE side, and is the right tool for a giftag-versus-ps2gl regression once a known-good dump of each exists. |

Cautions:

- The PINE server can wedge when requests are pipelined; serialise them, and
  treat a stalled reply as "restart the emulator", since reconnecting does not
  clear it.
- `-batch` exits when the *virtual machine* shuts down. A PS2 program that
  returns does not shut the machine down, so the runner terminates the emulator
  on the `done` line or on a timeout, exactly as `emu_capture` does today.
- The fullscreen-capture and first-token pitfalls in
  [ps2/BUILD.md](../ps2/BUILD.md) still apply.

### PPSSPP

The strongest of the four. Two routes.

**The GUI build's WebSocket debugger.** Enabled under Settings → Tools →
Developer tools → *Allow remote debugger*; the port is shared with remote disc
streaming and settable there. The URL is `ws://<host>:<port>/debugger`,
subprotocol `debugger.ppsspp.org`. Events confirmed in the source:

| Area | Events |
|---|---|
| Input | `input.buttons.send` (a map of button name to held state), `input.buttons.press` (one button for a duration in frames), `input.analog.send` (x, y in [-1, 1], left or right stick). Button names: `cross circle triangle square up down left right start select ltrigger rtrigger home` and more. |
| Frame | `gpu.buffer.screenshot`, returning a PNG data URI or raw base64 with width, height and format; also `gpu.buffer.renderColor`, `renderDepth`, `renderStencil`, `texture`, `clut`. |
| Memory | `memory.read` (base64, ranged), `memory.read_u8/u16/u32`, `memory.readString`, the matching writes, `memory.search`. |
| Game | `game.status` (id, version, title, paused), `game.reset`, `game.speed.get/set` (fast forward, percent). |
| Log | A `log` event per line to every connected client: timestamp, header, level, channel, message. |

Command-line pieces the runner uses with it: `--windowed`, `--log=FILE`,
`--appendconfig <ini>` to enable the debugger and pin its port for this run
only, and on Windows `-s` so nothing is saved back to the developer's settings.
`-software` forces the software renderer for a deterministic frame.

**The headless build**, `PPSSPPHeadless`, is a purpose-built test runner:
`--timeout=<s>`, `--screenshot-save=<file>`, `--screenshot=<reference>` with
`--max-mse=<n>`, `--compare` against a `.expected` text file, `--graphics=`
`software|gles|vulkan|directx11`, `--root <dir>` mounted on `host0:`,
`--state=<file>`, `--debugger=<port>`. What it compares as text is the
module's writes to file descriptor 1, which is exactly what the PSP platform's
console does. It is not in the binary releases and must be built from source;
it is the one runner here that can go on a Linux CI host.

Cautions: the memory figure the emulator reports depends on which hardware
model it emulates — [psp/BUILD.md](../psp/BUILD.md) already says to set the
original model before trusting it, and the runner's appended configuration
should pin that too.

### Vita3K

The weakest seam, and [vita/BUILD.md](../vita/BUILD.md) already warns that its
graphics are an approximation: a frame comparison here proves "same as last
run", never "correct". What exists:

| Seam | Note |
|---|---|
| Positional `.vpk` path, or `-r <installed path>` | Install-and-run, or run an installed title without reinstalling (faster, and it skips the install step that can hang) |
| `--app-args` / `-Z` | Arguments reach the engine, comma-separated, so renderer selection and the harness flags work |
| `-c <config location>`, `--load-config`/`-f`, `--keep-config`/`-w` | A private configuration per run, left unmodified |
| `--log-level`/`-l`, `--archive-log`/`-A` | Log verbosity; a per-title copy of the emulator log |
| `--console`/`-z` | "Start the emulator in console mode" — meaning to confirm on the installed build |
| `gdbstub` configuration key, with a `gdbstub` module in the source tree | GDB remote protocol from a script: memory reads, breakpoints. *Unverified*: how it is switched on and which port it listens on. |
| `screenshot-format` configuration key | An in-app screenshot exists, but nothing on the command line or a socket drives it. Window capture through the existing PowerShell helper, generalised to take any process name, is the fallback. |

The log and the result file are both host folders (see above), so the
`ready`/`checkpoint`/`done` route works here without any socket.

### Win32 — the engine's own window

The window is created by the platform with no windowing library, and keyboard
and mouse state are filled from the message queue, not sampled. That makes the
queue the injection point:

- **Post key-down and key-up messages to the window handle** from outside. The
  window procedure records them exactly as it records real keys, so the engine
  cannot tell the difference, and no focus is needed. The keyboard-to-pad
  bridge then turns them into pad buttons, so every pad button and the left
  stick are drivable without a controller. Mouse buttons and wheel arrive the
  same way; cursor position is sampled, so a mouse script moves the real
  cursor or is answered by the wrapping platform instead.
- **Find the window by its class name**, which the platform registers as a
  fixed string, rather than by process name — several examples may be open.
- **Screenshot**: the existing helper's screen copy of the client rectangle,
  which is fine while the window is unobscured; the renderer readback in
  contract item 4 is the robust form.
- **Exit code** is real here: the harness verdict returns from `main`.
- **Without a GPU**: `--renderer null` runs. A drawn frame on a GPU-less
  runner needs either Mesa's software OpenGL or the WebGPU implementation's
  fallback adapter, neither of which has been tried with this engine.

## The host runner

One tool, tentatively `tools/harness.py`, in the same relationship to
`emu_capture` and `run_target` as `emu_capture` has to `run_target` today: it
resolves the emulator the same way (including the `*_PATH` overrides), and
`emu_capture` becomes one thing it can do.

Responsibilities:

1. **Prepare** a runner-owned emulator configuration (PCSX2 `-datapath`,
   PPSSPP `--appendconfig`, Vita3K `-c`) so the developer's settings are never
   read or written, and drop the options file and the input script where the
   platform will find them.
2. **Launch** with the harness flags, the log destination and a window that is
   never fullscreen.
3. **Attach** through the emulator's seam: PINE, the WebSocket, GDB, or the
   Win32 window handle. The attachment is an adapter class per emulator with
   one interface: press, release, stick, screenshot, read memory, read log.
4. **Drive**: wait for `ready`, replay the script where the engine is not
   doing it itself, request frames at checkpoints.
5. **Collect**: the log, frames, the result file; parse the harness lines into
   events.
6. **Judge**: the assertions the test declared (see below), written to a
   report file, and an exit code.
7. **Stop**: on `done`, on a failed assertion, or on a timeout; terminate the
   emulator; never leave one running.

Tests for the runner itself live in `tools/tests/` with a mocked subprocess
and canned log text, the way `test_emu_capture.py` and `test_run_target.py`
already work. Every emulator-specific string (a flag, an event name, an ini
key) lives in one adapter file so a change upstream is one edit.

## The first tests

Exactly the checks the build documents ask a person to make, and nothing
cleverer:

| Check | Assertion |
|---|---|
| Boots and runs | `ready` appears naming the requested platform and renderer; `done` appears within the frame count |
| Renderer really started | The renderer in `ready` is the one asked for, not a fallback — a silent fallback is the failure every platform document warns about |
| Nothing broke | No error-level lines between `ready` and `done` |
| Memory is stable | Heap in the last checkpoint equals heap in the first, after the scene has settled |
| Interface fits | Zero overflow reports on the PS2, the only platform with a fixed screen-space ceiling |
| Pacing | The frame split stays under the platform budget at every checkpoint, on the desktop; on emulators it proves only that the loop paces |
| Backends agree | The same example, same script, same checkpoint frame, on both of a platform's backends: image diff within a tolerance. Automates the rule that any difference is a defect in one of them |
| Regressions | The frame hash at a checkpoint equals the recorded one |

Every example gets the first six by default. Backend equivalence and
regression hashes are opted into per example, with the recorded reference
frames kept under `tools/tests/` beside the test that uses them.

## CI split

- `tools-ci.yml` gains the runner's own tests: parser, adapters against canned
  responses, script compilation. No emulator.
- Emulator runs need the Windows installs under `C:\` and a desktop session for
  window capture, so they sit on a self-hosted Windows runner, gated to the
  same events as the build.
- A GitHub-hosted job can cover Win32 with the null renderer today, and the
  PSP through `PPSSPPHeadless` once a build of it is pinned and cached the way
  the WebGPU release is.

## Sequence

1. **Spec.** `docs/subsystems/HARNESS.md` (name open) from the
   [NEW_SYSTEM.md](../guidelines/NEW_SYSTEM.md) template: the three flags, the
   line grammar, the script and its refusal rules, the frame-evidence contract
   and its honest absences, dependencies on Debug, Input and Renderer, and what
   happens when it is absent (flags ignored, no lines, no wrapper). Decide there
   whether it ships in release: the argument for shipping is that release
   verification "is running the game", and a harness that exists only in debug
   builds cannot make that check. Review before code.
2. **Engine, cheapest slice first.** Flags and the `ready`/`checkpoint`/`done`
   lines. No wrapper, no readback. Enough for the first six checks on every
   platform through the log alone.
3. **Runner, log route only.** Launch, tail, parse, judge, terminate, for all
   four emulators plus Win32. Fold `emu_capture` in. Tests.
4. **Scripted input.** The wrapping platform, the script declaration and its
   compiler, record mode on Win32. First scripted test: open a menu and pick an
   item on every platform from one script.
5. **Frame evidence.** Renderer readback on the four backends where it is
   cheap; emulator screenshot on the PS2; backend equivalence and regression
   hashes.
6. **Emulator-specific depth**, only where the log route proved insufficient:
   PINE for state reads and save-state iteration, the PPSSPP WebSocket for
   input where the engine-side script cannot be delivered, GS dumps for the
   PS2 backend comparison.
7. **Instructions and specs in the same changes**: `copilot-instructions.md`
   gains the runner beside `emu_capture`; each platform's build document
   replaces the manual step with the runner invocation; the Testbed and Action
   specs get the one-line cross-references they need.

## Open questions

- **PSP launch arguments** (*unverified*). Whether PPSSPP hands anything
  beyond argv[0] to the module. If not, the options file is the only route on
  that platform, and the harness spec should say so.
- **Vita3K GDB stub** (*unverified*). The module exists and a `gdbstub`
  configuration key exists; how it is enabled and which port it listens on
  were not found in documentation. Check the installed build before designing
  the adapter.
- **PS2 `host:` as a development root.** Whether the platform should look on
  `host:` for the options file and the result file when it booted from
  `cdrom0:`. It changes nothing on a retail disc, but it is a second root and
  the platform spec would have to say so.
- **Release or debug.** Whether the harness is compiled into release builds
  (see Sequence, step 1).
- **Determinism ceiling.** A fixed delta time makes the game deterministic;
  the renderers are not, across drivers or emulator versions. Equivalence and
  regression checks need a tolerance and a pinned emulator version each, and
  the first failures will be tolerance tuning, not defects.

## Sources checked, 2026-09-17

- PCSX2: [command-line options](https://pcsx2.net/docs/advanced/cli/);
  [PINE client crate](https://docs.rs/pine-client/latest/pine_client/) and
  [mcp-pine](https://github.com/dmang-dev/mcp-pine/blob/main/README.md) for
  the connection, operations and the wedge caveat;
  [IopBios.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/IopBios.cpp)
  for the host filesystem root and operations;
  [GS Dump Runner](https://pcsx2.net/docs/advanced/gsdumprunner/).
- PPSSPP: [headless README](https://github.com/hrydgard/ppsspp/blob/master/headless/README.md);
  [command-line arguments](https://www.ppsspp.org/docs/reference/command-line/);
  the WebSocket subscribers for
  [input](https://github.com/hrydgard/ppsspp/blob/master/Core/Debugger/WebSocket/InputSubscriber.cpp),
  [GPU buffers](https://github.com/hrydgard/ppsspp/blob/master/Core/Debugger/WebSocket/GPUBufferSubscriber.cpp),
  [game](https://github.com/hrydgard/ppsspp/blob/master/Core/Debugger/WebSocket/GameSubscriber.cpp),
  [memory](https://github.com/hrydgard/ppsspp/blob/master/Core/Debugger/WebSocket/MemorySubscriber.cpp) and
  [log](https://github.com/hrydgard/ppsspp/blob/master/Core/Debugger/WebSocket/LogBroadcaster.cpp);
  [mcp-ppsspp](https://github.com/dmang-dev/mcp-ppsspp/blob/main/README.md)
  for how the debugger is enabled.
- Vita3K: [config.cpp](https://github.com/Vita3K/Vita3K/blob/master/vita3k/config/src/config.cpp)
  for the command line,
  [config.h](https://github.com/Vita3K/Vita3K/blob/master/vita3k/config/include/config/config.h)
  for the configuration keys, the
  [gdbstub module](https://github.com/Vita3K/Vita3K/tree/master/vita3k/gdbstub),
  and the [FAQ](https://vita3k.org/faq) for the data directory.
