# Renderer — opengl (nx)

The reference Nintendo Switch backend: desktop OpenGL, provided by a port of the
open-source Mesa driver for this console's graphics processor.

Contract and shared behaviour: [RENDERER.md](../../subsystems/RENDERER.md).

This is **not** the desktop backend of the same name. Both speak the same
interface and select with the same `--renderer opengl`, and they share no code:
this one attaches through EGL to the console's system window, the desktop one
through the desktop windowing system. A bug in one says nothing about the other.

## What it targets

A core-profile OpenGL 4.3 context from the Mesa port the homebrew toolchain
distributes, which translates the calls into commands for the same graphics
processor the default renderer drives directly. Its purpose here is the one the
fixed-function libraries serve on the other consoles: a second, independent
implementation to compare frames against.

## Model

**The shader source is the default renderer's.** The build prefixes a core
4.3 version line to the same source the default renderer compiles offline, and
embeds the text; the driver compiles it when this backend is constructed. That is
what makes a frame difference between the two a backend bug rather than a shader
difference.

**One vertex buffer, orphaned every frame.** World geometry and then screen
space are copied into it as two contiguous spans and drawn as runs sharing a
texture. Orphaning lets the driver hand back fresh storage rather than wait for
the frame in flight.

**Display is a buffer swap** at one swap per vertical blank. The swap is where
this backend blocks, and it is timed as the frame's present wait.

Three programs exist: the flat/unlit one above for the screen-space pass and
`RenderToImage3D`'s preview target, a PBR one for the main scene pass, and a
depth-only one for the real-time shadow pass — the PBR and shadow shader
sources are likewise shared with [deko3d](DEKO3D.md), just embedded as text
and compiled at runtime here instead of offline. See "Materials & lighting"
below.

## Materials & lighting

- **PBR tier.** `SupportsPbrShading()` is `true`. The main pass shades with a
  metallic-roughness Cook-Torrance BRDF (GGX distribution, Smith geometry,
  Schlick Fresnel), sampling all three of a material's maps where present and
  falling back to a flat tangent-space normal and a neutral
  (occlusion=1/roughness=1/metallic=0) ORM where a slot is empty. Normal
  mapping reconstructs tangent space from screen-space derivatives
  (`dFdx`/`dFdy`).
- **No `glGetUniformLocation`/`glUniformBlockBinding` calls, on either
  program.** Every uniform block and sampler declares its own
  `layout(binding=N)` in the shared shader source (core since GL 4.2), the
  same convention [deko3d](DEKO3D.md)'s NVN shaders are compiled against —
  both backends bind by the same fixed numbers because they compile the same
  text, rather than each backend looking a name up independently.
- **A per-run material uniform, one 256-byte-strided slot per draw run in one
  buffer**, written with `glBufferSubData` and bound with
  `glBindBufferRange` at that run's offset — the same dynamic-offset shape
  [WebGpu](../../win32/renderers/WEBGPU.md)'s material buffer uses, at the
  same conservative 256-byte stride every desktop/Vulkan-class driver
  guarantees for it.
- **The PBR pass repoints uniform-buffer binding point 0 away from the flat
  program's own buffer**, since both the flat and PBR vertex shaders declare
  their transform block at binding 0; the screen-space pass explicitly rebinds
  it back before drawing, rather than assuming binding-point state survives a
  program switch untouched.
- **Baked and dynamic lighting compose by addition, not replacement** — the
  same formula and reasoning as
  [WEBGPU.md](../../win32/renderers/WEBGPU.md)'s Materials & lighting
  section.
- **One real-time shadow caster, dynamic geometry only**, `GFX_SHADOW_MAP_SIZE`
  square. As on [deko3d](DEKO3D.md), depth is written into an ordinary RGBA8
  colour texture's R channel — cleared to **white** (far, 1.0), not the
  scene's background colour — rather than sampled from a real depth texture,
  since a depth-sampler path is not confirmed to exist identically under both
  this driver's GLSL compiler and deko3d's offline NVN one; the main shader
  manually 3×3 PCF-compares against it as a plain `sampler2D`. The shadow
  pass renders only the dynamic span of this frame's staged geometry from a
  camera-centred orthographic frustum, into a dedicated FBO (RGBA8 colour +
  a depth renderbuffer for the shadow pass's own depth test, never sampled)
  created once at startup. No alpha-mask cutout support in the shadow pass.
  Skipped outright, leaving the map's stale contents unsampled, when no
  caster is designated or nothing dynamic is on screen.

## Quirks and limits

- **The screen-space pass blends; the world pass does not.** Blending and a
  disabled depth test are enabled only for screen space.
- **The surface is created at 1920 x 1080 and cropped in handheld mode.** The
  frame is drawn into the top-left 1280 x 720, which under OpenGL's lower-left
  origin is a viewport starting 360 rows up. A viewport at zero draws into rows
  the display is told not to show.
- **The driver is large.** Linking it adds tens of megabytes to the executable and
  its allocations come out of the same memory as everything else, which matters
  when the title is launched as an applet. See [../PLATFORM.md](../PLATFORM.md#memory).
- **It cannot attach while the default renderer holds the display.** The two share
  the system window. Falling back to this backend works only because the default
  renderer releases its display buffers when it fails; a context that cannot
  create its window surface is the symptom when that release is incomplete.
- **Vertex upload and buffer fill are not measured.** The driver copies the data
  asynchronously, so a timing around the upload call measures the call rather
  than the copy. Both read as absent in the snapshot.
- **A texture chooses its filter at upload and cannot change it afterwards.**
- **Console pixel formats are expanded on upload**, with the budget charged the
  expanded size and alpha rescaled from the console convention.
- **`RenderToImage3D` draws into a framebuffer object and finishes the pipeline
  before returning.** Clip-space vertical is negated for that draw, because a
  framebuffer object's first row is its bottom while an uploaded texture's first
  row is its top; without it the image is upside down only where it is rendered.
- **The sky is not implemented.**
- **Far-field geometry is drawn by no backend on any platform.**

## When to prefer it

Only to decide which of two renderers is wrong. It is slower to start, heavier in
memory and one more layer from the hardware than [deko3d](DEKO3D.md), which is
what ships.
