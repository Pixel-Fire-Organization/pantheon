# Renderer — webgpu (macOS)

The default backend: the [shared WebGPU renderer](../../renderers/WEBGPU.md) presenting
through **Metal**. This document is what the platform adds to the shared behaviour.

Contract: [RENDERER.md](../../subsystems/RENDERER.md).

## What this platform supplies

The renderer is abstract and asks a platform for two things, a surface and its release. Here:

- **The surface is a child view of the window's content view whose backing layer is a Metal
  layer**, created when the renderer starts and removed when it shuts down, at one pixel per
  point. The renderer gives the graphics library that layer and nothing else; the library never
  touches the view.
- **One child view per renderer.** A renderer that fails to start removes its view, so the
  fallback renderer that follows finds the window as the platform left it. This is exercised: with
  the default's WGSL shader deleted from a bundle, the renderer reports the file and the fallback starts
  in the same window and draws the same frame as it does when chosen directly.
- The view resizes with the window, and the renderer reconfigures the surface when the
  framebuffer size it reads each frame changes.

Everything else — device and adapter selection, the surface format, pipelines, the frame — is the
shared behaviour. The adapter is requested with a preference for the high-performance one, which is meant
to select the discrete processor on a Mac that has two; which one was chosen is not logged.

## The library carries Metal and nothing else

The pinned prebuilt of the graphics library, for either architecture, contains the Metal backend
and **no OpenGL, no ANGLE and no Vulkan**. This was checked, not assumed: the library files were
scanned for each backend's symbols and library names, and Metal's are present while every other's
are absent, in both the Intel and the Apple Silicon build. Three consequences follow.

- **No backend selection is needed**, so none is made. There is only one to find.
- **The graphics library cannot supply an OpenGL fallback here.** On this platform its OpenGL path
  exists only through an external translation layer behind a build option the prebuilt does not
  enable, and it has no way to present through an OpenGL surface. The fallback is therefore a
  separate renderer written against Apple's own OpenGL — see [OPENGL.md](OPENGL.md).
- **Building the library from source would not change that**, and needs a Rust toolchain, so the
  pinned prebuilt is used. One archive is fetched per architecture, each checked against its own
  digest, and the two are merged into one universal library the executable links and the bundle
  ships.

The library's own minimum system is 10.13 for the Intel build and 11.0 for the Apple Silicon
one; the build's target of 11.0 is the stricter.

## Quirks and limits

- **Present modes are limited to what Metal offers**: first-in-first-out and immediate. The shared
  renderer asks for the first, which is vertical synchronisation, so the frame is paced by the display
  and a low-latency mode is not available.
- **A non-sRGB, blue-first surface is chosen**, for the reason the shared spec gives; the log names
  the format picked at start-up.
- **One pixel per point.** The layer's content scale is one and the drawable is the view's size in
  points, so on a Retina display the compositor scales the frame up. See
  [PLATFORM.md](../PLATFORM.md#graphics).
- **The layer is created on the main thread**, which is where the engine builds its renderer.
- The sky is not implemented, as on the other desktop platform.
- Console pixel formats are expanded on upload, as in the shared renderer.

## Verification

Run against the same cooked shaders and the same frame, this renderer and the
[OpenGL](OPENGL.md) one draw the **same interface**: on the ui_gallery example, outside one
animated plot, the two frames differ by zero pixels. The lit, shadowed level of the material_pbr
example runs on both with no validation error, no shader error and no incomplete framebuffer; its
frames cannot be compared pixel for pixel because its camera follows the clock and its sectors
stream in over the first moments.

## When to prefer it

It is the default, and there is rarely a reason to choose otherwise. Prefer [OpenGL](OPENGL.md)
when comparing two independent implementations to decide which one is wrong, or when the default
cannot start.
