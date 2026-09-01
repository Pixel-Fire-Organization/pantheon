# Platform — Nintendo Switch (`nx`)

A hybrid console: a handheld with a 1280 x 720 touchscreen that becomes a
set-top box when docked, driving a television at 1920 x 1080. It has the most
capable graphics processor of any console here, one memory pool shared between
that processor and the main one, and a homebrew runtime whose memory allowance
depends on how the title was launched.

Selectable as `nx`. Build instructions are in [BUILD.md](BUILD.md); the
distribution container is in [PACKAGING.md](PACKAGING.md); renderers are in
[renderers/](renderers/).

## One platform, not a family

The original console, the later revision and the handheld-only model all run
one binary. What differs between them — whether the console is docked, and
therefore how large the framebuffer is and whether the touchscreen is visible —
is a **run-time** fact that changes while the title is running, not a property
of the hardware a build targets. A family of variants would have to pick one
answer at compile time for a value that has two during a single session, which
is exactly the case variants cannot express.

So there is no base and no variant. Every value that depends on the operation
mode is answered live, the way the desktop platform answers its window size.

## What is different in kind from the other platforms

- **The framebuffer changes size mid-session.** Docking and undocking change
  the output from 1280 x 720 to 1920 x 1080 and back, and the title keeps
  running through it. This is the desktop platform's resizable window arriving
  on a console: renderers read the size every frame, and the interface is laid
  out against the size it is given rather than a fixed one. Unlike the desktop,
  the display aspect never changes — both sizes are 16:9 with square pixels.
- **The processor and the graphics processor share one pool of memory.** There
  is no video pool. Textures, render targets, vertex buffers, the engine map, the
  heap and the graphics driver's own allocations all come from the same place,
  so a texture budget here is a policy carve-out of main memory rather than a
  hardware ceiling of a separate region.
- **How much memory a title has depends on how it was launched.** A homebrew
  title started through the album applet runs as a library applet and receives a
  few hundred megabytes; one started by taking over an installed application
  receives several gigabytes. The engine budget is set for the smaller of the
  two, because that is the launch a player most often uses, and the platform says
  once at start-up when it is running in the constrained mode.
- **Assets live inside the executable.** The distribution is a single file
  whose read-only file system carries the resource archive. There is no install
  directory to read loose files from, and nothing to copy beside the executable.
- **Leaving the title suspends it rather than stopping it.** The HOME button
  takes the title out of focus and the system stops scheduling it until it
  returns. The platform accounts for that time so a suspension does not arrive as
  one enormous frame delta. There is also a real exit request, which the close
  query reports, as on the PlayStation Portable.

## Capabilities

| Capability | Available | Note |
|---|---|---|
| Gamepad | Yes | Up to four ports; handheld controls and the first wireless controller share port 0 |
| Keyboard | No | The dock accepts a USB keyboard; it is not exposed. See [Known limitations](#known-limitations) |
| Mouse | No | |
| Analog triggers | No | ZL and ZR are digital switches; the trigger queries report fully released or fully pressed |
| Resizable window | Yes | The framebuffer size is not fixed — the dock changes it, not a resize handle. See [Window](#window) |
| Async IO | Yes | |
| File write | Yes, while the SD card is mounted | To the title's directory on the SD card only; never to the executable's own file system |
| Touch | Handheld mode only | Front surface; answered live from the operation mode |
| System dialog | Yes, for text entry | The software keyboard applet. Message and confirmation boxes use the interface's own modal — see [Dialogs](#dialogs) |
| Text characters | No | Typed text arrives only through the software keyboard, not key by key |

**Touch is answered from the operation mode, not from the hardware model.**
Docked, the touchscreen is in the dock and cannot be seen; a capability that
answered yes there would invite content to ask the player to point at a screen
they are not looking at. The answer changes the frame the console is docked or
undocked, which is honest in the same way a disconnected controller reporting
absent is honest.

Button prompts draw **Nintendo** letters. They are not the Xbox letters: both
families print A, B, X and Y, but on different buttons. See
[Input](#input).

## Memory

**One pool, shared by everything.** The system gives the process a single
allowance, and the runtime hands all of it to the C heap at start-up. The engine
map, aligned allocations, textures, render targets, the graphics driver's command
memory and the fallback renderer's driver all come out of it.

| Launch | Allowance, approximately |
|---|---|
| Through the album applet (library applet) | 440 MB |
| By taking over an installed application | 3 GB and more |

The engine map, inside that allowance:

| | Size | Slots | Per slot |
|---|---|---|---|
| Engine budget | 128 MB | | A policy ceiling that fits the applet allowance with the graphics driver present |
| Config arena | 1 MB | 4 | 256 KB |
| Level-data arena | 32 MB | 16 | 2 MB |
| Renderer arena | 16 MB | 1 | 16 MB |
| Main pool | 4 MB | | 256 B chunks |
| Texture budget | 128 MB | | Charged separately; a carve-out of the same pool |

**There is one allocator, so there is nothing to cross.** Because the heap
already owns the whole allowance, the arenas and the pool are reserved from it
as aligned blocks and released the same way, and aligned allocations come from
the same heap. The allocator-pairing rule still holds — memory from the platform
contract is released through the platform contract — but on this platform
getting it wrong would not corrupt a second heap, because there is none. That is
luck, not licence: the rule is what keeps shared code correct on the platforms
where there are two.

The reservation is refused when the map exceeds the budget, and when the heap
cannot supply it, naming the heap's size and how much of it is already in use.

Slot alignment stays at 16 KB, matching every other platform, so slot arithmetic
behaves identically everywhere. It is a whole multiple of the 4 KB granularity
the graphics driver maps memory in.

**A texture is charged at 32-bit colour, rounded up to 4 KB.** The default
renderer gives each texture its own mapped block, and a block cannot be smaller
than one page, so a small glyph atlas costs a page rather than its nominal size.
The fallback renderer's driver pools small textures and pays less than it is
charged; charging the larger of the two is what keeps a budget accepted by one
backend honoured by the other.

Heap statistics report the engine budget as the total and the map as what is
used, the same convention as the other consoles, and report as free what the heap
itself has left. Because both graphics drivers allocate from that same heap, the
free figure already has their memory taken out of it — it is the figure that
decides whether the next reservation succeeds.

## Storage and IO

| | |
|---|---|
| Max async read | 8 MB |
| Queued requests | 32 |
| Mounted archives | 4 |
| Resource handles | 256 |

**Two roots, with different rules.** Assets are addressed relative to the
executable's own read-only file system, which contains exactly what was packaged.
Writes go to the title's directory on the SD card, named after the title's
display name under the directory homebrew conventionally keeps its data in, and
created on first use.

If the executable carries no file system — a build that skipped packaging, or an
executable copied without its packaged form — initialisation fails naming the
cause rather than letting every later read fail individually. If the SD card is
not mounted, the file-write capability answers no.

The engine's canonical asset keys are uppercase and backslash-separated; the
platform translates on the way out, exactly as every other platform does.

The largest single read is 8 MB, twice the other consoles' figure, so that the
largest texture this platform cooks fits in one read together with its asset
header.

## Input

Controllers are read once per frame into the shared snapshot. The accepted
controller styles are the ones with the **full** set of controls — the handheld
rails, a pair of wireless controllers held together, and the full-size wireless
controller. A single wireless controller held sideways is not accepted, because
it lacks half the buttons and a second stick, and a pad that answered for
buttons it cannot produce would make the capability answers wrong. See
[Known limitations](#known-limitations).

Port 0 is the handheld controls and the first wireless controller together, so a
player can undock mid-session without losing the pad; ports 1 to 3 are the next
three controllers.

**Face buttons map by label, not by position.** The engine names its buttons
after the PlayStation pad, where the bottom button confirms. On this hardware the
right button, A, confirms and the bottom button, B, cancels. Mapping by position
would put confirmation on B, which every player of this console reads as *back*.
So A answers as the engine's confirm button and B as its cancel button, X and Y
as the other two:

| Engine button | This pad | Position on this pad |
|---|---|---|
| Cross | A | Right |
| Circle | B | Bottom |
| Triangle | X | Top |
| Square | Y | Left |
| L1, R1 | L, R | Shoulders |
| L2, R2 | ZL, ZR | Triggers, digital |
| L3, R3 | Stick clicks | |
| Select, Start | −, + | |

The consequence to know about is gameplay authored by position: an action bound
to "the bottom button" in the action map lands on the right-hand button here.
That is the deliberate trade — menus behave as this console's players expect,
and positional gameplay is a declaration a title can override in its own
bindings.

Button prompts use the **Nintendo** icon family, which draws A, B, X and Y on the
engine buttons the table above maps them to. It is a family of its own rather
than the Xbox one because the two agree on the letters and disagree on where each
letter is.

Stick axes arrive as signed sixteen-bit values with up positive; they are
normalised against their full scale with the vertical axis flipped, the same
conversion the desktop platform applies to its controllers, and the deadzone
constant applies to the result exactly as it does there.

The debug combinations are the full-pad ones:

| Intent | Buttons |
| :--- | :--- |
| Performance snapshot | L + ZL + R + ZR |
| Overlay toggle | L + ZL + left stick click + right stick click |
| Debug menu | − and + together |

**Touch is a device group of its own**, never a mouse. One surface, the front,
reports up to ten contacts normalised over the panel, each with a stable identity
for as long as the finger stays down. The panel reports contact, not pressure, so
every contact's force reads as one. Docked, it reports none and the capability
answers no.

## Window

There is no window, but the framebuffer is not fixed either.

| Mode | Framebuffer |
|---|---|
| Handheld | 1280 x 720 |
| Docked | 1920 x 1080 |

The size is read from the operation mode, which the platform samples once per
frame while it services the system's messages, and renderers read it at the start
of every frame. Both renderers keep 1920 x 1080 display buffers for the whole
session and draw into the top-left 1280 x 720 of them in handheld mode, telling
the display to show only that region, rather than rebuilding their buffers on
every dock — a rebuild would cost a visible stall exactly when the player is
moving the console.

The close query is positive once the system has asked the title to exit. The
native handle is the system's default window, which is what both renderers
attach their display buffers to.

## Graphics

| | |
|---|---|
| Framebuffer | 1280 x 720 handheld, 1920 x 1080 docked |
| Frame budget | 16667 us |
| Display aspect | 16:9 in both modes; pixels are square |
| Draw list capacity | 4096 |
| Texture budget | 128 MB |
| Max texture | 1024 x 1024 |
| Vertices per frame | 262144, both renderers |

## Renderers

| Backend | Role |
|---|---|
| [deko3d](renderers/DEKO3D.md) | **Default.** A thin, explicit interface to the graphics processor |
| [opengl](renderers/OPENGL.md) | A desktop OpenGL implementation over the same processor, kept as a known-good reference |
| null | Final fallback; headless. See [RENDERER.md](../subsystems/RENDERER.md) |

Fallback order is deko3d, then opengl, then null.

**Both renderers compile the same shader source**, so a frame that differs
between them is a bug in one of them, not a difference in what they were asked
to draw. The default renderer carries that source as a binary compiled at build
time; the reference one compiles the same text at run time through its driver.

**The two renderers share the graphics processor and cannot both hold the
display.** Falling back from the default to the reference renderer only works
because the default one releases its display buffers completely when it fails.
A failure that left them attached would make the reference renderer fail too,
and the title would run headless for a reason that has nothing to do with either.

`opengl` here is the same interface the desktop backend of that name drives,
through an entirely separate implementation that shares no code with it. See
[renderers/OPENGL.md](renderers/OPENGL.md).

The relationship between the two renderers mirrors the pair on each of the other
consoles: one drives the hardware directly and is the default, the other is a
library exposing a more familiar drawing model, kept as a reference to compare
against.

## Dialogs

**Text entry is the system's software keyboard.** It runs as a separate applet
that takes over the screen and returns when the player finishes, so there is no
frame to service while it is open — unlike the Vita, where the dialog is drawn
into the title's own frame. The call that opens it therefore blocks until the
player is done, and the result is handed to the first poll afterwards: the same
shape the desktop platform uses for its blocking message box, reached through the
same non-blocking contract. Typed text is converted to the engine's printable
ASCII at the platform boundary; anything outside it is dropped.

**Message and confirmation boxes are drawn by the interface.** The only system
message box available to a homebrew title is the error applet, which frames every
message as an error, offers no cancel and so cannot answer a confirmation at all.
Presenting an ordinary notice through it would tell the player something had
gone wrong. The platform refuses those two kinds, and the interface's own modal
draws them, as it does on every platform that refuses.

## Performance

The nine items [guidelines/PERFORMANCE.md](../guidelines/PERFORMANCE.md) asks
every platform to state.

**Budget and pacing.** 60 Hz, 16667 us, in both modes. The default renderer
builds the frame's command list while the previous frame may still be drawing,
then blocks acquiring a display buffer, submits, and queues the buffer for
presentation. Its vertex buffers are doubled and alternate every frame, so
staging overlaps drawing by one frame without writing into memory the graphics
processor may still be reading. The reference renderer blocks in its buffer swap,
at one swap per vertical blank.

**The binding constraint.** The main processor's single core running the frame:
every vertex of every visible object is transformed there every frame, and that
work does not scale with the graphics processor. Docked, fill rate is the second
constraint — the same scene costs 2.25 times the pixels at 1080p. Content fits
by processor-side vertex count first, and by overdraw second when docked.

**Per-frame ceilings.**

| | |
|---|---|
| Vertices per frame | 262144, world and screen space together, screen space reserved first |
| Draw list entries | 4096 |
| Interface quads | 16384, plus 2048 overlay quads |
| Draw runs | 1024 world, 256 screen-space |
| Resident sectors | 9 |

**Clock.** The system tick counter, 64 bits at 19.2 MHz. It does not wrap in any
session length that matters. Time spent out of focus is measured and subtracted,
so a title returning from the HOME menu sees an ordinary frame delta rather than
the length of the suspension.

**Scheduling.** The kernel is strictly priority-preemptive within a core and does
not time-slice between priorities, the PlayStation 2 and PlayStation Portable
hazard. Threads are also pinned to a core. The IO worker therefore sits one step
above the main thread, and on a different core from it when the process is
allowed more than one, so a frame loop that never blocks cannot starve it. The
worker blocks on a semaphore; it never polls.

**Cost of diagnostics.** Every log line goes to the system debug output — a
no-op on retail hardware without a debugger, captured by the emulator's guest
log — and to a synchronous write to the log file on the SD card, through the
file-system service. A diagnostic that fires every frame costs a service round
trip per line; the default renderer's vertex overflow report, emitted only when
the shortfall changes, is the pattern.

**What the snapshot measures here.** The default renderer reports geometry build,
upload, present wait (time blocked acquiring a display buffer) and buffer fill.
The reference renderer reports geometry build and present wait (time in the
buffer swap); upload and fill read as absent.

**Baseline.** 2026-09-17, **emulator** (Ryujinx 1.8.3, firmware 19.0.1,
Vulkan on an RTX 3060), debug build, the game's boot scene (main menu), default
`deko3d` backend, docked, launched as an application:

| | |
|---|---|
| Frame rate, heartbeat | 58.1 to 62.0, instantaneous, over 900 frames |
| Heap | 54 272 KB, constant for the whole run |
| Engine map | 50 176 KB arenas and 4 096 KB pool, from a 3.3 GB heap |
| Texture budget left | 130 976 KB after the menu's three textures |

The emulator holds the refresh rate whatever the frame costs, and launches a
homebrew title with the application allowance rather than the applet one, so
these figures prove that the loop paces, that nothing on the frame path grows,
and that the accounting is sane — not that content fits (rule 15). Not yet
taken: the snapshot's split, the backend's buffer fill, handheld mode, the
reference backend, and any figure on hardware.

**Left on the table**, in the order to attack it: moving the transform of static
world geometry onto the graphics processor, which this platform can afford and
the others cannot; a compressed texture vocabulary in the cook list, which the
hardware samples natively; staging on more than one core; the sky.

## Known limitations

- **The interface is not rescaled between modes.** It is laid out in framebuffer
  pixels at one metric scale, so it draws a third smaller when docked. A scale
  that followed the operation mode would need the metric scale to become a
  run-time value, which it deliberately is not today.
- **Single wireless controllers are not accepted as pads.** Held sideways, one
  has half the buttons and one stick; accepting it would require the capability
  answers to vary by which controller is in use.
- **USB keyboards and mice are not exposed.** The dock accepts them, but the
  platform does not read them, and it answers both capabilities no rather than
  claiming devices it never polls.
- **No system achievements.** Homebrew has no access to the console's own
  achievement service; the engine's cross-platform achievement system is what
  ships. See [ACHIEVEMENT.md](../subsystems/ACHIEVEMENT.md).
- **Launched as an applet, memory is tight.** The budget fits, but a title that
  adds large textures should be tested from the album launch rather than the
  application takeover, which hides the problem.
- **Motion sensors, rumble and the infrared camera are not exposed.** The
  platform contract has no vocabulary for them.
- **A failed renderer falls back silently on the console itself.** The engine
  logs it, but only the log file and the emulator's guest log show which backend
  actually started.
- **No sound**, as on every platform.
- Sector recentring is synchronous, as everywhere.
