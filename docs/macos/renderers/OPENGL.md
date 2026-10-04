# Renderer — opengl (macOS)

The fallback backend: **Apple's own OpenGL**, a 4.1 core-profile context. A separate
implementation, written for this platform.

Contract and shared behaviour: [RENDERER.md](../../subsystems/RENDERER.md).

**This is not the Windows backend of the same name, nor the Switch one.** It draws the same
geometry in the same way, from the same shader sources, but it is a different file against a
different API surface, and shares no code with either. See
[the Windows backend](../../win32/renderers/OPENGL.md) and
[the Switch backend](../../nx/renderers/OPENGL.md). **Nor is it related to the default renderer**:
that one cannot reach OpenGL on this platform, which is why this one exists. See
[WEBGPU.md](WEBGPU.md).

## What Apple's OpenGL is

- **Deprecated since macOS 10.14, and frozen at 4.1.** Core profile 4.1 with GLSL 4.10 is the
  highest it grants; nothing newer will arrive. Apple's macOS 26 release notes say it remains in the
  SDK. **Whether it ships in the release after that is not confirmed.** This backend exists only as long as it does, and the platform
  treats it as a fallback that may be removed under it.
- **A core profile is forward-compatible only**: no compatibility profile, no fixed function, and a
  vertex array object is required to draw. There is no debug context, so no debug message callback.
- **On Apple Silicon it is a translation layer over Metal.** There, this is not an independent path
  from the default; on Intel it is the real driver.
- **It is single-threaded in effect**: every call is made from the main thread, which is where the
  engine builds and draws.
- **Every call is deprecated**, and the build treats warnings as errors, so the one source file that
  includes the framework headers defines the system's silence-deprecation switch.

## Only the core profile

The Windows backend has two paths, a 3.3 core profile and a 2.1 legacy one, because it must run on old
drivers, virtual machines and software rasterisers. **This one has only the core path.** The build
targets macOS 11, and every Mac that can run it grants a 4.1 core context, so a legacy path would be code
that never runs. The consequences: no `--gl-version`, no second dialect of every shader, and no
legacy shader files in the macOS bundle.

## Model

Geometry is staged into the representation the [WebGPU](../../renderers/WEBGPU.md) backend uses, so
the two produce identical frames, three-dimensional and screen-space geometry occupying separate spans
of one buffer and drawn as runs sharing a material.

Entry points are the system framework's own, linked directly through its core-profile header; there
is no loader. That header and the legacy one cannot be used together, which is why a core-only backend
can use the simple route.

**Context.** A child view of the window's content view is an OpenGL view with a pixel format asking for
the 4.1 core profile, accelerated, double-buffered, 24-bit colour with 8-bit alpha and a 24-bit depth
buffer. It does not ask for the best-resolution surface, so the drawable is one pixel per point; see
[PLATFORM.md](../PLATFORM.md#graphics). The view is removed, and its context cleared, when the renderer
shuts down, so a failed start leaves nothing for the next renderer.

**Programs.** Three exist, one per shader source set: a flat/unlit one for the screen-space pass and the
preview target, a PBR one for the main scene, and a depth-only one for the shadow pass. Their sources are
the same cooked files as the Windows backend's core path, prefixed with the one shared version line, so
**a difference between the two backends' core paths is a driver difference**. The contract is in
[formats/SHADER_ASSETS.md](../../formats/SHADER_ASSETS.md).

## Materials and lighting

As the shared design: a metallic-roughness model sampling a material's three maps, a fixed array of
dynamic lights, one real-time shadow caster from a directional light that covers dynamic geometry only,
and baked and dynamic lighting composed by addition. See
[the WebGPU renderer's](../../renderers/WEBGPU.md) section, which this follows exactly, and the
[Windows backend's](../../win32/renderers/OPENGL.md) for the comparison-sampler shadow map.
`SupportsPbrShading()` is true.

The shadow map is a depth texture in its own framebuffer, sampled with hardware comparison.
`RenderToImage3D` renders into a colour texture and depth buffer in one reused framebuffer, resized on
demand; **its clip-space Y is negated** so every sampler of the returned handle agrees which row is the
top, and the call finishes the work before returning, since the interface samples the result the same frame.

## Pacing

The context is asked for a swap interval of one, both through the view's own parameter and the lower
layer's, and the value is read back. **That was measured not to be enough.** On a laptop with two graphics
processors the context landed on the one that does not drive the built-in display, the read-back said one,
and the frame ran unpaced at a median of 223 frames a second, then 381.

So the swap also **sleeps out whatever remains of one display refresh** since the previous swap. The
refresh comes from the window's screen when the system can say (macOS 12 and later) and is 60 otherwise;
when the driver has already paced the frame there is nothing left to sleep and the cap does nothing. With it,
the same scene measures a median of 59.6 frames a second, matching the default renderer's 60.5. The method
and its alternatives are in [PLATFORM.md](../PLATFORM.md#components-considered).

## Quirks and limits

- **The screen-space pass blends; the world pass does not**, and depth testing is off for the former, as in
  every other backend. Screen-space work is drawn as one call per texture run.
- **A texture chooses its filter at upload** and cannot change it afterwards; nearest exists for the pixel
  font atlas.
- **Console pixel formats are expanded on upload**, to plain 32-bit colour with the alpha rescaled from the
  console convention, and the platform budgets against the expanded size.
- Textures are never mipmapped: only level zero is uploaded, and a mipmapped filter with no chain samples as
  incomplete.
- **The sky is not implemented**, matching the other desktop backend.
- **Far-field geometry is drawn by no backend on any platform.**
- The first line of the log names the driver's version and renderer string. On the development machine it
  reads `4.1 ATI-7.0.25` and `AMD Radeon Pro 555X OpenGL Engine`: the fallback ran on the discrete processor.

## When to prefer it

When the default cannot start, which on a Mac that can run the build is unusual, and when comparing two
independent implementations to locate a rendering bug. Two backends drawing the same frame differently is a
reliable signal that one is wrong; the interface frames of the two match to the pixel.
