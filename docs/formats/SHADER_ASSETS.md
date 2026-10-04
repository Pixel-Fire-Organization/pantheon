# Format — Shader assets

The shaders the desktop renderers compile at start-up. They are **source text kept
as assets**, not code in the engine: a shader is edited, cooked and shipped without
rebuilding the engine, and one source serves every desktop platform that has the
renderer it is written for.

Which renderers read them is in [renderers/WEBGPU.md](../renderers/WEBGPU.md),
[win32/renderers/OPENGL.md](../win32/renderers/OPENGL.md) and
[macos/renderers/OPENGL.md](../macos/renderers/OPENGL.md). How a platform cooks them is
its cook list, [PIPELINE.md](../PIPELINE.md).

## Not an archive entry, and why

Shaders are **loose files in a directory beside the master archive**, not entries in
it, and they are not read through the resource manager. This is deliberate and
follows from the start-up order:

- A renderer is built **before** the IO, archive and resource subsystems exist.
- Those three are selectable by the game; the renderer is not. It is a
  precondition of the engine, like memory and the log.

A renderer that loaded its shaders through the resource manager would depend on a
subsystem a game may switch off, and would force every console to start its storage
before its renderer. Reading a file through the platform file API depends on
nothing that is not already there. It is also the engine's existing "assets resolve
as loose files" tier, applied to the one class of asset that must be readable first.

The consequence is a plain one: a bundle whose `shaders` directory is missing or
incomplete has no renderer that can draw. Each renderer reports the file it could not
open and the cook target that produces it, and the platform's fallback chain carries
on to the next renderer, ending at the null one.

## Source

`assets/engine/shaders/` — the engine-owned asset directory, which both the game's
cook and every example's cook already read, so every executable gets the same set.
There is no descriptor: the directory is the declaration, and a file's extension and
position name its dialect.

| Dialect | Files | Used by |
|---|---|---|
| `wgsl` | `flat.wgsl`, `pbr.wgsl`, `shadow.wgsl` — one module each | The WebGPU renderer, on every platform that has it |
| `glsl` | `flat`, `pbr`, `shadow` — each as `.vert.glsl` and `.frag.glsl` | Every desktop OpenGL renderer on a core-profile context |
| `glsl_legacy` | The same six files under `legacy/` | The Win32 OpenGL renderer's 2.1 path only |

The three programs are the same on every dialect: **flat** draws the interface and
the offscreen preview; **pbr** is the main scene pass; **shadow** is the depth-only
pass for the real-time shadow caster. See each renderer spec for what they do.

## What the source may contain

**WGSL has no version number**, so the equivalent of "which version" is a feature
baseline: the WebGPU 1.0 core language, nothing optional. The same text must
translate for Vulkan, Direct3D 12 and Metal, and an optional feature is a feature
one of them lacks. So a WGSL file declares **no `enable`, no `requires` and no
`diagnostic` directive** — which rules out half-precision floats, subgroups,
dual-source blending and clip distances, and anything later in the language that
needs a `requires`.

**GLSL carries no `#version` line.** The renderer prefixes the one it needs, and
prefixes the platform constants as definitions after it. There is one core version
for every desktop OpenGL renderer, `330 core`, so a file that compiles for one
compiles for the others. The legacy files are the exception and are prefixed
`120`. A GLSL file also uses no `#extension`, and is plain ASCII: some drivers refuse
any other byte, in a comment as much as in code.

**Platform constants arrive as text, never as literals.** The light count and the
shadow-map size are spliced from the platform's constants at start-up, so the shader
and the code that sizes its inputs cannot drift. In GLSL they are two definitions the
renderer prefixes. WGSL has no preprocessor, so the two tokens are replaced in the text:
`GFX_MAX_LIGHTS_PLACEHOLDER` by the light count, and `GFX_SHADOW_MAP_SIZE_PLACEHOLDER`
by the shadow-map edge as a floating-point literal. A replacement is always shorter
than its token; a shader that grew past that is refused rather than truncated.

**A file is at most 262144 bytes**, contains no NUL byte, and is UTF-8 without a
byte-order mark. The reader refuses anything else, and so does the cook, because the
ceiling is a constant both enforce.

## The cook

Cooking is the shader class of stage 3. A platform's cook list opts in with a `SHADER`
policy; a platform without one — every console — cooks and ships nothing.

| Field | Meaning |
|---|---|
| `enabled` | Whether this platform ships shaders |
| `dialects` | Which of `wgsl`, `glsl`, `glsl_legacy` its renderers load. All three when absent |

The cook takes every file of the dialects asked for, **refuses the whole set if any
file is missing or breaks a rule above**, normalises line endings to LF, and writes
the result to a `shaders` directory next to the cooked assets. It writes nothing when
it refuses, and removes anything left from an earlier cook that this one no longer
ships. The packaging gate re-checks the same rules against the cooked directory, so a
set that was edited by hand after the cook cannot reach a container.

## The edit loop

The shipped directory is plain text, and the renderers read it with the ordinary file
API, so a shader can be changed three ways, in increasing distance from the engine:

1. Edit the file inside a staged bundle and restart.
2. Edit the source and run the platform's cook target; no compile and no link.
3. Pass **`--shaders <directory>`** and the renderers read every shader from there
   instead — typically the source directory itself, so an edit needs neither a cook nor
   a staging step. The override replaces the directory wholesale: a file absent from it
   is a missing file, not a fall-back to the shipped one.

Nothing validates a shader read this way beyond the reader's own checks and the driver's
compiler, which is the point and the risk; the cook is what applies the portability rules.

## What a reader must establish

The reader sizes a buffer from a number in a file, so it checks the number first:

- The path fits its buffer, and names a file that opens.
- The size is at least one byte and at most the ceiling, **before** anything is
  allocated from it.
- The allocation, one byte larger than the file for the terminator, succeeded.
- The read returned every byte it asked for.
- No byte of the result is NUL, since a NUL ends the string the driver is handed.

Any failure leaves the result empty, logs the path, the size or the byte count and the
rule it broke, and reports failure to the renderer, which refuses to initialise.
Nothing is clamped.

## Known limitations

- A shader is read once, at renderer start-up. Changing one takes a restart.
- Nothing here checks that a WGSL file *runs* on Vulkan or Direct3D 12. The cook keeps
  the source inside the baseline those compile; the check that it does is the platform
  where it runs.
- There is no per-material shader. A material names a shader *type*, and the three programs
  above are what serves the one type that exists.
