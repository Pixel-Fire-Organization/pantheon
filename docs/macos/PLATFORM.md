# Platform — macOS (`macos`)

A native desktop target for the Mac: a real window, a keyboard, a mouse and game
controllers, one binary that carries both Intel and Apple Silicon code, and a heap
that is not policed against a hardware ceiling.

Selectable as `macos`. Build instructions are in [BUILD.md](BUILD.md); renderers are in
[renderers/](renderers/) and, for the part shared with Windows,
[../renderers/WEBGPU.md](../renderers/WEBGPU.md).

## One platform, one binary, two architectures

The Mac has run on two processor architectures since 2020, so a build carries **both** in
a single executable, linked against a single library that carries both as well: Intel and
Apple Silicon are not variants. Nothing this platform answers differs between them. What
does differ is below the platform — the processor's speed, and which graphics processor
drives the display (see [Graphics](#graphics)).

The build targets **macOS 11**, the first release that runs on Apple Silicon, so one binary
covers every Mac from then on, on either architecture. Apple's current release, macOS 27,
runs on Apple Silicon only; Intel Macs stop at macOS 26.

## What is different in kind from the other desktop platform

- **The default renderer cannot reach OpenGL, and the fallback is not the default's
  fallback in the usual sense.** The graphics library the default renderer uses ships a
  Metal backend and nothing else on this platform. The fallback is therefore a separate
  implementation of the real OpenGL that Apple still ships, not the default renderer
  pointed at a different API. See [renderers/WEBGPU.md](renderers/WEBGPU.md) and
  [renderers/OPENGL.md](renderers/OPENGL.md).
- **The window is drawn at one pixel per point.** A Retina display has two or three
  pixels for each point, and the interface's metrics are pixels fixed by the theme, so
  drawing at native density would shrink the interface to half its intended size. See
  [Graphics](#graphics).
- **The application is a bundle.** The executable, its library, the archive and the
  shaders live inside one folder the system treats as a single application, and the
  platform resolves its data from inside it. See [Storage and IO](#storage-and-io).
- **The system owns the event loop.** The platform never hands control to it. The
  engine's own loop pumps the event queue once per frame, as part of polling input.

## Capabilities

| Capability | Available | Note |
|---|---|---|
| Gamepad | Yes | Up to four, through the system controller framework |
| Keyboard | Yes | |
| Mouse | Yes | |
| Analog triggers | Yes | Real pressure |
| Resizable window | Yes | |
| Async IO | Yes | |
| File write | Yes | |
| Touch | No | |
| System dialog | Yes | A modal alert; refuses text entry, see below |
| Text characters | Yes | Printable ASCII only; see [Input](#input) |

## Memory

Values are budgets, not hardware limits, and are the Win32 values: the contract that an
over-budget load fails loudly rather than being paged out only means something if a
budget exists.

| | Size | Slots | Per slot |
|---|---|---|---|
| Engine budget | 512 MB | | A policy ceiling, not a hardware one |
| Config arena | 1 MB | 4 | 256 KB |
| Level-data arena | 32 MB | 16 | 2 MB |
| Level LOD1 arena | 16 MB | 32 | 512 KB |
| Renderer arena | 16 MB | 1 | 16 MB |
| Main pool | 4 MB | | 256 B chunks |
| Texture budget | 256 MB | | Charged at 32-bit colour, the whole mip chain |

Aligned allocations come from the C allocator's aligned entry point and are released
with the ordinary free; there is one heap here, so crossing allocators cannot corrupt a
second one. The allocator-pairing rule still holds, because the platforms that have two
are what it exists for.

Heap statistics report the reservation made at start-up, as the Windows platform does, so the
figure means the same thing on both desktop platforms.

## Storage and IO

| | |
|---|---|
| Max async read | 4 MB |
| Queued requests | 64 |
| Mounted archives | 4 |
| Resource handles | 256 |

**Assets are read from inside the bundle.** When the executable sits in the bundle's
`Contents/MacOS` directory, the data root is the sibling `Contents/Resources`; run from
anywhere else — a development build, a bare executable in a folder — the data root is
the executable's own directory. In both cases the archive and the `shaders` directory sit
at the root, and the root is the executable's real path with symbolic links resolved, so
launching from any working directory, or through a link, behaves the same.

**Writes do not go there.** The per-title save location is the user's own Application
Support directory, under the developer's name and then the title's, created on first use.
A bundle is signed and may sit somewhere the user cannot write, and a signed bundle that
changes under its own signature is reported as damaged.

The IO worker sleeps a millisecond between scans when idle, and has a stack of 512 KB, the
system's own default for a secondary thread, rather than the 64 KB Windows uses. The engine's
log formatting and the C library's own take kilobytes of stack, and nothing is gained by
asking a desktop for less.

## Input

Keyboard and mouse are read from the event queue, **not by sampling device state**.
Sampling misses a key pressed and released inside one frame, which is what a quick tap
is; the queue does not. Held keys and buttons are cleared when the window loses focus,
because the matching release goes to whoever took focus. Modifier keys arrive on a
separate event with no press or release, so each is derived from its own flag bit.

The mouse position is in framebuffer pixels from the top-left, which at one pixel per
point is the window's point coordinate system flipped to a top-left origin. The wheel
is reported in notches: a trackpad's precise deltas are divided by ten per notch.

Controllers are read by polling the system's extended-gamepad profile for each of up to
four connected controllers, in the order the system lists them. Face buttons map by
position, as on every other platform here: bottom to Cross, right to Circle, left to
Square, top to Triangle. The triggers are analog and are also reported as the digital
second shoulders past half travel. The Select button is the controller's *Options* button
where it has one. Wireless discovery is started once. **Controller input is built, and
has not been exercised on a controller here.**

**A keyboard-to-virtual-pad map is installed by default**, exactly as on Windows and for
the same reason, with the same keys; **disabled by `--no-keyboard-pad`**. It covers
every pad button, since a partial bridge makes anything bound to the missing buttons
unreachable without a controller.

| Intent | Combination | From the keyboard |
| :--- | :--- | :--- |
| Performance snapshot | L1 + L2 + R1 + R2 | 1 + 3 + 2 + 4 |
| Overlay toggle | L1 + L2 + L3 + R3 | 1 + 3 + 5 + 6 |
| Debug menu | Select + Start | Tab + Escape |

Button prompts draw Xbox letters: the system's controller names the face buttons A, B,
X and Y.

**The character channel is the typed characters of the key events**, filtered to
printable ASCII plus backspace, enter and escape, and discarded while Command or Control
is held so a shortcut never types. It does not use the system's text-input machinery, so
there is **no input method** and no accented or non-Latin text; the filter is what the
contract promises, and everything outside it is dropped here.

**Dialogs.** A message or confirmation runs the system's alert modally on the calling
thread and reports its outcome on the first poll, the shape [Win32](../win32/PLATFORM.md)
uses for its message box. Text entry is refused: the character channel serves it inline.

## Window

The window is created directly against the system's windowing framework, with no
windowing library. Everything else here is already native — threads, controllers,
aligned allocation — so a library would add a dependency and a build step to replace
a few hundred lines. The same reasoning is recorded for Win32.

- The window is resizable, with a minimum size, and opens at 1280 x 720 points.
- **The framebuffer is the window's content area in points.** The renderers read its
  size every frame, and a minimised or collapsed window keeps the last real size rather
  than reporting zero.
- Each renderer adds its own **child view** to the window's content view and removes it
  when it shuts down, so a renderer that fails to start leaves nothing behind for the next
  one in the fallback chain.
- A menu bar with a single Quit item exists so Command-Q and the application menu quit
  the application the way a Mac user expects; closing the window and quitting both ask the
  engine to stop and let it finish the frame.
- **The system's own quit request — from the Dock, a logout or a restart — is handled directly.**
  The platform takes over that event, tells the engine to stop, and reports success at once. The
  obvious alternatives both fail: refusing the request tells the system the application will not
  quit, which can abort a logout, and asking the system to wait deadlocks, because the wait runs
  inside the very call that delivered the event while the engine's own loop is what would end it.
  Launching from Finder, with the working directory at the root, is exercised too: the data root
  comes from the bundle, not the directory.

**The event pump runs as part of polling input**, once per frame. The queue is where
resize, close, wheel and focus events arrive, so draining it is part of reading input
rather than a step a caller could forget.

## Graphics

| | |
|---|---|
| Default window | 1280 x 720 points, resizable |
| Frame budget | 16667 us |
| Display aspect | Framebuffer aspect; pixels are square |
| Draw list capacity | 4096 |

**One drawable pixel per point.** The interface's metrics are a compile-time percentage
of pixel values, so they do not follow the framebuffer's density: drawn at two pixels per
point, a Retina display would show everything at half the intended size and no theme could
correct it. At one pixel per point the interface is exactly what it is on Windows, and the
system's compositor scales the frame to the display. The cost is a soft image on a
high-density display; for content authored against a console framebuffer that is
acceptable, and rendering at native density is left until the interface's scale can be
answered at run time rather than compiled in.

## Renderers

| Backend | Role |
|---|---|
| [webgpu](renderers/WEBGPU.md) | **Default.** Metal, through the library shared with Windows |
| [opengl](renderers/OPENGL.md) | Fallback. Apple's OpenGL, a 4.1 core-profile context |
| null | Final fallback; headless. See [RENDERER.md](../subsystems/RENDERER.md) |

Fallback order is webgpu, then opengl, then null.

Both real renderers read their shaders from the bundle's `shaders` directory, cooked from
`assets/engine/shaders` and shared with Windows — see
[formats/SHADER_ASSETS.md](../formats/SHADER_ASSETS.md). **A bundle whose shaders are
missing has no renderer that can draw**: each reports the file and the cook target, and the
chain ends at the null renderer.

## Components considered

Recorded as the platform guideline asks, because the next platform will face the same
questions.

| Question | Decision | Why |
|---|---|---|
| A windowing library (GLFW, SDL) | **No.** Cocoa directly, in Objective-C++ confined to this platform's own sources | Same reasoning as Win32's own window: events, a window and a menu are a few hundred lines against an API the rest of the platform already speaks, and a library would add a submodule and a build step. Objective-C stays out of every header |
| Controllers | **The system controller framework**, polled | The HID layer reports raw usages that every vendor lays out differently; the framework maps them to one profile. It is polled rather than driven by handlers, so it fits the engine's once-per-frame snapshot |
| The graphics library | **The pinned prebuilt**, one archive per architecture, merged | Building it needs a Rust toolchain; the prebuilt carries Metal, which is what the default needs. Merged into one library so the executable is universal |
| OpenGL through that library | **Not possible**, and not attempted | The prebuilt contains no OpenGL, and that library has no way to present through an OpenGL surface here. See [renderers/WEBGPU.md](renderers/WEBGPU.md) |
| OpenGL through a translation layer | **No** | It would cap the fallback at OpenGL ES 3.0, below what Apple's own OpenGL offers, and ship two more libraries to do it |
| Shaders as archive entries | **No**, loose files beside the archive | The renderer is built before the archive subsystem exists, and is not selectable the way that subsystem is. See [formats/SHADER_ASSETS.md](../formats/SHADER_ASSETS.md) |
| A display link for pacing | **No**, a time-based cap on the OpenGL swap | The display link API is deprecated from macOS 15, its replacement needs macOS 14 and the build targets 11, and a cap that does nothing when the driver already paces needs no thread |

## Performance

The nine items [guidelines/PERFORMANCE.md](../guidelines/PERFORMANCE.md) asks every
platform to state.

**Budget and pacing.** The frame budget is 16667 us, a policy for content authored against
the console rather than a property of the Mac. The default renderer's frame is paced by the
display through first-in-first-out presentation, so it runs at the display's refresh rate:
on a 120 Hz display the frame is about 8.3 ms and the snapshot still compares it against
16.67. The fallback paces itself: the context is asked for a swap interval of one, and the
swap then **sleeps out whatever is left of one display refresh** if the driver returned
early. The second half exists because the first was measured not to hold — see the
baseline.

**The binding constraint.** None that is hardware-shaped. The engine's own per-frame work
dominates — staging every vertex of every visible object on the processor, then one upload —
and it is small next to any Mac's capacity. This platform establishes correctness and
compares backends; it does not judge cost. Anything that measures as slow here is
unshippable on every console.

**Per-frame ceilings.**

| | |
|---|---|
| Draw list entries | 4096 |
| Interface quads | 16384, plus 2048 overlay quads |
| Draw runs | 1024 world, 256 screen-space |
| Staged vertices | grows on demand from the heap — a gap against the fixed-budget rule, see [backlog/performance_findings.md](../backlog/performance_findings.md) |
| Resident sectors | 14 |

**Clock.** The monotonic tick counter, converted by the system's own timebase to seconds from
the first call: sub-microsecond, and it does not wrap within a session. The C library clock is
not used.

**Scheduling.** Preemptive and time-sliced; priorities do not matter here and none are set. The
IO worker sleeps a millisecond between scans when idle.

**Cost of diagnostics.** Each line goes to standard output, flushed per line so a redirected
log survives a crash. Cheap: a per-frame diagnostic is a nuisance here and a whole frame
elsewhere.

**What the snapshot measures here.** Neither real backend reports present wait, geometry build
or upload; all three read as not measured, as on Windows. Draw counts, cull counts and texture
binds are reported by both.

**Baseline.** 2026-10-03, **hardware** — a 2018 MacBook Pro (`MacBookPro15,1`), six-core Intel
Core i7, 32 GB, an Intel UHD Graphics 630 and an AMD Radeon Pro 555X, a Retina display that
the default renderer locked to about 60 frames per second, macOS 15.8.1. The **Intel slice only**: no Apple Silicon machine was available, so the
arm64 slice was built and signed but never run. A development (debug) build, the game's boot
scene — the level `MAINASSETSTEST` with its materials, a streamed sector ring and the
interface — about twenty seconds per renderer, read from the heartbeat:

| Renderer | Frames per second | Heap |
|---|---|---|
| `webgpu` | 60.5 median, 58.5 to 62.5, locked to the display | 70656 KB, constant |
| `opengl` | 59.6 median, 57.1 to 61.5, paced by the swap's own cap | 70656 KB, constant |
| `null` | about 51000 — the engine tick alone, about 20 us | 70656 KB, constant |

The process's resident memory, sampled every fifteen seconds for three minutes, was flat on `opengl`
(113.4 MB from the first minute) and rose slowly on `webgpu`, from 73.1 MB to 73.9 MB, while the engine's
own heap stayed at 70656 KB throughout. The growth is on the graphics library's side, since both
renderers share the platform's event pump; whether it is a leak is open — see PF-18 in
[backlog/performance_findings.md](../backlog/performance_findings.md).

The fallback was first measured **without** the cap: with the swap interval requested and read
back as applied, it ran unpaced at a median of 223 frames per second, and 381 in a second run.
This machine has two graphics processors and the OpenGL context landed on the one that does not
drive the built-in display, so the driver's vertical synchronisation did not reach the screen's.
The cap removes the dependence on the driver doing so; on hardware where it does, the cap has
nothing to sleep.

This is one development laptop's debug build, so it proves that the loop paces, that nothing on
the frame path grows and that the accounting is sane. It is not a figure for content, and not
one for Apple Silicon.

**Left on the table**: rendering at native pixel density, which needs the interface's scale to
become a run-time answer; present-wait measurement on both backends; a budget that follows the
display's refresh, since here the budget is policy rather than hardware; a baseline on Apple
Silicon; the sky, on both backends.

## Known limitations

- **Apple's OpenGL is deprecated and frozen at 4.1.** Apple's macOS 26 release notes say it remains in the
  SDK. Whether it ships in the release after that is not confirmed, and the fallback renderer exists only as
  long as it does.
- **On Apple Silicon, OpenGL is itself a layer over Metal**, so there the fallback is not an
  independent path. It is on Intel.
- **The image is soft on a high-density display**; see [Graphics](#graphics).
- **No input method and no non-ASCII text**; see [Input](#input).
- **Controller input and the Apple Silicon slice are built but unexercised on hardware.**
- **Neither desktop backend draws the sky**, as on Windows.
- **Far-field geometry is drawn by no backend on any platform.**
- Sound and font assets are unimplemented, as on every platform.
- Sector recentring is synchronous, though on a solid-state disk the hitch is small.
- **The bundle is signed ad hoc and not notarised.** It runs on the machine that built it; one that
  arrives over a network is quarantined by the system until it is notarised, which this build does
  not do.
- **No application icon.**
