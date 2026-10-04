# Renderer — webgpu (desktop)

The default desktop backend, built on a native implementation of the WebGPU API.
It is **one implementation shared by every desktop platform**, [Win32](../win32/PLATFORM.md)
and [macOS](../macos/PLATFORM.md) at present. This document is the behaviour they share;
what a platform adds — which graphics API sits underneath, how a surface is made from its
window — is in its own renderer spec: [macOS](../macos/renderers/WEBGPU.md).

Contract and shared behaviour: [RENDERER.md](../subsystems/RENDERER.md).

## One renderer, one platform hook

The renderer is an abstract class. Everything above the window — the device, the
pipelines, staging, the material and shadow passes, texture upload, the frame — is written
once. What differs per platform is how a presentable surface is made from that platform's
window, and who owns whatever the platform created to host it, so each platform supplies a
small derived class with two operations:

| Operation | Contract |
|---|---|
| Create the surface | Given the instance, return a surface over the platform's window, or nothing. Called once, before the adapter is requested, so the adapter is chosen for compatibility with it |
| Release the surface | Release the surface and anything the platform made to host it. Called when the renderer shuts down |

A base class cannot call a derived override from its own constructor, so the base
constructor only prepares its members; the **derived constructor finishes by asking the
base to bring the device up**, and the renderer then reports initialised or not exactly as
it always has. A new platform adds one derived class and two source files, and edits
nothing here.

The shared sources are not part of the engine's common source list, because they include
the WebGPU header. Each platform's own build fragment lists them.

## Shaders

The three modules — flat, PBR and depth-only — are **cooked assets** read at start-up, not
text in this renderer. Their contract, the portability baseline that lets one source serve
Vulkan, Direct3D 12 and Metal, and the way to edit one without rebuilding are in
[formats/SHADER_ASSETS.md](../formats/SHADER_ASSETS.md). A missing file fails
construction, naming the file and the cook target, and the platform falls back to its next
renderer.

## Model

Geometry is staged on the processor into a representation shared with the
[OpenGL](../win32/renderers/OPENGL.md) backend, then uploaded once per frame and drawn as runs of
vertices sharing a material. Three-dimensional and screen-space geometry stage
independently and occupy separate spans of one buffer, so a single upload serves
both.

Two shader modules cover the contract. A flat/unlit one serves the screen-space
pass and `RenderToImage3D`'s preview target, exactly as before materials existed.
A second, PBR one serves the main scene pass; its pipeline binds three textures
per run (albedo, normal, packed occlusion/roughness/metallic) instead of one,
plus a per-run material-factor uniform selected by a dynamic buffer offset
rather than a fresh bind group per draw call. A third, depth-only shader
serves the real-time shadow pass. See "Materials & lighting" below.

## Materials & lighting

- **PBR tier.** `SupportsPbrShading()` is `true`. The main pass shades with a
  metallic-roughness Cook-Torrance BRDF (GGX distribution, Smith geometry,
  Schlick Fresnel), sampling all three of a material's maps where present and
  falling back to a flat tangent-space normal and a neutral
  (occlusion=1/roughness=1/metallic=0) ORM where a slot is empty. Normal
  mapping reconstructs tangent space from screen-space derivatives (`dpdx`/
  `dpdy`) rather than a vertex tangent attribute, since the shared vertex
  format carries none.
- **Baked and dynamic lighting compose by addition, not replacement.** A
  vertex's incoming colour — the level compiler's baked result for sector
  geometry, or flat white for a dynamic model — is added to the dynamic
  ambient term and multiplied by albedo once; it is never folded into albedo
  itself, which would let it double up wherever albedo is reused (Fresnel F0,
  the diffuse BRDF term). Up to `GFX_MAX_LIGHTS` dynamic lights (directional or
  point, point attenuating quadratically to zero at its `range`) are summed on
  top with the full BRDF, every frame, for every kind of 3D geometry alike —
  there is no per-geometry-kind shading branch.
- **One real-time shadow caster, dynamic geometry only.** `SetShadowCasterLight`
  names one directional light slot (point lights are refused — there is no
  frustum to build from a point light's "direction"); that light's contribution
  is attenuated by a `GFX_SHADOW_MAP_SIZE`-square depth map, 3×3 PCF-sampled.
  The shadow pass renders only the dynamic (model/primitive) span of this
  frame's staged geometry — everything after `StagedGeometry::DynamicRunStart`
  — from an orthographic frustum centred on the active camera; static sector
  geometry is excluded because it already carries baked, shadow-aware lighting
  from the compiler and would cost a pass for no visual benefit. The shadow
  pass does not support alpha-mask cutout: every dynamic mesh, regardless of
  material flags, casts a solid silhouette. If no light is designated, or
  nothing dynamic is on screen this frame, the pass is skipped outright and the
  map's stale contents are never sampled — the main shader checks the caster
  index before ever touching the shadow texture.
- **A material-group cache, not a bind group per draw call.** The (albedo,
  normal, orm) triple a run resolves to is looked up in a small fixed-size
  cache and only creates a new `WGPUBindGroup` the first time that exact
  combination is seen; releasing a texture purges every cached group naming
  it, since a destroyed `WGPUTextureView` invalidates the bind group holding
  it regardless of the group's own reference. This is what keeps the frame
  path free of GPU-object allocation despite materials varying per run.
- **`RenderToImage3D`'s preview target deliberately stays on the flat shader.**
  A UI thumbnail's lighting must not depend on — or go dark relative to — the
  scene's own dynamic lights or shadow caster, so it never uses the PBR
  pipeline.

## Quirks and limits

- **The screen-space pipeline blends; the world pipelines do not.** Only the
  screen-space pipeline carries a blend state, so world rendering is byte for
  byte what it was before the interface gained transparency. That pipeline also
  never writes or tests depth, so quads draw in submission order.
- **Screen-space work is drawn as one call per texture run.** Consecutive quads
  sharing a texture coalesce, so a solid interface is a single call and a
  glyph-atlas interface is a small number. A run count ceiling exists; exceeding
  it drops the excess and reports once per frame.
- **A texture chooses its filter at upload and cannot change it afterwards.**
  Two samplers exist and a texture binds one of them at creation. Nearest exists
  for content magnified to whole-pixel scales — a pixel font atlas is the case
  that needs it, and linear filtering visibly blurs it.
- **A non-sRGB surface format is chosen deliberately.** The API will happily
  offer an sRGB surface, and taking it applies a colour transform to everything
  the engine draws, making every frame visibly lighter than the same content on
  the console. Since matching the console is the point of having two backends,
  the non-sRGB format is preferred explicitly. This is not a workaround for a bug
  — it is a colour-space decision, and reversing it silently changes every
  frame.
- **The sky is not implemented**, matching the other desktop backend; the
  console backends do draw it. World sectors, models, primitives and interface
  elements all draw.
- **Far-field geometry is drawn by no backend on any platform.**
- The staged geometry representation is shared with the other desktop backend by
  design, so the two produce identical frames. Divergence between them means one
  of the two upload paths is wrong, not that the geometry differs.
- Screen-space work may be submitted **before the frame formally begins**, so
  staging must not be reset wholesale at frame start. Doing so discards
  already-submitted interface content and presents as a blank overlay with a
  correct world.
- The library is a pinned prebuilt binary, verified by checksum and fetched at
  configure time. It is not vendored source, and its version is fixed by the
  build rather than discovered. Each platform pins the archive built for it: the
  graphics APIs a prebuilt carries differ per target, so what this renderer can
  reach is a property of the platform's archive, recorded in its own spec.

- **Console pixel formats are expanded on upload.** Textures may be cooked in
  formats that suit the console's video memory — 16-bit colour, or palettised
  with a colour table. A desktop graphics processor has no reason to carry those
  storage modes, so each is expanded to plain 32-bit colour as it is uploaded,
  and the platform budgets against that expanded size rather than the cooked
  size. Alpha is also rescaled from the console convention, where half-scale
  means fully opaque; without that, every texture would look half-transparent once
  blending is enabled.
- **`RenderToImage3D` (`Ui_Image3D`) needs a second pipeline variant when the
  surface format is not RGBA8.** The offscreen target is created RGBA8,
  matching `UploadTexture`'s own format so it samples with the ordinary
  shader and bind-group layout unchanged; the swapchain surface this platform
  actually presents through is usually BGRA8, and a `WGPURenderPipeline`
  bakes its colour-target format in, so a pipeline built for the swapchain
  cannot draw into an RGBA8 target. A second pipeline sharing every other
  piece of state is built once at startup only when the two formats genuinely
  differ. Unlike the OpenGL backend, **no Y-flip is needed**: a render-pass
  colour attachment and an uploaded texture agree on which row is the top
  here, so the same UV convention reads correctly either way. The result is
  sampleable as soon as it is submitted, not merely queued — later
  submissions on the same queue, including the interface draw reading it,
  are already ordered after it.

## When to prefer it

It is the default: the most direct path to the hardware on a modern desktop, with
explicit resource management that matches how the engine already thinks about
memory. Prefer [OpenGL](../win32/renderers/OPENGL.md) where drivers are old, where the target is a
virtual machine or a remote session, or when comparing two independent
implementations to decide which one is wrong.
