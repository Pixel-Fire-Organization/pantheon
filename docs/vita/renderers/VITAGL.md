# Renderer — vitagl (Vita)

The fallback Vita backend: a fixed-function drawing model layered over the same
console graphics API the default backend drives directly.

Contract and shared behaviour: [RENDERER.md](../../subsystems/RENDERER.md).

## What it targets

A vendored library that translates a subset of an old, fixed-function graphics
interface into calls on the console native API. It plays the same role here that
the subset library plays on the PlayStation 2: a simpler drawing model, brought
up first, kept afterwards as a known-good reference.

It is **unrelated to the desktop backend of a similar name**, which is the real
graphics API at a modern version. They share no code, and no platform hosts both.

## Model

**The library owns graphics and display initialisation.** It brings up the
graphics API and the display queue itself. The platform must therefore not touch
either, and the two Vita backends cannot be alive at the same time — selecting a
renderer is a startup decision, not something to toggle mid-session.

Geometry is staged by the shared processor-side stager every non-console backend
uses, then handed over as client-memory vertex arrays and drawn as runs sharing
one texture. Because the staging is shared, this backend and the default one
receive **identical vertices** for the same frame: a visible difference between
them is a bug in one of the two, not a difference in what was submitted.

Untextured runs are drawn against a one-texel white texture rather than by
toggling texturing per run, which keeps every run on one code path — the
fixed-function combiner multiplies by white, which is the identity.

The pipeline keeps view and projection in separate matrix stacks, but the staged
geometry is already in world space and carries one combined matrix, so it is
loaded into the projection stack against an identity model-view rather than being
split back apart.

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
**It tells the library when a system dialog is open.** The library composites the
dialog itself, but only when the frame is presented with that fact declared;
presenting without it draws the title correctly and leaves the dialog invisible
though open. See [../PLATFORM.md](../PLATFORM.md).

- **Its boot splash is compiled out.** The library shows an animated logo from
  initialisation until the first frame is drawn. Left on, an engine that starts
  the renderer and then fails to draw looks like a working application showing
  someone else's branding, which is exactly how a real fault was misread once.
  With it off, a stalled frame loop shows as a blank screen.
- **It cannot be shut down.** The library exposes no teardown entry point at all.
  Textures can be released; the graphics API and the display queue it brought up
  stay owned until the process exits. Two consequences worth knowing: the
  backend's `Shutdown` is partial by necessity, and if construction fails after
  initialisation succeeded, the display stays held by a library nothing is
  driving — the engine falls back to the null backend and the screen simply stops
  updating. That is visible and diagnosable, which is the best available outcome,
  but it is not a clean recovery.
- **It requires the shader compiler the player must extract.** This is the
  single most important thing to know about this backend, and it is not what the
  library's file layout suggests. The fixed-function shaders it ships are **Cg
  source text**, not compiled binaries, and the fixed-function path compiles a
  variant at run time for each combination of lighting, texture and clipping
  state it encounters. Only the clear and blit shaders are genuinely
  precompiled.

  So a player needs `libshacccg.suprx` extracted and decrypted on their console.
  Without it, shader compilation fails and nothing draws. The library's own
  documentation states this requirement plainly; the presence of a `shaders/`
  directory full of headers is what makes it easy to conclude otherwise.

  There is a shader cache that persists compiled variants to the writable data
  location, but it does not remove the requirement — the first encounter with
  each variant still needs the compiler.
- **The vendored revision is pinned, and deliberately not master.** The library's
  master calls into a newer version of the shader-compiler wrapper than the SDK
  packages, and does not compile against the installed one. The pin is the
  revision the SDK's own package set is built from. Moving it means checking that
  pairing again.
- **It needs SDK packages the SDK bootstrap does not install** — the shader
  compiler wrapper, its extension library, the plugin loader stub, and a NEON
  maths library. See [BUILD.md](../BUILD.md).
- **It allocates on its own terms.** Its pools do not pass through the platform
  memory contract, so they sit outside the engine budget and are invisible to the
  memory snapshot. The budget therefore under-reports real usage while this
  backend is active.
- **Its surface is a subset**, and an unsupported call is not always a loud
  failure. A drawing feature that silently does nothing here may work on the
  default backend, which makes "compare the two frames" the primary diagnostic.
- **Depth is a zero-to-one range**, not the minus-one-to-one of desktop OpenGL,
  so the projection is built for that convention. Building it the desktop way
  collapses the depth buffer and everything sorts wrongly.
- **Pixels are not square** — the display aspect applies here exactly as it does
  to the default backend.
- **The sky is not implemented.** World sectors, models, primitives and interface
  elements all draw.
- **Far-field geometry is drawn by no backend on any platform.**
- **Console pixel formats are expanded on upload**, through the shared expansion
  path, and alpha is rescaled from the console convention where half-scale means
  fully opaque.
- **`RenderToImage3D` (`Ui_Image3D`) is implemented, unlike on some other
  backends without genuine framebuffer-object support.** The library exposes a
  real `glGenFramebuffers`/`glFramebufferTexture2D`/`glRenderbufferStorage`
  surface, so the offscreen target is an ordinary FBO — a colour texture plus a
  depth renderbuffer — lazily created, reused and resized on demand, drawn into
  with the same fixed-function pipeline as the main pass. `glFinish()` forces
  completion before the handle is returned, since commands otherwise queue the
  same as on the desktop OpenGL backend. A draw into a state combination not
  already seen this run can trigger the same first-use shader compile stall
  the fixed-function path always risks — see the shader-compiler requirement
  above.

## When to prefer it

As a reference implementation when a frame looks wrong and it is unclear which
backend is at fault, and during bring-up of changes to shared geometry staging,
where a simpler drawing model makes the mistake obvious. It is also the automatic
fallback when the default backend fails to construct.

Prefer [gxm](GXM.md) otherwise: it is faster, its memory is accounted for, and it
can be shut down.
