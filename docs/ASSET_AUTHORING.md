# Authoring assets

How to add content the engine can load. What happens to it afterwards is the
build pipeline's business — see [PIPELINE.md](PIPELINE.md).

## Source layout

Each asset is a pair of files, side by side anywhere under the asset source
directory: a metadata descriptor, and the raw source file it names.

```
<name>.json      metadata
<name>.png       the source file it points at
```

The pair may live at any depth under the source tree — the cook stage
discovers descriptors recursively, so organising sources into subfolders
never hides them from cooking. A source file with no descriptor beside it is
not cooked at all; that is how the same directory can hold content meant only
for another stage (a level compiler baking a texture straight into its own
container, for instance) alongside content meant for the archive.

The source may be in any common format the cook stage can read. Nothing consumes
the source at runtime — only the cooked result is shipped.

## Metadata

```json
{
  "type": "TEXTURE",
  "source": "player_tex.png",
  "deps": []
}
```

```json
{
  "type": "MODEL",
  "source": "enemy.obj",
  "deps": ["enemy_tex"]
}
```

| Field | Type | Meaning |
|---|---|---|
| `type` | string | `TEXTURE`, `MODEL`, `MATERIAL` or `FONT`. `SOUND` is enumerated but unimplemented. `THEME` assets are not authored per file — they are cooked from the title's theme declaration |
| `source` | string | The raw source file, in the same directory |
| `deps` | string[] | Other assets this one needs, up to the format maximum |
| `format` | string | Preferred texture encoding. A platform cook list may override it |
| `mip_levels` | number | Mip levels to generate |

A `MATERIAL` descriptor is shaped differently — it has no single `source`,
since its maps are named fields instead. See "Materials" below.

**Declare dependencies.** They are loaded before the asset that names them and
reference-counted, so a model is never reported ready before its textures, and a
texture in use cannot be evicted. One load request then suffices for a whole
object graph — the caller does not list the textures a model needs.

**Naming matters.** Assets are addressed by a canonical key derived from their
path, so the same asset referenced by device path and by baked dependency string
resolves to one entry rather than two. The rule is in
[formats/ARCHIVE_FORMAT.md](formats/ARCHIVE_FORMAT.md).

## Encoding is a platform decision

`format` is a preference, not an instruction. Each platform's cook list decides
what that platform actually wants, because the right encoding is a hardware
question — a palettised texture where video memory is scarce, a directly
uploadable one where it is not. See [PIPELINE.md](PIPELINE.md).

The consequence for authoring: **budget limits differ per platform**, and an
asset that fits one may be rejected on another. The constrained platform is the
one to check against. Its arithmetic is in
[ps2/TEXTURE_BUDGET.md](ps2/TEXTURE_BUDGET.md).

## Materials

A material lives under `assets/materials/`, mirroring `assets/textures/`
path-for-path (`assets/textures/props/box.jpg` ↔
`assets/materials/props/box.json`) — TrenchBroom's own face-texture picker
already calls itself the "material browser," and this is what makes painting
a brush with a texture the same act as picking that material, with no editor
plugin or config change. A texture with no matching material file still gets
an implicit default material (flat, no normal/ORM maps), so this is additive:
nothing about an existing `.map` or model needs to change to keep working.

Its descriptor names maps by real field (`albedo`, `normal`, ...), not a
single `source` — see [formats/MATERIAL_FORMAT.md](formats/MATERIAL_FORMAT.md)
for the full field list. Occlusion, roughness and metallic are authored as
**separate** source images (`occlusion`, `roughness`, `metallic`) — the way
art actually ships — and the cook step packs them into one runtime texture;
author a single pre-packed `orm` image instead only if that is what you
already have.

A material's own maps are cooked as ordinary standalone texture dependencies,
so the same per-platform dimension and budget rules above apply to them; a
platform's cook list may cap a normal or ORM map more tightly than the
general texture ceiling, or skip baking either entirely on fixed-function
hardware that could never sample them.

## Levels

A level names the resources it requires, and their types are inferred from the
assets themselves, so the list may freely mix kinds. Each is pinned on load so it
cannot be evicted mid-level.

**Loading a level is all-or-nothing.** If any required resource fails, everything
already pinned in that attempt is unpinned and unloaded and the load reports
failure, leaving the resource table and the texture budget exactly as they were.
There are no partial loads and no leaked pins.

## Checking your work

Cooked output can be inspected and validated without running the engine — headers,
dependency lists, encodings, sizes and budget usage. A validation failure names
the asset and the rule it broke. See [PIPELINE.md](PIPELINE.md).
