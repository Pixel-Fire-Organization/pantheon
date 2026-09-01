# Asset Source Tree (`assets/`)

This is the one asset source tree for both level authoring and the resource
archive. There is no separate "raw asset" directory elsewhere in the repo.

```
assets/
├── maps/       TrenchBroom .map sources — level-only, never cooked by cook_assets.py
├── textures/   texture sources, optionally paired with a .json descriptor
├── models/     model sources, optionally paired with a .json descriptor
└── <loose files>   copied byte-for-byte onto the PS2 disc root
```

- **`maps/`** is compiled by the `compile-levels-<platform>` CMake target
  (`tools/compile_level.py --platform <name>`) directly into a level
  container, per platform — a level pins every one of its brush materials for
  its whole lifetime, so a material's max size is capped by that platform's
  own `cooklist.json` (`level_textures`), the same idea as the `TEXTURE`
  ceiling `cook_assets.py` reads but sized for many simultaneously-pinned
  materials instead of one deliberately-loaded resource. Maps are never read
  by `cook_assets.py` itself, only by the level compiler.
- **`textures/` and `models/`** serve two consumers that can both read the
  same file:
    - `compile_level.py` reads raw texture and model files directly and bakes
      them into a level's own container, downscaling a material that exceeds
      the target platform's cap. **If the texture already has a `.json`
      descriptor beside it** (see below) and fits the cap as-is, the level
      references the standalone `RASSETS/*.PS2A` copy instead of baking its
      own — give a material used by more than one level a descriptor to stop
      it being duplicated into every level that paints with it.
  - `tools/cook_assets.py` walks these two directories recursively (any
    depth) looking for `.json` descriptors, and cooks each pair it finds
    into a standalone `.ps2a` in `dist/cooked/<platform>/rassets/`. A file
    with no descriptor next to it is level-only content and is never cooked
    into the archive.
  - **TrenchBroom's own requirement is stricter than the cook stage's**: it
    only shows a texture as a material if it sits in a subdirectory *exactly
    one level* under `textures/` (a "material collection", e.g.
    `textures/props/BOX.JPG`) — a loose file directly in `textures/`, or one
    nested two levels deep, is invisible in the material browser even though
    the cook stage and the level compiler would both still read it fine.
    Always give a texture meant to be paintable in TrenchBroom a one-level
    collection folder.
- **Loose files directly under `assets/`** (not inside `maps/`, `textures/`,
  or `models/`) are not part of either pipeline. On PS2 they are copied
  byte-for-byte onto the ISO root, the same way any other file placed there
  would be — see `docs/ps2/ISO_GENERATION.md`.

---

## Descriptor File Format

A cooked asset is a `.json` descriptor next to the source file it names,
sharing its base name:

```
textures/props/BOX.JSON   ←  descriptor
textures/props/BOX.JPG    ←  source image
```

The packer derives the output name from the JSON filename:
`MAINMENU.JSON` → `rassets/BOX.ps2a` → `cdrom0:\RASSETS\BOX.PS2A;1`

### Schema

```json
{
    "type":   "TEXTURE",
    "source": "MAINMENU.JPG",
    "deps":   []
}
```

| Field    | Type     | Required | Description                                                       |
|:---------|:---------|:---------|:------------------------------------------------------------------|
| `type`   | string   | Yes      | Asset type (see table below)                                      |
| `source` | string   | Yes      | Source filename — must be in the same directory as the descriptor |
| `deps`   | string[] | Yes      | Names of other assets this one depends on (max 8, omit extension) |

---

## Asset Types

| `type` value | Source file format             | Notes                                                                                                                     |
|:-------------|:-------------------------------|:--------------------------------------------------------------------------------------------------------------------------|
| `TEXTURE`    | `.png`, `.jpg`, `.bmp`, `.tga` |                                                                                                                           |
| `MODEL`      | `.obj`, `.gltf`, `.glb`        |                                                                                                                           |
| `FONT`       | `.ttf`, `.otf`                 | A cooked font is metrics only; its atlas is an ordinary texture named as a dependency — see `docs/subsystems/RESOURCE.md` |
| `SOUND`      | `.wav`, `.ogg`, `.mp3`         | Enumerated but unimplemented on every platform                                                                            |

`THEME` assets are not authored as a descriptor pair — they are cooked from
`game/config/theme.json`.

---

## Dependencies

The `deps` array lists the **base names** of other assets that must be loaded
before this one. The packer resolves them to full disc paths automatically:

```json
{
    "type":   "MODEL",
    "source": "ENEMY.OBJ",
    "deps":   ["ENEMY_TEX"]
}
```

This tells the runtime: when loading `ENEMY.ps2a`, also load
`cdrom0:\RASSETS\ENEMY_TEX.PS2A;1` first and increment its reference count.
The dependency is declared once in the JSON — no changes needed in game code.

- Maximum **8 dependencies** per asset.
- Dependency names are case-insensitive at authoring time; the packer
  uppercases them.
- Circular dependencies are not detected — avoid them.

---

## Naming Conventions

| Rule                                            | Example                                            |
|:------------------------------------------------|:---------------------------------------------------|
| Base name = JSON name = output name             | `MAINMENU.JSON` → `BOX.ps2a`                            |
| Names are uppercased on disc (ISO 9660 Level 1) | `box.json` still produces `BOX.PS2A;1`             |
| Source file must be next to its descriptor      | Do **not** point `source` at a different directory |
| Dep names have **no extension**                 | `"deps": ["BOX"]` not `"deps": ["BOX.PS2A"]`       |

---

## Binary `.ps2a` Layout

The packer writes a **2080-byte fixed header** followed by the raw source file bytes:

```
Offset  Size   Field
------  -----  -------------------------------------------
0       4      magic        = 0x50533241 ("PS2A" little-endian)
4       4      type         = 0=TEXTURE 1=MODEL 2=SOUND 3=FONT
8       1      depCount     = number of active dependency slots (0–8)
9       3      reserved     = 0x000000
12      16     ext          = source file extension e.g. ".jpg", ".png" (null-terminated)
28      2048   deps[8][256] = null-terminated disc paths for each dep
2076    4      dataSize     = byte count of the payload that follows
2080    N      <raw source file bytes>
```

See `docs/formats/ASSET_FORMAT.md` for the authoritative format contract and
`docs/subsystems/RESOURCE.md` for the runtime load contract.

---

## How to Pack

Packing runs **automatically during the CMake build**. To run it manually:

```bash
# From the project root (WSL / Linux)
python3 tools/cook_assets.py --platform win32

# Override source/output directories
python3 tools/cook_assets.py --src assets/textures --src assets/models \
    --dst dist/cooked/win32/rassets --cooklist engine/config/win32/cooklist.json
```

---

## Accessing Assets at Runtime (C++)

```cpp
int handle = Engine_Resource_Load(RES_TEXTURE, "RASSETS/BOX.PS2A");

void GameUpdate(float dt)
{
    if (Engine_Resource_IsReady(handle))
        /* draw using Engine_Resource_Get(handle) */;
}
```

See `docs/ASSET_AUTHORING.md` for authoring rules and
`docs/subsystems/RESOURCE.md` for the runtime contract.

---

## Example: Adding a New Texture

1. Drop `WALL.JPG` into a collection folder, e.g. `assets/textures/props/WALL.JPG`
   (a one-level-deep subfolder — required for it to show up in TrenchBroom's
   material browser, see above).
2. Create `WALL.JSON` next to it:
   ```json
   { "type": "TEXTURE", "source": "WALL.JPG", "deps": [] }
   ```
3. Build — the cook stage produces `rassets/WALL.ps2a`.
4. In C++: `Engine_Resource_Load(RES_TEXTURE, "RASSETS/WALL.PS2A")`.

## Example: Textured Model with Dependency

1. Drop `CRATE.OBJ` into `assets/models/` and `CRATE_TEX.JPG` into a
   collection folder under `assets/textures/`, e.g. `assets/textures/props/`.
2. Create `CRATE_TEX.JSON` next to the texture:
   ```json
   { "type": "TEXTURE", "source": "CRATE_TEX.JPG", "deps": [] }
   ```
3. Create `CRATE.JSON` next to the model:
   ```json
   { "type": "MODEL", "source": "CRATE.OBJ", "deps": ["CRATE_TEX"] }
   ```
4. In C++: `Engine_Resource_Load(RES_MODEL, "RASSETS/CRATE.PS2A")` — the
   texture is loaded automatically as a dependency.
