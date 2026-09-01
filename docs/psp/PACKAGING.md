# PSP Packaging

Two containers, built from one staged tree, both landing in `dist/psp/`. This
platform is the only one here that ships more than one, and the reason is in
[Why two containers](#why-two-containers) below.

Three files describe the work and none of them is the build fragment:

| | |
|---|---|
| Declaration | `game/config/platform/psp/package.json` |
| Schema | `tools/schemas/package.schema.json` |
| Reader | `tools/psp_package.py` |

## Why the config lives under `game/`, and the schema does not

The same convention the Vita established. A cook list answers a **hardware**
question — how this processor wants a texture encoded — and lives in
`engine/config/psp/`. An icon, a display name and a container layout answer a
question about the **game**, and live in `game/config/platform/psp/`. The schema
is neither: it is the cook system's own contract, authored here rather than by
the game, so it lives with the other schemas in `tools/schemas/`.

The declaration's `$schema` field points at that schema by relative path purely
as an editor hint. Nothing reads it back; the tool that validates takes the
schema path as its own argument.

Identity is **not** restated here. The title identifier, display name and version
come from `game/config/title.json`, which every platform shares, and are folded
in when this declaration is read. That is what keeps the name the console shows
and the directory a save is filed under from disagreeing.

## Why the toolchain's own packaging macro is not used

The development kit ships a CMake macro that produces a container from an
executable target. It is not used here, and for a different reason than the
Vita's macros were rejected.

> The macro attaches its work as post-build commands on the executable target.
> Packaging in this project is gated on validation — `tools/validate_cooked.py`
> must pass before anything is packed — and a post-build hook runs when the
> executable links, which is before that gate. Using it would mean a container
> could be produced from a cooked tree that was never checked.

So the fragment calls the underlying tools directly, in a target that depends on
the validation target, exactly as the Vita fragment calls its own tools directly.
The macro is otherwise sound; this is a sequencing conflict, not a defect.

## `package.json`

| Field | Meaning |
|---|---|
| `platform` | `psp` |
| `title.id` | *Declared centrally* — from `title.json` |
| `title.name` | *Declared centrally*, overridable here |
| `title.version` | *Declared centrally* |
| `sfo.category` | `MG` — a memory-card application |
| `sfo.extra` | Raw metadata passed through unchanged: the memory-size request, the bootable flag, the firmware floor |
| `eboot.icon` | 144 x 80 image shown in the system menu |
| `eboot.picture` | 480 x 272 background shown behind the menu entry |
| `eboot.overlay`, `eboot.icon_anim`, `eboot.sound` | Optional container slots, unused here |
| `files` | Extra content staged into both containers |

The memory-size request asks for the extended region on later hardware models.
It is set, and the engine never spends it — see
[PLATFORM.md](PLATFORM.md#memory). It is harmless on the original model.

## Container slots

The memory-card container is a fixed sequence of slots, and an absent slot is not
omitted — it is filled with a placeholder. Getting the ordering wrong produces a
container the system menu shows with the wrong image rather than one it rejects,
so the reader emits the slots positionally and never by name.

## Why two containers

| | Memory card | Disc image |
|---|---|---|
| Runs on retail hardware | Yes, with custom firmware | Only under a loader that tolerates an unencrypted executable |
| Runs in the emulator | Yes | Yes |
| Asset root | The title's own directory | The disc's user directory |
| Writable storage | Same directory | None, unless a card is present |

The memory-card container is **primary**: it is the route that works everywhere,
and it is what a developer actually copies onto a device.

The disc image exists for two reasons. It is the shape that matches the
PlayStation 2's distribution, which keeps the two consoles comparable when a bug
looks like it might be in the container rather than the engine; and it is a
single file the emulator boots directly, which makes it the faster loop.

**One binary serves both.** The asset root is resolved at startup from whichever
container the title was started from, so neither is a separate build. See
[PLATFORM.md](PLATFORM.md#storage-and-io).

## Package layout

Memory card — copy `dist/psp/` to `PSP/GAME/<name>/`:

```
EBOOT.PBP        metadata, icon, background and the executable, in one file
RASSETS.PS2R     the master archive - every rasset and every compiled world
```

Disc image — `dist/psp/game.iso`:

```
UMD_DATA.BIN                   disc identity
PSP_GAME/PARAM.SFO             the same metadata, outside the container
PSP_GAME/ICON0.PNG             the same icon
PSP_GAME/PIC1.PNG              the same background
PSP_GAME/SYSDIR/EBOOT.BIN      the executable, unencrypted
PSP_GAME/USRDIR/RASSETS.PS2R   the master archive - every rasset and every compiled world
```

## Build order

1. **Validate** the cooked tree against the cook list that produced it. Nothing
   below runs if this fails.
2. **Read and validate** this declaration, folding in the shared title identity,
   and emit the build variables from it.
3. **Fix up** the linked executable's imports. Skipping this produces a binary
   that loads and then fails at its first system call.
4. **Generate** the metadata file from the declared fields.
5. **Pack** the memory-card container from the metadata, the images and the
   executable.
6. **Stage and image** the disc tree from the same inputs.

Steps 5 and 6 consume identical inputs, which is what guarantees the two
containers carry the same build.

## Adding a file to the package

Add a `{src, dst}` entry to `files[]` in the declaration. It is staged into both
containers. Never add it to the build fragment — what ships is described in one
place, and a fragment entry would reach one container and not the other.
