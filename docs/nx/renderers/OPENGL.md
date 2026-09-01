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
