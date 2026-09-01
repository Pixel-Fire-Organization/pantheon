# The debug testbed

A catalogue of scenes, each exercising one engine or platform capability, built
into every debug binary and absent from every release one. The contract behind it
is [subsystems/TESTBED.md](subsystems/TESTBED.md); the interface it draws through
is [subsystems/UI.md](subsystems/UI.md). This page is what to press and what each
scene shows.

**What "correct" looks like on a given platform is that platform's build
document**, not this page: [PS2](ps2/BUILD.md), [Win32](win32/BUILD.md),
[Vita](vita/BUILD.md), [PSP](psp/BUILD.md), [nx](nx/BUILD.md).

## Reaching it

The build boots into the game. A held chord opens the testbed and the same chord
closes it; each platform answers with buttons its own pad has, and the chord in
force is named in the startup log and along the bottom of the menu.

| | |
|---|---|
| Open or close | The debug-menu chord — currently Select + Start everywhere, Tab + Escape through the Win32 keyboard bridge |
| Move | D-pad, or point with the mouse, the front touchscreen, or the left stick |
| Choose | Cross |
| Back | Circle — from a scene to the menu, from the menu to the game |
| Quit | The exit row in the menu |

**Every transition resets the engine's runtime state**, including closing the
menu, which restarts the game from the beginning rather than resuming it. That is
deliberate: a scene measures itself rather than whatever ran before it.

## Input and devices

| Scene | Shows |
|---|---|
| **Keyboard mouse** | Held keys, cursor position, movement delta, wheel and buttons. Tapping a key faster than a frame should still register; moving focus away from the window should clear everything held. |
| **Touch** | Front and rear contacts in **separate** boxes, with contact id and pressure. The two are never merged: a rear pad sits behind the device and corresponds to nothing on screen. |
| **Pointer** | The cursor itself — which device currently owns it, its speed, and a trail. The left stick ramps from slow to fast while held; the d-pad hides the cursor. |
| **Debug chords** | All three chord intents, the buttons this platform answered with, and how many of each are currently held. Live button state sits beside it, since the chord that opens the testbed cannot be held while reading this. |

## Rendering and display

Primitive rendering, the UI widget set, live theming and the draw-list
ceiling moved out to standalone examples — see [EXAMPLES.md](EXAMPLES.md)
(`primitives`, `ui_gallery`) and [subsystems/TESTBED.md](subsystems/TESTBED.md)
for why. What's left here is display calibration, which is still a "look at
the device" diagnostic:

| Scene | Shows |
|---|---|
| **Screen and aspect** | The framebuffer edge, a title-safe inset, a centre cross, and a box that is square **on the display** rather than in the framebuffer. Framebuffer size is re-read every frame, so a resize shows up immediately. |
| **Depth range** | Markers from the near plane outwards, each twice as far as the last and scaled to stay the same apparent size. A step that vanishes is a depth problem, not a small object. |

## Systems and budgets

| Scene | Shows |
|---|---|
| **Platform info** | Name, renderer, framebuffer, display aspect, frame budget and every capability on one page; every memory, texture and draw budget on the other. |
| **Memory** | Arena, pool, heap and texture occupancy, plus a tool: pick Config, Level data or the Main pool as a target and force an allocation, a free (pool only) or a reset against it, reading the byte count before and after. Also how the runtime reset is checked: the bars should read the same on entry to every scene, and the renderer arena — which has no tool, since a live renderer owns it — should never be among the ones that empty. |
| **Resources** | A free-form loader — a path field and a type (AUTO included) — over a live, selectable view of the resource table: every resident slot with its type, state, reference count, pinned flag and key. Selecting a row inspects it (dimensions for a texture) and offers pin/unpin/unload. Loading the same path twice should cost the budget once and occupy one slot, not two. |
| **Asset browser** | What is actually mounted and what is inside it, down to the last field. Every mounted archive with the path it came from, its entry count and payload size; that archive's entries with their size, offset and key hash. Selecting an entry reads it out of the archive **once** and recognises it by its own magic — a cooked asset, a compiled level core, a streamed sector, or none of those — so nothing in a container is reported as an opaque blob just because it is not a `.ps2a`. Each kind then opens as collapsible sections, described below. |
| ↳ *cooked assets* | The container header (type, source extension, payload and header sizes, dependency count) and every declared dependency path, read without loading anything. Loading then adds what only a resident resource can answer — handle, reference count, pinned flag — and the type's own detail: a **texture**'s dimensions, VRAM cost, pixel format and backend id beside a flat image of it; a **model**'s baked table (version, per-mesh vertex count, material index, topology, bounds radius, whether it carries normals and UVs; per-material diffuse reference) shown *before* loading, then its resolved per-material texture handles and exact vertex total once loaded, spinning as an inline `Ui_Image3D` preview — the same live-render-as-image mechanism a font's atlas or a theme's swatches use for their own previews, just fed a model instead of a resource handle, and drawn as an ordinary row rather than a full-screen backdrop, which is what keeps this scene pure interface with nothing behind it (see `Renderer::RenderToImage3D` and `docs/subsystems/RENDERER.md`); a **font**'s glyph count, codepoint range, missing-glyph index, icon-cell count, atlas size, line height, baseline, space advance, monospaced flag, measured advance range and tallest cell, beside the atlas image itself; a **theme**'s every colour role as a named swatch and every metric it carries, decoded for inspection only — loading one here never touches the live interface. **Sound** offers no load at all and says it is unimplemented engine-wide rather than across this platform only. |
| ↳ *level cores and sectors* | A `.ps2l` core reports its name, version, chunk count and total size; its grid (cell counts, cell size, world origin, how many cells actually hold geometry, total and largest sector payload against the streaming slot ceiling); every material key it pins; its entities with classname, origin and property count; its far field (clusters, atlases, azimuths and which material each atlas is); and its raw chunk table by four-character code, offset and size. A `.SEC` sector reports its version, mesh count, vertex total, AABB, whether a BVH is present, what it costs against the slot ceiling it must fit, and its full mesh table. Both are read straight from the archive with nothing loaded, which is what makes them inspectable when the level itself will not load. |
| ↳ *anything else* | Reported as unrecognised, with a hex dump of its first bytes rather than a shrug — enough to tell a truncated file from one written by the wrong tool. |
| **Level stream** | Level load and unload, and the resident sector ring around a streaming centre that can be walked in a circle. Load and unload twice; a leak shows up in the memory scene. |
| **Achievements** | Whether the platform records them, how many are declared, and an unlock per id. On most platforms this reports unavailable, and that path is the common one. |

## Timing and pacing

| Scene | Shows |
|---|---|
| **Performance** | The frame split into logic, render and wait against the platform budget, beside the renderer's own counters. Anything a backend does not measure reads as not measured, never as zero. |
| **Frame pacing** | A plot of frame deltas against the budget line, a bar crossing the screen at a fixed real speed, and a square that flips once a second. Both are driven by elapsed time, so they should behave identically on a 50 Hz and a 60 Hz target. |

## Adding a scene

1. Write `engine/src/debug/scenes/Scene<Name>.cpp` with an init and an update, and a
   shutdown if it takes anything that must be given back.
2. Declare its entry points and add one row to the catalogue, with a category.
3. Add the source to the debug source list.

Two rules the scene has to keep, both from
[subsystems/TESTBED.md](subsystems/TESTBED.md):

- **Gate on capability, never on platform identity.** A scene for a device this
  platform lacks still appears and says so.
- **Fit the interface budget on the smallest screen.** Text is the expensive
  part. Scroll a dense scene rather than letting it overflow; an overflow report in
  the log is a defect in the scene, not a limit to live with.
