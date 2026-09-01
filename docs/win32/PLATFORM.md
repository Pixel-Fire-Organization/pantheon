# Platform — Win32

A native desktop target: a real window, an arbitrary resolution, keyboard and
mouse, and a heap that is not policed against a hardware ceiling.

Build instructions are in [BUILD.md](BUILD.md); renderers are in
[renderers/](renderers/).

## What is different in kind from the console

Most values here are **budgets, not hardware limits**. The console numbers
describe a machine; these describe a policy. The policy is kept because the
engine contract is that an over-budget load fails loudly rather than being
silently paged out, and that only means something if a budget exists at all.

Two consequences worth stating plainly:

- **The framebuffer size is not fixed.** The window is resizable and renderers
  must ask for the framebuffer size each frame rather than caching it. Anything
  that bakes a resolution in will be wrong the first time the window is dragged.
- **Pixels are square**, so the projection aspect is simply the framebuffer
  aspect — unlike the console, where the display aspect and the framebuffer ratio
  differ.

## Capabilities

| Capability | Available | Note |
|---|---|---|
| Gamepad | Yes | Four ports |
| Keyboard | Yes | |
| Mouse | Yes | |
| Analog triggers | Yes | Real pressure, unlike the console pad |
| Resizable window | Yes | |
| Async IO | Yes | |
| File write | Yes | |
| System dialog | Yes | `MessageBox`; refuses text entry, see below |
| Text characters | Yes | `WM_CHAR`, the only text-entry mechanism here |

## Memory

| | Size | Slots | Per slot |
|---|---|---|---|
| Engine budget | 512 MB | | A policy ceiling, not a hardware one |
| Config arena | 1 MB | 4 | 256 KB |
| Level-data arena | 32 MB | 16 | 2 MB |
| Renderer arena | 16 MB | 1 | 16 MB |
| Main pool | 4 MB | | 256 B chunks |

Slot alignment stays at 16 KB even though the desktop has no transfer-alignment
requirement of its own. Matching the console means slot arithmetic behaves
identically on both platforms, so a slot-boundary bug cannot appear on one
platform and hide on the other.

Aligned allocations come from a **separate heap** here, and must be released
through the matching call. This is the concrete reason allocation sits behind the
platform contract at all — see [MEMORY.md](../subsystems/MEMORY.md).

## Storage and IO

| | |
|---|---|
| Max async read | 4 MB |
| Queued requests | 32 |
| Mounted archives | 4 |
| Resource handles | 256 |

The read buffer is far larger than the console's because there is no address
coverage window to stay inside.

Paths resolve relative to the executable, so a distribution directory is
self-contained and can be copied anywhere and run.

**Writes do not.** The per-title save location is the user's own local
application data, under the developer's name and then the title's. A
distribution directory is expected to sit somewhere a normal user account cannot
write to, and a directory that can be copied anywhere is one whose copies would
otherwise each carry their own divergent save.

## Input

Gamepads through XInput, up to four. Sticks are converted to the same convention
the console uses, so deadzone handling and the analog range mean the same thing
on both platforms.

**Keyboard and mouse are read from the window message queue**, not by sampling
device state. Sampling misses a key that is pressed and released inside one
frame, which is exactly what happens with a quick tap; the queue does not. Held
keys are cleared when the window loses focus, so alt-tabbing away does not leave
a key stuck down forever.

**A keyboard-to-virtual-pad map is installed by default.** It exists so content
written for a pad runs on the desktop unmodified — the movement keys drive the
left stick, the arrows the directional pad, and so on. It is a separate,
disableable layer, not part of the keyboard queries: with it disabled the
keyboard still reports as a keyboard and only the virtual pad disappears. See
[INPUT.md](../subsystems/INPUT.md).

**The bridge covers every pad button, including the ones a keyboard has no
obvious analogue for.** A partial bridge is worse than none: it looks complete
while quietly making anything bound to the missing buttons unreachable without a
controller plugged in, which is how this platform's debug combination was
unpressable from the keyboard. See [DEBUG.md](../subsystems/DEBUG.md).

| Intent | Combination | From the keyboard |
| :--- | :--- | :--- |
| Performance snapshot | L1 + L2 + R1 + R2 | 1 + 3 + 2 + 4 |
| Overlay toggle | L1 + L2 + L3 + R3 | 1 + 3 + 5 + 6 |
| Debug menu | Select + Start | Tab + Escape |

Button prompts draw Xbox letters, matching the XInput convention the gamepad
bridge already follows. See [subsystems/UI.md](../subsystems/UI.md).

The keyboard column is the bridge, not a second binding: it disappears with the
bridge, so with the bridge disabled every combination above is reachable only
from a controller.

**A second, separate character stream rides the same `WM_CHAR` messages the
window already receives**, behind `PlatformCapability::TextCharacters`: this is
the printable-ASCII-plus-backspace/enter/escape channel `Platform.h` documents
for `Keyboard_PopCharacters`, not the keyboard device queries above and not the
pad bridge. `MessageBox` (`Dialog_Open`) blocks the calling thread and refuses
`DialogKind::TextInput` outright — this platform has no host text dialog, so
the UI subsystem's text-entry widgets use the character channel directly,
inline, with no dialog box at all, matching what a desktop with a keyboard
already expects. Physical Backspace reaches both streams at once, meaning both
"the Back gamepad signal fired" and "a 0x08 byte is waiting in the character
channel" become true on the same keystroke; a widget reading the character
channel must not also read the ordinary Back signal while it does, or the same
key would both edit and cancel. See the *Dialogs and text entry* section of
[subsystems/UI.md](../subsystems/UI.md).

## Window

The window is created directly against the operating system, with no windowing
library. Everything else on this platform is already native — threads,
controllers, aligned allocation — so a library would add a dependency and a
cross-compile step to replace about a hundred lines. A future desktop platform
gets its own directory and may choose differently without affecting this one.

**The message pump runs as part of polling input**, once per frame. The queue is
where resize, close and wheel events arrive, so draining it is part of reading
input rather than a separate step a caller could forget to perform.

## Graphics

| | |
|---|---|
| Default window | 1280 x 720, resizable |
| Frame budget | 16667 us |
| Display aspect | Framebuffer aspect; pixels are square |
| Draw list capacity | 4096 |

## Renderers

| Backend | Role |
|---|---|
| [webgpu](renderers/WEBGPU.md) | **Default.** Modern explicit API |
| [opengl](renderers/OPENGL.md) | Version-configurable, down to a legacy path |
| null | Final fallback; headless. See [RENDERER.md](../subsystems/RENDERER.md) |

Fallback order is webgpu, then opengl, then null.

The desktop OpenGL backend is **unrelated to the console backend of a similar
name**. One is the real graphics API at a modern version; the other is a
library implementing a small subset of an old version on top of vector microcode.
They share no code.

## Performance

The nine items [guidelines/PERFORMANCE.md](../guidelines/PERFORMANCE.md) asks
every platform to state.

**Budget and pacing.** The frame budget is 16667 us — a policy for content
authored against the console, not a property of this platform. The frame is
paced by the display through vertical synchronisation on both backends (the
default's first-in-first-out presentation; the fallback's swap interval, which
is best-effort), so it runs at the display's refresh rate whatever that is: on
a 144 Hz display the frame is about 6.9 ms, and the snapshot still compares it
against 16.67. The processor overlaps the graphics driver by however many
frames the driver queues; the engine does not control that.

**The binding constraint.** None that is hardware-shaped. The engine's own
per-frame work dominates — staging every vertex of every visible object on the
processor, then one upload — and it is small next to any desktop's capacity.
This platform establishes correctness and compares backends; it does not judge
cost. Anything that measures as slow here is unshippable on every console.

**Per-frame ceilings.**

| | |
|---|---|
| Draw list entries | 4096 |
| Interface quads | 16384, plus 2048 overlay quads |
| Draw runs | 1024 world, 256 screen-space |
| Staged vertices | grows on demand from the heap — a gap against the fixed-budget rule, see [backlog/performance_findings.md](../backlog/performance_findings.md) |
| Resident sectors | 9 |

**Clock.** The high-resolution performance counter: monotonic, sub-microsecond,
and it does not wrap within a session. The C library clock is not used; its
millisecond resolution is a sixteenth of a frame.

**Scheduling.** Preemptive and time-sliced; priorities do not matter here and
none are set. The IO worker sleeps a millisecond between scans when idle,
which on this platform rounds up to the scheduler's own granularity.

**Cost of diagnostics.** Each line goes to standard output, flushed per line so
a redirected log survives a crash, and to the debugger when one is attached.
Cheap — a per-frame diagnostic is a nuisance here and a whole frame elsewhere.

**What the snapshot measures here.** Neither backend reports present wait,
geometry build or upload; all three read as not measured, and the engine's own
wait figure is not measured on any platform (see the findings record). Draw
counts, cull counts and texture binds are reported by both.

**Baseline.** 2026-09-16, **hardware** — a development desktop with a discrete
graphics card and a 144 Hz display — the game's boot scene (the main menu),
nine seconds per renderer, read from the heartbeat:

| Renderer | Frames per second | Heap |
|---|---|---|
| `webgpu` | 141 to 162, instantaneous | 54272 KB, constant |
| `opengl` | 144, locked to the display | 54272 KB, constant |
| `null` | 10000 to 20000 — the engine tick alone, 50 to 100 us | 54272 KB, constant |

**Left on the table**: the sky, on both backends; present-wait measurement on
both backends, which is one call each; a budget that follows the display mode,
since here the budget is policy rather than hardware.

## Known limitations

- **Neither desktop backend draws the sky.** World geometry, models, primitives
  and interface elements all render; the sky does not. The console backends do
  draw it, so this is a desktop gap.
- **Far-field geometry is drawn by no backend on any platform** — the level
  format describes it, nothing renders it.
- Sound and font assets are unimplemented, as on every platform.
- Sector recentring is synchronous, though on desktop storage the hitch is far
  smaller than on optical media.
