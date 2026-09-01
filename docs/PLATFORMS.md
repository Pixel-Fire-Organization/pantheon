# Documentation index

Start at [ENGINE.md](ENGINE.md) for the architecture, or go straight to what you
need below.

**Read the relevant spec before changing the thing it describes.** Specs carry
the reasoning that is deliberately not in the source — hardware quirks, race
conditions, renderer limits, budget ceilings.

## Building something new

Read the guideline **before** starting, not while reviewing.

| | |
|---|---|
| [guidelines/NEW_SYSTEM.md](guidelines/NEW_SYSTEM.md) | Adding an engine subsystem |
| [guidelines/NEW_PLATFORM.md](guidelines/NEW_PLATFORM.md) | Adding a platform |
| [guidelines/NEW_EXAMPLE.md](guidelines/NEW_EXAMPLE.md) | Adding a standalone example |
| [guidelines/SECURE_CODING.md](guidelines/SECURE_CODING.md) | Writing a reader, a callback, a teardown path, a platform file operation or a workflow |
| [guidelines/SECURITY_REVIEW.md](guidelines/SECURITY_REVIEW.md) | Checking the tree for that class of defect |
| [guidelines/PERFORMANCE.md](guidelines/PERFORMANCE.md) | Writing anything on the frame path, and what every platform spec states about performance |
| [guidelines/PERFORMANCE_REVIEW.md](guidelines/PERFORMANCE_REVIEW.md) | Checking the tree obeys it, and taking each platform's baseline |

## Engine

| | |
|---|---|
| [ENGINE.md](ENGINE.md) | Architecture, layers, startup order, constants rule |
| [APP_API.md](APP_API.md) | The surface game code uses |
| [COMMAND_LINE.md](COMMAND_LINE.md) | Launch options and the argument grammar |
| [PIPELINE.md](PIPELINE.md) | The five build stages, cook lists, validation |
| [ASSET_AUTHORING.md](ASSET_AUTHORING.md) | Adding content |
| [TESTBED.md](TESTBED.md) | The debug scenes, and how to reach them |
| [EXAMPLES.md](EXAMPLES.md) | Standalone per-capability example binaries |

## Subsystems

|                                          | Optional                  |
|------------------------------------------|---------------------------|
| [Memory](subsystems/MEMORY.md)           | No                        |
| [Debug](subsystems/DEBUG.md)             | Snapshot only             |
| [Renderer](subsystems/RENDERER.md)       | No — use the null backend |
| [IO](subsystems/IO.md)                   | Yes                       |
| [Archive](subsystems/ARCHIVE.md)         | Yes                       |
| [Resource](subsystems/RESOURCE.md)       | Yes                       |
| [Level](subsystems/LEVEL.md)             | Yes                       |
| [Sector](subsystems/SECTOR.md)           | Yes                       |
| [Input](subsystems/INPUT.md)             | No — force-enabled        |
| [Action](subsystems/ACTION.md)           | No — force-enabled        |
| [Scene](subsystems/SCENE.md)             | Yes                       |
| [UI](subsystems/UI.md)                   | Yes                       |
| [Achievement](subsystems/ACHIEVEMENT.md) | Yes                       |
| [Testbed](subsystems/TESTBED.md)         | Debug builds only         |

## Formats

On-disc layouts. Every layout below is identical on every platform; what varies
by platform is the *content* of a texture or theme payload, never its shape. The
last is a console format one platform is obliged to produce.

**Containers**

- [ASSET_FORMAT.md](formats/ASSET_FORMAT.md) — one cooked asset
- [ARCHIVE_FORMAT.md](formats/ARCHIVE_FORMAT.md) — the container, and the canonical key rule

**Payloads**

- [TIM2_TEXTURE.md](formats/TIM2_TEXTURE.md) — a cooked texture
- [FONT_FORMAT.md](formats/FONT_FORMAT.md) — a cooked font's metrics
- [THEME_FORMAT.md](formats/THEME_FORMAT.md) — a cooked interface theme
- [LEVEL_FORMAT.md](formats/LEVEL_FORMAT.md) — compiled worlds

**Records**

- [ACHIEVEMENT_RECORD.md](formats/ACHIEVEMENT_RECORD.md) — what a player has earned
- [ACTION_OVERLAY.md](formats/ACTION_OVERLAY.md) — a player's rebindings
- [TROPHY_PACK.md](formats/TROPHY_PACK.md) — Vita trophy container

## Platforms

| | PlayStation 2 | Win32 | PlayStation Vita | PlayStation Portable | Nintendo Switch |
|---|---|---|---|---|---|
| Spec | [ps2/PLATFORM.md](ps2/PLATFORM.md) | [win32/PLATFORM.md](win32/PLATFORM.md) | [vita/PLATFORM.md](vita/PLATFORM.md) | [psp/PLATFORM.md](psp/PLATFORM.md) | [nx/PLATFORM.md](nx/PLATFORM.md) |
| Building | [ps2/BUILD.md](ps2/BUILD.md) | [win32/BUILD.md](win32/BUILD.md) | [vita/BUILD.md](vita/BUILD.md) | [psp/BUILD.md](psp/BUILD.md) | [nx/BUILD.md](nx/BUILD.md) |
| Selectable as | `ps2pal`, `ps2ntsc` | `win32` | `vita`, `vitatv` | `psp` | `nx` |
| Default renderer | [giftag](ps2/renderers/GIFTAG.md) | [webgpu](win32/renderers/WEBGPU.md) | [gxm](vita/renderers/GXM.md) | [gu](psp/renderers/GU.md) | [deko3d](nx/renderers/DEKO3D.md) |
| Fallback | [ps2gl](ps2/renderers/PS2GL.md), then null | [opengl](win32/renderers/OPENGL.md), then null | [vitagl](vita/renderers/VITAGL.md), then null | [pspgl](psp/renderers/PSPGL.md), then null | [opengl](nx/renderers/OPENGL.md), then null |

PS2 extras: [MASP.md](ps2/MASP.md),
[TEXTURE_BUDGET.md](ps2/TEXTURE_BUDGET.md),
[PS2SDK_SETUP.md](ps2/PS2SDK_SETUP.md),
[ISO_GENERATION.md](ps2/ISO_GENERATION.md),
[PCSX2_DEBUGGING.md](ps2/PCSX2_DEBUGGING.md),
[PS2GL_FUNCTIONS.md](ps2/renderers/PS2GL_FUNCTIONS.md).

Vita extras: [PACKAGING.md](vita/PACKAGING.md) — title metadata, the store-front
images and their exact sizes, and trophies.

PSP extras: [PACKAGING.md](psp/PACKAGING.md) — the two distribution containers,
why both ship, and what each slot holds.

nx extras: [PACKAGING.md](nx/PACKAGING.md) — the single executable container, its
icon, and how the examples are packaged.

Editor setup: [CLION_SETUP.md](CLION_SETUP.md).

## Adding a platform

Full procedure: [guidelines/NEW_PLATFORM.md](guidelines/NEW_PLATFORM.md). In
short, it is adding **one directory** and one registration. No shared engine
file changes.

1. Create the platform directory holding its platform implementation, its
   concern files, its constants, its cook list, its entry point and its build
   rules.
2. Implement the platform contract and declare the implementation as the one
   built into the binary.
3. Register the name in the known-platform list, with its metadata.
4. Add a toolchain file if the platform needs one.

Then write its specs — a platform spec with its *Performance* section (the
nine items [guidelines/PERFORMANCE.md](guidelines/PERFORMANCE.md) lists, with
a dated baseline), a build document, and one spec per renderer it introduces —
and add them here. A platform whose quirks are undocumented is not finished,
because the next person to touch it will be reading source that deliberately
no longer explains itself; a platform whose performance is unmeasured is not
finished either, because nothing can then be said to have regressed.
