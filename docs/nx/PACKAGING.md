# Nintendo Switch packaging (`nx`)

One container, `dist/nx/game.nro`: the homebrew executable format, carrying the
title's metadata, its icon and a read-only file system holding the resource
archive. It is the whole distribution — nothing ships beside it.

Three files describe the work and none of them is the build fragment:

| | |
|---|---|
| Declaration | `game/config/platform/nx/package.json` |
| Schema | `tools/schemas/package.schema.json` |
| Reader | `tools/nx_package.py` |

## Why the config lives under `game/`, and the schema does not

The convention the Vita established and the PlayStation Portable follows. A cook
list answers a **hardware** question and lives in `engine/config/nx/`. An icon
and a container layout answer a question about the **game**, and live in
`game/config/platform/nx/`. The schema is the cook system's own contract, so it
lives with the other schemas.

**Identity is not restated here.** The display name, author and version come from
`game/config/title.json`, which every platform shares, and are folded in when the
declaration is read. This platform needs **no identifier** in that declaration:
a homebrew executable has no title identifier to be filed under, and the
writable directory is named after the display name instead. The schema therefore
requires a title identifier only of the platforms whose containers carry one.

## Why the toolchain's own packaging function is not used

devkitPro's CMake support ships a function that turns an executable target into
this container. It is not used, for two reasons:

> It attaches the container to the default build target, so every build of the
> engine library would also try to package — before `tools/validate_cooked.py`
> has passed, which is the gate every container in this project sits behind.
> And when no metadata is supplied it generates its own from the CMake project's
> name, which is this repository's name rather than the title's.

So the fragment calls the metadata tool and the container tool directly, in a
target that depends on validation, exactly as the Vita and PlayStation Portable
fragments call their own tools directly.

## `package.json`

| Field | Meaning |
|---|---|
| `platform` | `nx` |
| `title.name` | *Declared centrally*, overridable here — the name the launcher shows |
| `title.version` | *Declared centrally* |
| `nro.icon` | 256 x 256 JPEG the launcher shows beside the name |
| `files` | Extra content staged into the container's file system |

The author the launcher shows is the declaration's developer.

The icon's size and format are checked before packaging. The container tool
accepts any JPEG and the launcher draws whatever it is given, so a wrong size
reads as an art problem on the console rather than as the packaging error it is.
A PNG is refused outright: the tool embeds the bytes unexamined, and the launcher
then shows no icon at all.

## Package layout

```
game.nro
  executable        the linked program
  metadata          name, author, version
  icon              256 x 256 JPEG
  file system
    RASSETS.PS2R    the master archive - every rasset and every compiled world
```

On an SD card, the file goes anywhere under `switch/`. At run time the title
writes its log and any saved data to its own directory there, named after the
display name.

## Build order

1. **Validate** the cooked tree against the cook list that produced it. Nothing
   below runs if this fails.
2. **Read and validate** this declaration, folding in the shared title identity,
   and emit the build variables from it.
3. **Stage** the container's file system: the master archive, then every
   `files[]` entry.
4. **Generate** the metadata from the declared name, author and version.
5. **Pack** the executable, the metadata, the icon and the staged file system
   into the container.

## Examples

Every example under `examples/` gets its own container,
`examples/dist/<name>/nx/<name>.nro`, built the same way from that example's own
title declaration and cooked assets, with the game's icon. It is the only way an
example can run on this platform at all: the executable reads its assets from its
own file system, so a bare executable beside a loose archive has nothing to read.

The examples share the engine library, and with it the directory their log is
written to, which is named from the game's title declaration rather than the
example's. Run one example at a time when reading that log.

## Adding a file to the package

Add a `{src, dst}` entry to `files[]` in the declaration. It is staged into the
container's file system beside the archive. Never add it to the build fragment —
what ships is described in one place.
