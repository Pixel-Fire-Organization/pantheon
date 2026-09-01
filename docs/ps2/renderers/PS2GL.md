# Renderer — ps2gl (PS2)

A backend built on ps2gl, a third-party library implementing a subset of an old
fixed-function graphics API on top of vector-unit microcode and hardware transfer
chains.

Contract and shared behaviour: [RENDERER.md](../../subsystems/RENDERER.md). The
subset of library entry points this engine relies on is catalogued in
[PS2GL_FUNCTIONS.md](PS2GL_FUNCTIONS.md).

**This is not the desktop OpenGL backend.** It shares a name family and nothing
else — no code, no base class, no shared assumptions. The desktop backend is the
real API at a modern version; this is a small emulated subset on unusual
hardware. Treating them as one versioned backend would be a category error, and
they are deliberately named and selected separately.

## Model

Geometry is submitted through the library, which compiles it into display lists
and drives the hardware through vector microcode. The engine does not write
display packets itself here. The limit is consequently **draw calls**, not packet
bytes.

## Budgets

| | |
|---|---|
| Shared packet | 65000 quadwords, across all render paths |
| Cost per draw call | About 82 quadwords with a state change |
| Draw call budget | 720, primitives and model meshes combined |
| Meshes per model | 8 |
| Cached models | 16 |

The draw-call budget is derived from the other two: the shared packet divided by
the per-call cost, with headroom. Exceeding it overruns a packet the engine does
not own.

## Quirks and limits

- **A textured draw's colour is on a different scale from an untextured
  one's, the same GS hardware quirk [GIFTAG](GIFTAG.md) documents.**
  Modulation treats 128, not 255, as unity, so a colour passed through GL's
  ordinary [0,1] convention (1.0 meaning "no change") overexposes a texture by
  roughly two-to-one, with detail lost above half intensity, whenever it
  reaches the GS unconverted. This backend's own colour calls — the interface's
  screen-space quads, level sectors, standalone primitives, models, and the
  skybox — scale by 128/255 in place of 1.0 for exactly this reason. The
  vendored library is aware of the same scale (`GetMaxColorValue` in
  `base_renderer.h`) but only applies it to lighting colours, not to a plain
  per-vertex or material colour, so any future draw call added here needs the
  same scale applied explicitly; the library will not do it.
- **Far-field geometry is not implemented**, as on every other backend. Level
  geometry and the sky both draw; the distant impostor ring does not.
- **Screen-space quads are queued, with a fixed per-frame ceiling.** Interface
  drawing is submitted before the frame begins, so it is buffered and emitted at the
  end. The queue is sized as the interface budget plus a fixed headroom for the
  game's own rectangles and the panic display, so raising the interface budget
  raises the queue with it rather than silently dropping quads inside the backend
  and blaming the backend. Beyond capacity they are dropped, and the overflow is
  reported **once per frame, with a count** — never once per dropped quad. A
  per-item diagnostic on this platform's console costs more than the frame it
  describes, so it would replace the problem it reports rather than measure it.
- **Blending works, but only three factor pairs exist.** The library implements
  exactly three combinations and rejects the rest; the one the interface needs —
  source alpha against its inverse — is among them. Anything else is a
  not-implemented failure inside the library rather than a compile error here, so
  a change to the blend equation must be checked against that list first.
- **There is no scissor.** The library's clipping region is part of the draw
  environment rather than an independent rectangle, so the entry point exists but
  does nothing useful. Screen-space clipping is therefore done before submission,
  which is the arrangement every backend uses anyway.
- **Screen-space texturing goes through the fixed-function modulate path**, which
  the library does implement. The neighbouring decal and blend texture modes do
  not, so the interface must stay on modulate.
- **Indexed drawing is unavailable.** The library treats it as a hard error, so
  indexed meshes are skipped entirely rather than drawn incorrectly. Content must
  be baked unindexed, which is what the cook stage produces.
- **The microcode toolchain is fragile.** Building the library requires
  assembling vector microcode, and the assembler shipped with the standard
  toolchain is known to fault on these sources. This blocks linking a full
  executable in some environments — see [BUILD.md](../BUILD.md).
- Model caching is bounded. Beyond the cached-model limit, display lists are
  rebuilt, which costs far more than drawing them.
- State changes dominate cost. The per-call budget assumes a state change per
  call; batching by material recovers a substantial fraction of it.
- The library is third-party and vendored. It is **not modified** by this
  project, so its limits are worked around rather than fixed.
- **`RenderToImage3D` (`Ui_Image3D`) is not implemented.** A correct,
  synchronous render-to-image needs proof that the GS has actually finished
  writing the scratch target before its handle is handed to the interface to
  sample the same frame; the library declares
  `pglFinishRenderingImmediateGeometry` for exactly this but never implements
  it. This backend always answers 0 (logged once) rather than guess at
  synchronisation it cannot verify. [GifTag](GIFTAG.md), the sibling PS2
  backend, implements the same capability the ordinary way a GS-generation
  backend can — reading a texture from the same memory a draw just wrote —
  and is the answer when a build needs this on PS2.

## When to prefer it

It is currently the only backend that draws streamed level geometry, so any
content with a world needs it until that gap closes in [giftag](GIFTAG.md). It is
otherwise the fallback, because it depends on a toolchain step the default
avoids.
