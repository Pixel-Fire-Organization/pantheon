# Renderer — gxm (Vita)

The default Vita backend, driving the console graphics API directly.

Contract and shared behaviour: [RENDERER.md](../../subsystems/RENDERER.md).

## What it targets

The console native graphics API: an explicit, low-level interface with
programmable shading, manual memory management and an application-driven display
queue. It is the same relationship the direct console backend on the PlayStation 2
has to that machine — closest to the hardware, most work to write, and the one
worth optimising.

## Model

**Shaders are compiled at build time, not at run time.** The console ships a
runtime shader compiler, but it is a system component the player must extract
themselves, so depending on it would make the default renderer fail on an
ordinary machine. The shader set is fixed, small, and linked into the executable
as finished binaries. This is the single property that justifies this backend
being the default — the fallback does not have it.

Compiling them needs an offline shader compiler that the open toolchain does not
ship, so the build **requires** one and refuses to configure without it. It is
not committed to this repository. See [BUILD.md](../BUILD.md).

**Graphics memory is allocated by the backend and mapped explicitly.** Render
targets and textures come from the video pool; vertex and index data and the
shader scratch region come from their own allocations. All of it is mapped for the
graphics processor before use and unmapped on shutdown. None of it passes through
the engine arenas, which describe processor-side memory only.

**Display is a queue, not a swap.** Frames are handed to a display thread that
presents them on the vertical blank. The backend must not assume a frame is
finished being read when it returns.

Geometry follows the model the other backends use: staged on the processor,
uploaded once per frame, drawn as runs sharing a texture. Three-dimensional and
screen-space geometry stage independently.

## Quirks and limits

- **The screen-space pass blends; the world pass does not.** Blending is enabled
  only for screen space, so world rendering is unaffected by the interface
  gaining transparency. Depth testing is off for the same pass, so quads draw in
  submission order.
- **Screen-space work is drawn as one call per texture run.** Consecutive quads
  sharing a texture coalesce, so a solid interface is a single call and a
  glyph-atlas interface is a small number. A run count ceiling exists; exceeding
  it drops the excess and reports once per frame.
- **A texture chooses its filter at upload and cannot change it afterwards.**
  The filter is fixed in the sampler or texture object at creation. Nearest
  exists for content that is magnified to whole-pixel scales — a pixel font
  atlas is the case that needs it, and linear filtering visibly blurs it.
- **The fragment program already multiplies texture by vertex colour**, so the
  glyph atlas needs no new shader and therefore no offline shader compilation.
  This is the reason the interface's textured path costs nothing to add here.
**It hands each presented frame to the dialog service while a system dialog is
open.** A dialog on this platform is composited into the title's own back buffer,
so the surface, its format, its stride and the frame's sync object are passed
over after the scene is closed and before the frame is queued for display. Skip
it and the title draws perfectly with no dialog on it, while the dialog is open
and taking input. See [../PLATFORM.md](../PLATFORM.md).

- **Pixels are not square.** The framebuffer is 960 x 544 but the panel is 16:9,
  so projection must use the display aspect. Using the framebuffer ratio produces
  a frame that is subtly stretched and looks correct until compared against
  another platform.
- **Memory must be mapped before the graphics processor sees it.** An unmapped
  pointer is not a fault at the point of use — it is a fault later, in the
  display thread, with a stack that points nowhere useful.
- **The video pool has its own allocation granularity**, larger than a page. A
  small texture still consumes a whole unit, so the budget is spent faster than
  the sum of texture sizes suggests. The platform texture footprint accounting
  reports the real cost, not the nominal one.
- **The shader set is unlit, and binds no normal attribute.** Geometry is staged
  with normals because other backends want them, but nothing here consumes one —
  so on this platform they are transformed and uploaded per vertex for nothing,
  which is a quarter of the per-vertex work and a quarter of the bytes, on the
  weakest processor in the family.
  Binding an attribute the shader does not use does not work: the compiler
  removes it, the name cannot then be found, and renderer setup fails - which
  reads on hardware as the engine silently falling back to the other backend.
  The build now refuses a shader that has dropped a parameter the backend binds,
  so this fails at compile time instead.
- **Shaders are fixed at build time.** A material needing something the shader
  set does not express cannot be expressed at all without adding a shader and
  rebuilding. That is the deliberate cost of not depending on the runtime
  compiler, and it is the right trade for a default backend.
- **There is no non-indexed draw.** Every draw call takes an index buffer, so one
  is filled once at construction with an identity sequence and each run is a span
  into it. This is not a design choice; the hardware has no other draw path.
- **The vertex ceiling is fixed and enforced.** Vertex and index buffers are
  graphics memory reserved once, because growing them mid-frame would stall the
  display. The shared stager will happily build far more than this machine can
  hold, so the ceiling is declared to the stager, which refuses whole entries
  that will not fit rather than letting the frame be truncated on upload. How full the buffer is — including by how much a frame overshot — is
  reported every frame in the performance snapshot; the log line is emitted only
  when the shortfall changes. Reporting it per frame instead is itself ruinous
  here: writing to the memory card costs far more than the frame it describes, so
  a per-frame diagnostic becomes the slowest thing in the frame. See
  [DEBUG.md](../../subsystems/DEBUG.md).
- **Beginning a scene restores depth but not colour.** Depth comes back from the
  surface background; colour does not, so the frame is cleared by drawing a
  full-screen quad before anything else. Omitting it leaves the previous frame
  visible wherever nothing is drawn this frame, which reads as flickering rather
  than as a missing clear.
- **The depth buffer is sized to tile-aligned dimensions**, not to the visible
  framebuffer. The hardware renders whole tiles, and a depth buffer sized to the
  visible area is overrun on the last row.
- **Textures are expanded straight into graphics memory.** They must be mapped
  before the graphics processor sees them, so there is no processor-side copy to
  expand into first. Releasing one drains the pipeline first, because the frame in
  flight may still be reading it.
- **`RenderToImage3D` (`Ui_Image3D`) is a second scene, run to completion before
  the frame's own.** The hardware forbids a second open scene — a call may not
  begin until the previous one's has ended — so this always runs fully (begin,
  clear, draw, end) strictly before the frame's own `sceGxmBeginScene`, never
  nested inside it; callers already promise to call it during game/scene/Testbed
  update, which is what keeps that ordering true. Ending a scene only queues the
  work, so an explicit `sceGxmFinish` forces it to complete before the handle is
  handed back — the single most likely place to get this capability wrong here.
  The offscreen target (its own render target, colour surface and tile-aligned
  depth-stencil surface, video-pool granularity and all) is lazily created,
  reused, and resized on demand rather than negotiated once, since CDRAM here is
  considerably less pressured than PS2 GS VRAM. Its colour format matches the
  display buffers exactly, so the ordinary shader draws into it unchanged — no
  second pipeline variant, unlike the WebGPU backend.
- **The sky is not implemented.** World sectors, models, primitives and interface
  elements draw. The console backends on the PlayStation 2 do draw the sky, so
  this is a gap here rather than an engine-wide one.
- **Far-field geometry is drawn by no backend on any platform.**
- **Console pixel formats are expanded on upload**, as on the desktop backends.
  Textures cooked palettised or at sixteen bits are expanded to plain 32-bit
  colour, and the budget is charged the expanded size. Alpha is rescaled from the
  console convention where half-scale means fully opaque.

## The shader compiler

The compiler is Sony's, redistributed by third parties rather than licensed for
redistribution, so this project does not carry a copy: it is fetched by each
developer and is excluded from version control. The build **requires** it and
fails to configure with a message naming where it looked — deliberately, because
a silent fallback to the other backend would swap a self-contained title for one
needing a player-supplied component, which is exactly the surprise this design
exists to avoid.

It is a Windows executable, and this project builds under WSL. It runs there
through interop, but it is a Windows process and understands only Windows path
strings; handed a Linux path it fails with "the target path is invalid", which
says nothing about the real cause. That translation is handled in one place by
the build's shader step rather than being spread through the build rules.

## When to prefer it

It is the default, and for a title that ships to other people it is the only
sensible choice: it is the direct path to the hardware, its memory is accounted
for, it can be shut down cleanly, and it needs nothing installed on the player's
console. Prefer [vitagl](VITAGL.md) only when comparing two independent
implementations to decide which one is wrong.
