# Renderer — giftag (PS2)

The default PS2 backend. Transforms geometry on the main processor and writes
hardware display packets directly, with no intermediate graphics library.

Contract and shared behaviour: [RENDERER.md](../../subsystems/RENDERER.md).

## Model

Vertices are transformed in batches by the vector coprocessor and written
straight into a display packet as interleaved texture-coordinate, colour and
position registers. The limit is therefore **packet capacity**, not draw-call
count — the opposite of the [ps2gl](PS2GL.md) backend, and the reason the two
have completely different budgets.

Geometry packets are **double-buffered**: while the hardware drains one frame,
the processor builds the next into the other. This is what keeps the transform
cost off the critical path.

**The vector-unit batch path is guarded.** At construction the backend
transforms a few vertices through the toolchain's batch routine and through a
scalar transform, and uses the scalar path for the whole session if the two
disagree. The batch routine (ps2sdk's `calculate_vertices`) divides its
output's x/y/z by the homogeneous w internally and leaves w undivided, rather
than returning the same raw clip-space vector a from-scratch matrix multiply
would; the guard and the runtime transform both account for that now, and the
self-test passes under emulation. See the platform spec's Performance section
and [backlog/performance_findings.md](../../backlog/performance_findings.md)
for the history.

**The batch routine's own frustum test can silently return the wrong thing.**
`calculate_vertices` compares each vertex's clip-space x/y/z against its w and,
when one exceeds it, returns exactly `(0, 0, 0, 1)` instead of a projected
position — its own definition of "clipped." Downstream, that reads as a
vertex sitting exactly at screen centre with w = 1, not as a vertex to
discard: the guard-band and visibility checks around it were written for the
scalar path's convention (an off-screen vertex has real, out-of-range
coordinates) and were never taught this batch-specific one. In practice a
vertex has to leave the view frustum by more than the guard band affords for
this to matter, which the boot scene's geometry does not exercise, but
content that puts vertices there — something very close to the camera, or
right at the frustum's edge — would render at screen centre instead of being
culled. Unmeasured whether this can be reached by ordinary content; revisit
before trusting the batch path on a scene with close-up geometry.

## Budgets

| | |
|---|---|
| Packet capacity | 61440 quadwords each, about 960 KB |
| Packet buffers | 2, double-buffered |
| Transformed vertices | 40000 per frame |
| Transform batch | 1024 vertices |
| Reserved headroom | 64 quadwords per packet |

A textured strip vertex costs three registers, or one and a half quadwords, so
packet capacity works out to roughly forty thousand textured vertices — which is
where the vertex cap comes from. The packet quadword count is a sixteen-bit
field in the hardware, so capacity cannot be raised past its range by
configuration.

## Quirks and limits

- **Each batch names its primitive in a register write, not in its transfer
  tag.** The tag format has a field for the primitive and a flag saying to use
  it, and that field is *not* honoured here: a batch that relies on it draws with
  whatever primitive and attributes were last set, so geometry appears but is
  silently the wrong shape and never textured. The symptom is vicious, because
  the leftover primitive is a screen-aligned rectangle: a scene rendered entirely
  as rectangles covers roughly the same pixels as the triangles it replaced, so
  the horizon lands where it should and the image reads as plausible until it is
  compared against [ps2gl](PS2GL.md) side by side. Writing the primitive register
  explicitly costs one tag and one register per batch and removes the ambiguity.
- **A textured draw's vertex colour is on a different scale from an untextured
  one's.** Modulation treats 128, not 255, as unity, so a colour passed straight
  through doubles the texture's brightness and everything above half intensity
  saturates. It does not look obviously broken — it looks like a slightly
  blown-out texture, and faint detail simply disappears. Untextured geometry uses
  the full range, because there its colour is the result rather than a multiplier.
  This is a GS hardware fact, not something specific to this backend's own
  packet-writing code — [ps2gl](PS2GL.md) hits the same conversion wherever it
  writes a plain (non-lit) colour for a textured draw, and needed the same fix.
- **World geometry is drawn without back-face rejection.** Compiled level faces
  are not reliably wound after the map-to-engine coordinate transform, so the
  pass draws both sides, exactly as the [ps2gl](PS2GL.md) backend does. It costs
  fill rate on a platform that has little to spare, and it is a content problem
  rather than a renderer one.
- **Primitive coordinates are centred, not screen-relative.** Vertices are
  written around the middle of the hardware's coordinate space and the display
  offset is subtracted again on the way to the window, which is what gives
  off-screen geometry a symmetric guard band. Writing screen-relative
  coordinates instead puts the whole scene outside the visible rectangle and
  nothing is drawn at all — a defect that hid completely until the platform was
  run, because it produces a correctly-paced frame loop over an empty screen.
- **Geometry leaving the guard band is rejected whole.** There is no clipping.
  A triangle or a strip run with any vertex outside the representable coordinate
  range is discarded entirely and counted as culled, rather than being clamped:
  clamping would silently deform the primitive, and the coordinate field wraps
  rather than saturating, so an unchecked value lands somewhere arbitrary on
  screen.
- **Excess geometry is dropped loudly, never silently.** Exceeding the vertex cap
  or packet capacity logs and discards the overflow. Silently overrunning a
  display packet corrupts the hardware transfer, which presents as a hang or
  garbage far from the cause — a dropped triangle and a log line are strictly
  better.
- **Screen-space quads are queued, with a fixed per-frame ceiling.** Interface
  drawing is submitted before the frame begins, so it is buffered and emitted at the
  end. The queue is sized as the interface budget plus a fixed headroom for the
  game's own rectangles and the panic display, so raising the interface budget
  raises the queue with it rather than silently dropping quads inside the backend
  and blaming the backend. Beyond capacity they are dropped, and the overflow is
  reported **once per frame, with a count** — never once per dropped quad. A
  per-item diagnostic on this platform's console costs more than the frame it
  describes, so it would replace the problem it reports rather than measure it.
- **Every texture binding writes a colour-table descriptor, palettised or not.** The register writer reads the descriptor whatever the pixel format, so the case with no colour table is an inert descriptor rather than an absent one. Passing nothing there faults on a null pointer at the first textured draw — which is what happens the first time any texture is bound, so it is a defect that hides completely until the platform is actually run.
- **The screen-space pass turns pixel testing off around itself.** Its sprites
  carry no depth, and the frame's depth test compares against what world geometry
  wrote, so without this the interface is occluded wherever anything was drawn
  underneath it. Bracketing the batch costs two register writes and is correct
  regardless of what the frame clear left in the depth buffer; giving every
  sprite a maximum depth instead would cost bits on every vertex forever and
  would break again the moment the depth convention was inverted.
- **Blending is a per-quad bit, not a state change.** The hardware takes it from
  a field of the primitive descriptor that every batch already emits, so a
  translucent quad costs nothing extra to set up, opaque quads pay no fill rate,
  and draw order is preserved exactly — no sorting into opaque and translucent
  passes. The blend equation itself is written once per frame.
- **Alpha saturates at half scale.** The hardware's fully-opaque alpha is 128,
  not 255, so submitted alpha is rescaled on the way in. A value written straight
  through would clip, and every translucent quad would be more opaque than asked.
- **Screen-space texturing addresses texels directly.** Textured quads use the
  hardware's unnormalised texture coordinate path, so the interface's normalised
  coordinates are converted against the bound texture's dimensions at emit time.
  This is why a screen-space texture must have power-of-two dimensions: the
  texture register stores them as exponents.
- **The screen-space flush checks packet space per quad**, as the triangle paths
  do, and drops the remainder of the batch with one report if the packet is
  exhausted. A textured quad costs four qwords against an untextured quad's
  three, so a full interface is a larger share of the packet than it used to be.
- **The interface's share of the packet is reserved before world geometry is
  built.** Its cost is already known by then, because it was submitted during the
  game update. Every world draw is measured against capacity minus that
  reservation, so a heavy world yields and the interface survives, rather than
  the interface vanishing exactly when a player needs it. Texture bindings are
  counted against the same reservation, and a strip is measured per emitted run
  rather than once for the whole strip — a run carries its own header, so a
  single up-front estimate understates a strip that the near plane has split.
- **Texture memory is what the frame and depth buffers leave.** This backend
  reports its own ceiling rather than the platform's, because that figure was
  derived from the other backend's buffer layout and is around eight times what
  this one actually has. Loads are then refused up front, with the usual
  actionable message, instead of being accepted by the resource manager and
  failing one at a time inside the backend.
- Reserved headroom exists so packet termination always fits. Filling a packet
  exactly to capacity leaves no room to close it.
- Transform batching is fixed. It is sized to the coprocessor's local memory, not
  tunable at runtime.

- **Far-field geometry is not implemented.** The level format carries a
  far-field description and a per-frame budget for it, but no backend on any
  platform draws it. Distant impostors simply do not appear.
- **Verified under emulation, not on hardware.** The frame loop, the display
  registers, the clear, the packet path, primitives, models, textures and world
  geometry have all been observed running, and the scene produced is equivalent
  to [ps2gl](PS2GL.md) rendering the same content. Nothing here has been run on a
  console, so anything an emulator forgives — transfer cache coherency, and
  behaviour at the scissor and guard-band edges — remains unproven.
- **The display's read circuit must be programmed explicitly.** Setting the video
  mode and handing the hardware a frame buffer is not enough; the visible area is
  a separate register, and without it the hardware scans out nothing whatever is
  in memory. The symptom is a black screen with a correctly paced frame loop and
  no error anywhere, which is indistinguishable from a renderer that draws
  nothing.
- **`RenderToImage3D` (`Ui_Image3D`) renders at a fixed size, not the caller's
  requested one.** A square colour-plus-depth surface is reserved once at
  construction, sized from `GFX_PS2_IMAGE_TARGET_SIZE`, the same way the frame
  and depth buffers are — not negotiated per call, so `GetTextureBudgetBytes`
  already excludes it rather than a heap allocation shrinking the reported
  ceiling out from under an already-running texture budget. The caller's
  width/height only shape the camera's aspect ratio; the result is stretched
  to fit whatever screen rect the interface asked for regardless, exactly as
  any other texture of a different size already would be. The draw itself
  reuses `DrawTriangles`/`DrawStrip`/`BindTexture` by pointing the packet they
  write to at a dedicated scratch packet instead of the frame's own — those
  three know nothing about which packet they are filling — then drains it with
  a blocking `dma_channel_wait`/`draw_wait_finish` before returning, unlike the
  frame's own double-buffered packets, which are left in flight.

## When to prefer it

It is the default because it avoids the microcode toolchain the alternative
depends on, and because packet-capacity limits scale with scene complexity more
predictably than draw-call limits. It draws the same scene as
[ps2gl](PS2GL.md) — primitives, models, the sky, the interface and streamed
world sectors, textured — so the choice between them is now about cost and
toolchain rather than about coverage. The far field is the one thing neither
draws.
