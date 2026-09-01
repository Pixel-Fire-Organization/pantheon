# Renderer — gu (PSP)

The default backend on this platform.

Contract and shared behaviour: [RENDERER.md](../../subsystems/RENDERER.md)

## What it targets

The console's native graphics interface, driven directly: the processor builds a
display list of hardware commands and hands it to the graphics engine, which
consumes it asynchronously. It is the same relationship the Vita's default
backend has with its hardware, and the same one the PlayStation 2's packet
backend has with the GS — the direct path, with nothing between this code and the
command stream.

Its sibling [pspgl](PSPGL.md) reaches the same hardware through a fixed-function
library and exists to be compared against.

## Model

**Geometry is staged on the processor and uploaded once.** It uses the shared
geometry stager, as both desktop backends and both Vita backends do, so a frame
difference between this backend and its sibling is a difference in the backend
rather than in what was submitted. The converted copy the hardware reads is the
backend's share of the renderer arena, taken when the backend is constructed;
the stager's own working arrays are taken from the C heap and grow on demand,
which [backlog/performance_findings.md](../../backlog/performance_findings.md)
tracks as a gap against the fixed-budget rule.

**Textures live in main memory, not video memory.** Video memory is 2 MB and the
frame and depth buffers spend most of it; what is left is not enough to be worth
managing as a texture pool. So the texture budget is the platform's own main
memory figure, reported unchanged — this backend has no opinion to override it
with. Its sibling, which does keep textures in video memory, reports a far
smaller figure, and that divergence is the reason the budget is asked of the
backend rather than the platform.

**Palettised textures are uploaded as a palette and an index buffer**, with no
expansion. The cook list encodes them that way on purpose: an expansion step
would cost four times the memory on the platform that can least afford it, and
the hardware samples the indexed form natively. A 32-bit asset is uploaded as it
is.

**Their alpha is still rescaled, and skipping that is not subtle.** Cooked
textures carry the console convention in which half-scale alpha means fully
opaque. Every backend that expands to 32-bit colour rescales as part of
expanding; a backend that uploads the cooked bytes verbatim — which is the whole
point of this one — must do it deliberately or every texel arrives at half
alpha, and with blending enabled the entire world draws semi-transparent.
Palettised textures make this nearly free: alpha lives only in the colour table,
so 256 entries are corrected however large the image is. A 32-bit texture pays
per texel, which is one more reason the cook list prefers the palettised form
here.

**Presentation is a buffer swap synchronised to the display.** While a system
dialog is open the presented frame is handed to the dialog service first — see
the platform spec; a backend that skips that draws a correct frame with no dialog
visible on it, while the dialog is open and eating input.

## Quirks and limits

- **The display list is consumed asynchronously, and the buffer must not be
  touched while it is.** Overwriting a list the graphics engine is still reading
  produces geometry from a previous frame mixed into the current one, or a hang
  with no diagnostic. The lists are double-buffered for that reason.
- **Memory handed to the graphics engine must be flushed from the cache first**,
  or written through the uncached alias. The engine reads main memory directly
  and does not see the processor's dirty cache lines. The symptom is stale
  geometry or stale texture content that changes depending on what else ran that
  frame, which is about as unhelpful as a symptom gets. Arena slots are aligned
  well past the cache line so a flush never catches a neighbour.
- **The texture cache does not notice a new image, and must be flushed after
  every bind.** The graphics engine caches texels; pointing it at different
  pixels without invalidating that cache leaves it sampling the previous run's
  texture. The symptom is the worst kind: the frame looks right from one camera
  position and wrong from another, because what breaks is whichever run happens
  to follow which — so it reads as a camera or culling bug rather than a texture
  one. This is a real defect that has occurred here.
- **The depth buffer is 16 bit, and that is not a detail.** With the 0.1 near
  plane every other platform here uses, the depth quantum at fifty units from
  the eye is about a third of a world unit and at two hundred units it is six —
  so coplanar and near-coplanar surfaces fight, and the fight changes as the
  camera moves. Nothing culls back faces on any backend in this engine, so
  interior faces punch through exterior ones as soon as precision runs out, and
  the result is speckling over large surfaces rather than an obviously wrong
  frame. The near plane is raised to compensate; it is a platform constant, and
  lowering it back to the desktop value reintroduces this immediately.
- **The per-frame vertex ceiling is a fixed buffer, and setting it too low does
  not degrade gracefully.** Whole entries are rejected once the frame budget
  fills, so a ceiling below what the scene needs drops *different* objects each
  frame as the camera reorders them — which reads as the world flickering, not
  as a budget being reached. Screen-space work is given its own span sized from
  the interface budget rather than a fraction of the world's, because sharing
  one span lets a heavy world truncate the interface exactly when it is most
  needed. This is a real defect that has occurred here, in both directions.
- **Texture dimensions are capped at 512 in each axis by the hardware.** This is
  not a budget. A larger texture cannot be sampled at all, so the cook list
  enforces the cap rather than letting it fail at upload.
- **Texture dimensions must be powers of two.** A non-power-of-two texture is
  sampled with the wrong stride, which reads as a sheared image rather than as a
  rejected upload.
- **Swizzled textures sample considerably faster, and unswizzled ones are
  correct.** Both work; the difference is bandwidth, and bandwidth is the
  constraint on this hardware rather than fill rate.
- **The screen-space pass blends and runs with depth testing off**, as the shared
  contract requires. Leaving depth testing on is the defect where interface
  content is occluded by whatever world geometry wrote depth underneath it.
- **Two-dimensional and three-dimensional work is staged independently.** Some
  screen-space submission happens during the game update, before the frame
  formally begins; resetting all staging at frame start discards it, and presents
  as interface content vanishing while the world is fine.
- **The built-in primitive shapes are staged when the backend is constructed.**
  Skipping that leaves a backend that draws level and model geometry perfectly
  while silently discarding every primitive the game submits.
- **There is no scissor in the screen-space contract**, and this backend needs
  none: the interface clips its own quads before submitting them.
- **The vector unit is not used.** Vertex transformation runs on the main
  processor. This is the largest single piece of performance left unclaimed on
  this platform.
- **The sky and far-field paths are not implemented.** They are outstanding on
  the desktop backends too; this backend draws primitives, models, level geometry
  and interface.
- **`RenderToImage3D` (`Ui_Image3D`) reserves its scratch target from otherwise
  unclaimed VRAM, and never resizes it.** The display and depth buffers are
  placed once at construction by a private bump allocator that only ever
  grows; the tail it leaves free (roughly two thirds of this platform's 2 MB)
  is enough for a small colour-plus-depth target, taken the same way on first
  use. A later request at a different size is refused rather than
  reallocated, since the allocator has no per-object free to reclaim the
  first size's bytes. The result is sampled the same "framebuffer memory is
  texture memory" way a PS2 GS backend samples its own scratch target: the
  target's VRAM address is bound directly as a texture, unswizzled — this
  engine's textures already sample correctly unswizzled, so no extra pass is
  needed. The draw runs in its own display list, not the frame's
  double-buffered ones (`EndFrame` has not built this frame's yet when a
  caller reaches this), and blocks on `sceGuSync` before returning. Because
  the draw-buffer/offset/viewport/scissor registers persist across
  `sceGuStart` calls rather than resetting with each one, the same list also
  restores them to the main framebuffer's before it ends — skipping that
  would leave the *next* ordinary frame drawing into this scratch target
  instead of the screen.

## When to prefer it

Always, unless you are chasing a suspected bug in this backend. It is the
default, it holds four times the texture its sibling does, and it is the only one
of the two with a path to using the hardware properly.
