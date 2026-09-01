# Subsystem — Renderer

## Purpose

Turn the frame the game described into pixels. The engine defines one rendering
contract; each platform supplies the backends that can satisfy it on that
hardware. Backend-specific behaviour is documented per backend, not here — see
the renderer specs under each platform:
[PS2](../ps2/PLATFORM.md#renderers), [Win32](../win32/PLATFORM.md#renderers),
[Vita](../vita/PLATFORM.md#renderers), [PSP](../psp/PLATFORM.md#renderers),
[nx](../nx/PLATFORM.md#renderers).

## Contract

**Backends are chosen by the platform, not the engine.** A platform declares
which backends it supports, which is the default, and what to try when one fails.
Shared engine code never names a backend and contains no conditional compilation
selecting one.

**Draw lists, then a frame.** The game submits work — primitives, models, level
geometry, sky, screen-space rectangles and UI — into per-frame lists. The backend
consumes them between a frame beginning and ending. Submission and execution are
separate so the engine can sort, cull and batch without the game knowing.

**Submission may precede the frame.** Some screen-space submission happens during
the game update, before the frame formally begins. Backends must therefore stage
two-dimensional and three-dimensional work independently: resetting all staging
at frame start discards work the game has already submitted. This is a real
defect that has occurred, and it presents as screen-space content vanishing while
world content is fine.

**There is one screen-space primitive.** A backend implements a single
axis-aligned quad carrying a position, a size, a colour with alpha, and
optionally a texture handle with texture coordinates. Everything
two-dimensional — the interface, the game's own rectangles, the panic display —
reaches a backend through it, so screen space is implemented once per backend
rather than once per kind of caller. An opaque untextured rectangle is that same
primitive with a full alpha and no texture, offered as a convenience so a caller
with no interface to build need not construct one.

**The interface is translated once, for every backend.** A frame of interface
arrives as one batch of screen-space quads, and turning that batch into the
primitive above is done in the shared layer rather than per backend. This is
deliberate: comparing two backends rendering the same frame is the primary way
rendering bugs are located here, and an interface reimplemented once per backend
would differ once per backend and destroy that comparison. Every field of a
submitted quad reaches the backend, so a difference between two backends is a
difference in the backend, never in what was submitted.

**Screen space blends; world geometry does not.** Every backend blends the
screen-space pass against what is already in the framebuffer, using the quad's
own alpha, and every backend draws that pass with depth testing off. Both are
required rather than optional: without blending an interface cannot dim what is
behind it, and without disabling the depth test a screen-space quad is occluded
by whatever world geometry happened to write depth underneath it — which is a
real defect that has occurred, on the backend whose screen-space pass was the
only one not to disable it. Whether the world pass blends is a separate question
each backend answers for itself.

**Built-in primitive shapes are staged, not built in.** The engine describes a
cube, a sphere and a cylinder once, in a form no backend can consume directly.
Converting them into drawable arrays is part of **constructing** a backend, and
the storage for those arrays is the backend's share of the renderer arena. A
backend that skips that step is fully functional in every other respect and
still draws level and model geometry — but every primitive the game submits is
discarded before it reaches the frame. This is a real defect that has occurred,
on a new backend, and it presents as a world that renders while everything the
game draws directly is missing.

**What cannot be drawn is rejected before it is built, not after.** A backend
declares what it can accept for the coming frame; staging then refuses whole
entries that fall outside the view or will not fit, and reports them as culled.
Building geometry and discarding it at upload time costs the full price of work
that was never going to be shown, and it discards at an arbitrary point — mid
object, and mid triangle. Rejection is by whole entry for that reason.

**Screen-space work is reserved before world geometry is staged.** Its cost is
already known by then, because it was submitted during the game update. Without
that reservation a heavy world silently consumes the whole budget and the
interface disappears exactly when a player most needs it — which is a real
defect that has occurred. Where a backend still has to truncate as a last
resort, world geometry yields and the interface is kept.

**Textures are uploaded, then referenced by handle.** A backend accepts a decoded
texture and returns a handle; the invalid handle is a fixed value every backend
agrees on. The engine asks the platform, not the backend, what a texture *costs*
in bytes — that is a hardware question. It asks the **backend** how much it can
hold, because two backends on one platform can be left with very different
amounts after their own frame and depth buffers, and a ceiling derived from one
of them is simply wrong for the other. A backend that has no opinion answers with
the platform figure, so this costs nothing where it does not apply. Getting it
wrong is not a rounding error: it let a resource manager accept roughly eight
times the textures a backend could actually store, and turned an up-front,
actionable rejection into a stream of failures inside the backend. See
[Resource](RESOURCE.md).

**Cameras are addressed by slot.** Several three-dimensional cameras may be
configured; one is active. Two-dimensional rendering uses a separate camera.

**A model or primitive may be rendered to an image instead of the frame.**
This is a distinct, synchronous entry point (`RenderToImage3D`), not a flag on
the ordinary draw-list submission: the ordinary path defers everything
submitted during the game update to be processed once `Render()`/`EndFrame()`
runs, and a render-to-image caller — the interface, compositing the result as
a texture in the same frame it asked — has no later point to pick up a result
that arrived that way. It therefore clears, draws and resolves before
returning, backed by one scratch offscreen target per backend, reused and
resized on demand rather than allocated per call. Submitting it is the
caller's job, during the same phase ordinary submission already happens in;
issuing it from inside `Render()` is unsupported. Backends answer honestly
when they cannot do this — GS-generation hardware sources a texture read and
a framebuffer write from the same memory, which is what makes the trick
possible there at all, but only where the backend's own toolchain also
exposes a way to prove the hardware has actually finished before the result
is sampled; where it does not, the backend says so rather than guessing. This
is deliberately narrow — one object, one target, no arbitrary scene — and
does not relax the "no render graph" limit below. See
[UI](UI.md) (`Ui_Image3D`, the caller every scene reaches this through) and
each backend's own spec for which of them implement it.

**Every frame reports statistics** — draw counts and geometry submitted — because
the performance snapshot in [Debug](DEBUG.md) reads them, and a backend that does
not report them cannot be compared against one that does.

**Failure is loud and ordered.** When a backend fails to initialise, the failure
and its specific reason are logged, then the platform fallback is tried, and that
attempt is logged too. Falling back silently produces a running engine that looks
wrong for reasons nobody can see.

**The null backend terminates every chain.** It satisfies the contract and draws
nothing, so the engine always has a renderer and the frame loop never has to test
for its absence. It exists for **headless hosting and logic tests**, not as a
graphics fallback anyone should ship — reaching it by fallback means every real
backend failed, which is an error condition, not a degraded mode.

## Depends on

- **Platform** — backend construction, the window or display, and the fallback
  order.
- [Memory](MEMORY.md) — geometry staging comes from the renderer arena segment.
  Backends take that storage **as they are constructed**, so memory must be
  reserved before any backend is built.
- [Resource](RESOURCE.md) — texture data to upload.
- [Level](LEVEL.md) and [Sector](SECTOR.md) — resident world geometry to draw.

## Depended on by

- [Debug](DEBUG.md) — overlay and, on some platforms, the panic display.
- [Resource](RESOURCE.md) — texture upload and release.
- Game code, through the public game API.

## Lifecycle

Constructed after memory is reserved and before the engine starts, because engine
startup verifies a live renderer. A frame is begun, cleared, submitted to,
rendered and ended, in that order, every frame. Shutdown releases textures and
backend resources; the platform that constructed the backend destroys it.

## When not loaded

Not applicable — the renderer is never absent. Selecting the null backend is how
a game runs without graphics, and it is a backend choice rather than a subsystem
being disabled. This is deliberate: making the renderer optional would put a
presence test in every draw path for a case the null backend already handles at
zero cost.

## Failure modes

- **Every backend fails** — the null backend takes over and the engine runs
  blind, with each failure logged in order. The engine does not exit; on hardware
  with no console, a running engine that logs is more diagnosable than one that
  vanished.
- **Backend constructed before memory is reserved** — the backend gets no
  staging storage and fails immediately. This has happened; the ordering above is
  the fix.
- **Texture upload fails** — reported per asset with the slot and asset name; the
  frame still renders, untextured.
- **Unsupported backend requested** — refused at startup with the list of
  backends this platform actually supports, rather than a generic list of every
  backend the engine knows.
- **`RenderToImage3D` cannot be honoured** — a backend with no offscreen-target
  support, a request that failed (budget, an unsupported size), or a model that
  is not yet ready all answer with the same invalid handle a failed
  `UploadTexture` would. There is no separate failure channel because the
  caller (`Ui_Image3D`) already has to treat every one of those cases alike: a
  placeholder in the interface, not a stall waiting for a texture that will
  never arrive.

## Limits

- Draw list capacity is fixed per platform; overflow drops the excess and logs.
- One active three-dimensional camera at a time.
- Backend feature coverage is not uniform, and the gaps are recorded in each
  backend spec rather than being discoverable only by observing a missing effect.
- There is no render graph, no post-processing chain, and no shadow system.
  `RenderToImage3D` is not an exception to this: it is one caller-chosen object
  rendered into one scratch target, synchronously, on request — not a
  general graph of passes the renderer schedules on its own.
- **`RenderToImage3D` draws exactly one object, always at the origin**, into a
  scratch target every backend that supports it owns exactly one of. A second
  call — for a different preview, or a differently sized one — retargets that
  same storage, invalidating whatever texture handle an earlier call this
  frame or a previous one returned. There is no way to hold two renders of it
  open at once.
- **A backend draws no diagnostics of its own.** The on-screen overlay is
  interface, built from the same widgets as everything else, so it exists once
  rather than once per backend and cannot differ between them.
- **There is no scissor or clip rectangle in the screen-space contract.** The
  interface clips its own quads before submitting them, which is not merely a
  substitute: clipped-away content then costs nothing to submit *or* to draw,
  where a hardware scissor would still pay for both. It also keeps the batch
  free of interleaved state, so a backend never has to interpret ordering. One
  backend's graphics library has no usable scissor at all, so this is the only
  form of clipping that works everywhere.
