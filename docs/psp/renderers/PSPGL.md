# Renderer — pspgl (PSP)

The fallback backend on this platform. Not the default.

Contract and shared behaviour: [RENDERER.md](../../subsystems/RENDERER.md)

## What it targets

A fixed-function subset of an older desktop drawing model, implemented over the
same graphics hardware the default backend drives directly. It is the exact
counterpart of `ps2gl` on the PlayStation 2 and `vitagl` on the Vita: a library
exposing a simpler, immediate drawing model, kept as a known-good reference to
compare the direct backend against.

It is **unrelated** to the desktop OpenGL backend despite the lineage of the
name, exactly as those two are. They share no code and no platform hosts both.

**It ships inside the development kit.** Unlike the Vita's fallback, which is
vendored source built from a submodule, this library is a first-party part of the
toolchain and is linked like any other kit library. This platform therefore
carries no third-party dependency, no submodule and no pre-engine build step —
the one place where this platform is simpler than the two it was modelled on.

## Model

**Geometry is staged on the processor and uploaded once**, through the shared
geometry stager. The copy handed to the library is the backend's share of the
renderer arena; the stager's own working arrays are heap-grown, as on the
default backend (see
[backlog/performance_findings.md](../../backlog/performance_findings.md)). It
stages identically to the default backend, which is the whole point: a frame
difference between the two is a bug in one of them, not a difference in what
was submitted.

**Textures live in video memory**, allocated through the library's own video
memory allocator. Video memory is 2 MB and the frame and depth buffers already
spend most of it, so what remains for textures is a fraction of what the default
backend has in main memory. This backend therefore **overrides** the texture
budget with the figure it can actually honour.

That override is load-bearing. Reporting the platform's main-memory figure here
would let the resource manager accept several times the texture this backend can
store, turning an up-front, actionable rejection into a stream of failures inside
the backend. The engine asks the *backend* how much it can hold precisely because
two backends on one platform can differ this much.

**Textures are expanded to 32-bit colour on upload**, using the shared expansion
helper, because this drawing model has no palettised path. That is the second
half of why this backend holds so much less: the cooked asset that the default
backend uploads as a palette and indices is expanded fourfold here before it is
stored.

## Quirks and limits

- **The texture budget is a fraction of the default backend's**, for the two
  reasons above compounding — a smaller pool, holding larger copies of the same
  assets. A scene that fits comfortably on the default backend can fail to load
  its textures here. That is reported as an over-budget rejection naming what
  could be freed, not as missing textures.
- **Texture dimensions are capped at 512 in each axis and must be powers of two**,
  as on the default backend. The hardware limit is the hardware limit regardless
  of which drawing model reaches it.
- **The screen-space pass blends and runs with depth testing off**, as the shared
  contract requires.
- **Two-dimensional and three-dimensional work is staged independently**, for the
  same reason as on the default backend: screen-space work submitted during the
  game update must survive the start of the frame.
- **The built-in primitive shapes are staged when the backend is constructed.**
  This is the failure that looks like a working renderer — level and model
  geometry draw, every game-submitted primitive silently disappears.
- **A presented frame is handed to the dialog service while a dialog is open.**
  Both backends must do this independently; there is no shared present path that
  would do it for them. A backend that forgets draws a correct frame with an
  invisible dialog on top of it consuming input.
- **The fixed-function model has no scissor worth using**, which is one of the
  reasons the screen-space contract has none and the interface clips its own
  quads before submitting them.
- **The sky and far-field paths are not implemented**, as on the default backend.
- **State changes are more expensive than on the direct backend**, because each
  one becomes library bookkeeping before it becomes a hardware command. This
  backend is slower and is not trying not to be.
- **`RenderToImage3D` (`Ui_Image3D`) is not implemented.** The library exposes
  no framebuffer-object or pbuffer-surface path in this build — no
  `GL_OES_framebuffer_object` entry points, no `eglCreatePbufferSurface` — and
  it owns display-list submission internally behind `eglSwapBuffers` with no
  hook to redirect it, so there is no way to point the graphics engine at an
  offscreen target without reaching around the library and corrupting its own
  state tracking. This backend always answers 0, the same honest "unsupported"
  every other backend without an offscreen target gives. [Gu](GU.md), the
  default backend, implements it directly against the hardware and is the
  answer when a build needs this on PSP.

## When to prefer it

When the default backend draws something wrong and you need a second opinion from
the same submitted frame. That comparison is what this backend is for, and it is
the fastest way to establish whether a rendering bug is in a backend or in what
was staged.

Not for shipping, and not for judging performance — see the texture ceiling
above, which is the binding constraint long before frame time is.
