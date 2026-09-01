# Renderer — deko3d (nx)

The default Nintendo Switch backend, driving the graphics processor through
deko3d, a thin explicit interface to it.

Contract and shared behaviour: [RENDERER.md](../../subsystems/RENDERER.md).

## What it targets

deko3d is an open, low-level graphics interface written against the console's
graphics processor. Its model is closest to a modern explicit interface: the
application owns every block of memory the processor reads, records command lists
into memory it supplied, submits them to a queue, and presents through a
swapchain. There is no driver-side state tracking and no run-time shader
compiler. It is the same relationship the direct backends on the other consoles
have to their machines — closest to the hardware, most work to write, and the one
worth optimising.

It ships with the homebrew toolchain as a library linked into the executable, so
the player needs nothing installed.

## Model

**Shaders are compiled at build time.** The shader source is shared with the
reference renderer and has no version line of its own; the build prefixes the one
this backend's offline compiler requires, compiles each stage to a binary, and
links the binaries into the executable. The compiler ships with the toolchain, so
unlike the Vita's it is always present. See [BUILD.md](../BUILD.md).

**Every byte the graphics processor reads is a mapped block this backend
allocated.** Command memory, shader code, the vertex buffers, the descriptor
sets, each texture and each render target is a block of memory mapped for the
graphics processor with the access it needs — code blocks for shaders, image
blocks for textures and targets, processor-writable blocks for anything written
every frame. None of it passes through the engine arenas, which describe
processor-side staging only. All of it is released at shutdown, in reverse.

**Textures are addressed through descriptor sets.** A bound texture is an image
slot and a sampler slot in two descriptor sets the backend owns. Uploading a
texture writes its image descriptor into its slot through the command stream;
the backend keeps one sampler per filter and a texture chooses between them at
upload.

**Display is a swapchain.** Three display buffers are attached to the system
window. Acquiring one blocks until one is free, which is the frame's present
wait; presenting one queues it for the vertical blank.

Geometry follows the model the other staged backends use: built on the
processor, copied once per frame into mapped memory, drawn as runs sharing a
texture. World and screen space stage independently.

## Quirks and limits

- **The screen-space pass blends; the world pass does not.** Blending and a
  disabled depth test are bound only for screen space, so world rendering is
  unaffected by the interface gaining transparency.
- **The device keeps its upper-left origin but takes OpenGL's depth range.**
  Normalised vertical still points up, so the shared projection helpers produce
  the same matrices here as for the reference renderer; depth runs from minus one
  to one for the same reason. With the origin at the upper left, the handheld
  frame's viewport starts at zero and lands in the top-left of the display
  buffer, which is exactly the region the display is cropped to — and the
  offscreen image's first row is its top, so unlike the reference renderer it
  needs no vertical flip. Choosing the lower-left origin instead would move the
  handheld viewport 360 rows up and flip the offscreen image; half of that change
  draws a clear-coloured screen.
- **Display buffers are allocated once at 1920 x 1080.** Handheld mode draws into
  the top-left 1280 x 720 and crops the display to it. Rebuilding the buffers on
  every dock would stall exactly when the console is being moved.
- **Vertex buffers are doubled and alternate every frame.** The frame in flight
  may still be reading the buffer the previous frame wrote, and this backend does
  not wait for it before building the next. Writing into one buffer every frame
  would present as intermittent geometry corruption that no measurement explains.
- **The vertex ceiling is fixed and enforced.** The buffers are mapped memory
  reserved once, sized to the platform ceiling. The ceiling is declared to the
  stager, which refuses whole entries that will not fit; the upload clamps as a
  last resort, world geometry yielding to the interface. How full the buffer is
  is reported every frame; the log line is emitted only when the shortfall
  changes.
- **A texture chooses its filter at upload and cannot change it afterwards.** It
  is bound to one of two samplers when its descriptor is written.
- **Uploading a texture is a transfer, not a copy.** The expanded pixels are
  written into a processor-visible staging block, and a command copies them into
  the texture's own block. The upload waits for the queue to go idle before
  releasing the staging block, so an upload mid-frame costs a pipeline drain.
- **Releasing a texture drains the pipeline first**, because the frame in flight
  may still be sampling it.
- **Each texture is its own mapped block**, so the smallest texture costs a page.
  The platform footprint accounting charges that rounded-up size.
- **Console pixel formats are expanded on upload**, as on every non-PS2 backend.
  Textures cooked palettised or at sixteen bits become plain 32-bit colour, the
  budget is charged the expanded size, and alpha is rescaled from the console
  convention where half-scale means fully opaque.
- **`RenderToImage3D` (`Ui_Image3D`) records and submits a separate command list,
  then waits for the queue to go idle** before returning the handle, so the image
  is finished when the interface samples it. The offscreen target is a colour
  image and a depth image registered in the texture descriptor set, created
  lazily and resized on demand. Its colour format matches the display buffers, so
  the ordinary shader draws into it unchanged.
- **Shutdown destroys the swapchain before anything else.** The reference
  renderer attaches to the same system window, and cannot while this one still
  holds it — which is what makes falling back to it work at all.
- **The sky is not implemented.** World sectors, models, primitives and interface
  elements draw.
- **Far-field geometry is drawn by no backend on any platform.**

## When to prefer it

It is the default, and for a title that ships it is the right choice: it links no
driver beyond itself, its memory is entirely accounted for, and it is the direct
path to a processor with far more headroom than the engine currently uses. Prefer
[opengl](OPENGL.md) only when comparing two independent implementations to decide
which one is wrong.
