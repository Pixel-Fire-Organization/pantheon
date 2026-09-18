# Level Format (.ps2l v2) & the Level Compiler

Levels are authored in TrenchBroom (Valve-220 `.map`) and compiled offline into a
sectorized runtime format, whose entries ship folded into the one master archive
every asset the game owns lives in (see [subsystems/ARCHIVE.md](../subsystems/ARCHIVE.md)).
The runtime streams a 3×3 ring of sectors around the camera and draws billboard
impostors for the rest (see [subsystems/SECTOR.md](../subsystems/SECTOR.md) once
Phase D lands).

- Format structs: [engine/include/EngineLevelFormat.h](../../engine/include/level/EngineLevelFormat.h)
  (sizes locked with `static_assert`), mirrored by [tools/ps2lib/levelfmt.py](../../tools/ps2lib/levelfmt.py).
- Compiler: [tools/compile_level.py](../../tools/compile_level.py); inspector: [tools/dump_level.py](../../tools/dump_level.py).

## Coordinate convention

TrenchBroom/Quake is **Z-up** (X east, Y north, Z up); the engine is **Y-up**
(OpenGL, right-handed). The compiler maps `(x, y, z)_quake → (x, z, -y)_engine`,
so the map's horizontal X/Y plane becomes the engine's X/Z ground plane and the
sector grid partitions the ground. Positions are scaled by `_map_scale`
(worldspawn key, default `1/32` — 32 map units ≈ 1 metre). UVs are computed from
the untransformed Quake vertices, because the Valve-220 U/V axes live in map space.

Worldspawn keys: `_map_scale` (default 1/32), `_sector_size` (world units per
grid cell, default 64), `_max_edge` (max triangle edge after tessellation,
default 4 — see the compiler pipeline below for why this is mandatory on PS2).

## On-disc layout

`tools/compile_level.py` compiles one map straight into a standalone archive
`<NAME>.PS2R` (see [ARCHIVE_FORMAT.md](ARCHIVE_FORMAT.md)), written under
`dist/cooked/<platform>/levels/`. That file is a build-time artefact, not what
ships: it exists so one level can be compiled, inspected
([tools/dump_level.py](../../tools/dump_level.py)) and reasoned about in
isolation. `tools/pack_master_archive.py` then reads every level archive's
table of contents and payloads back out, byte-identical, and folds them into
the one master archive that actually ships alongside the platform's rassets
(see [subsystems/ARCHIVE.md](../subsystems/ARCHIVE.md)) — the keys below are
already globally unique (each is namespaced by its own level name), so this
merge needs no rewriting, only concatenation:

| Entry | Key | Contents |
|-------|-----|----------|
| Core | `<NAME>.PS2L` | chunked metadata, always resident once loaded |
| Sector | `<NAME>/S<cx>_<cz>.SEC` | per-cell geometry (PSEC), streamed |
| Texture | `<NAME>/<TEX>.PS2A` or `RASSETS/<TEX>.PS2A` | TIM2 material, pinned for level lifetime |
| Model | `<NAME>/<MODEL>.PS2A` | BKM2 entity model |
| Impostors | `<NAME>/FARFIELD.PS2A` | far-field billboard atlas |

Within one level, entries are laid out in access order (core, then sectors
row-major, then textures/models/atlas) so a sector crossing reads contiguous
sectors. Across levels, the master archive orders rassets first and then each
level's entries in level-name order — locality between two different levels'
sectors is not meaningful, since loading one no longer means unmounting
another (see [subsystems/LEVEL.md](../subsystems/LEVEL.md)).

### `.ps2l` core chunks

`LevelFileHeaderV2` + `LevelChunkEntry[]` then 16-byte-aligned payloads:

- **INFO** — level name, grid origin/size, cell counts, material/entity counts.
- **MATL** — `LevelMaterialEntry[]`: the canonical archive key of each material's
  TIM2 (and the far-field atlas as the last entry).
- **SGRD** — `LevelGridCell[cellsX*cellsZ]` row-major: per-cell PSEC size (0 =
  empty), world-space AABB, and an entity range (reserved; v1 spawns all entities
  at load).
- **ENTS** — a flat list of entity spawn records (`classname`, origin, key/values
  as string-table offsets). The engine hands these to the game's generated
  `Ecs_SpawnDispatch` (see [the ECS pipeline](../../tools/ECS/generate_ecs.py)).
- **FARF** — `FarfieldHeader` + `FarfieldCluster[]` + `FarfieldFrame[]`: one
  cluster per non-empty cell, `azimuthCount` impostor views each. The frame
  array is whatever the chunk has room for after the clusters, and every
  cluster owns a full set of `azimuthCount` frames: once the atlas is full the
  compiler stops emitting clusters rather than emit one with fewer views.
- **BSPT** — reserved chunk type; indoor BSP is a future addition (never emitted
  in v1). `SectorHeader.bvhOffset` is the matching per-sector hook.

### Sector payload (PSEC)

`SectorHeader` + `BakedMeshEntry[]` (reusing the BKM2 v2 mesh entry;
`materialIndex` indexes the level MATL table) + 16-byte-aligned vec4/vec3/vec2
geometry. The runtime builds `Mesh` views straight into the arena slot — zero
copy into the existing render path. Meshes are degenerate-stitched triangle
strips (or lists) built by the same stripifier as baked models
([ps2lib.mesh.bake_mesh](../../tools/ps2lib/mesh.py)).

### What a reader must establish before it trusts a chunk

Every chunk view is a cast into bytes read off disc, so the offsets and counts
below are checked, not assumed. A core or sector failing any of them is refused
whole; nothing is clamped into range, because a clamped offset still reads bytes
belonging to something else.

- **The chunk table fits the blob**, and so does every chunk's `offset + size`.
  Both sums are computed at a width that cannot wrap a 32-bit `size_t`.
- **Every chunk offset is a multiple of 16** (`LEVEL_CHUNK_ALIGN`). The compiler
  aligns them, and the runtime requires it: an unaligned word access traps on
  both MIPS targets, so a one-byte shift in the table is a crash rather than a
  bad read.
- **INFO is at least `sizeof(LevelInfoChunk)`**, its `cellSize` is finite and
  greater than zero, and `cellsX`/`cellsZ` are non-zero. `cellSize` divides
  every world position that is turned into a cell index; a zero produces an
  infinity whose conversion to an integer is undefined.
- **SGRD holds at least `cellsX * cellsZ` cells**, since the cell index is
  computed from INFO and never bounded against SGRD afterwards.
- **MATL is present and holds at least `materialCount` entries** whenever INFO
  declares any, and `materialCount` does not exceed `LEVEL_MAX_MATERIALS`.
- **ENTS is internally consistent**: its record array, property array and string
  table each fit the chunk, the property array is a whole number of properties,
  and every `classnameOffset`, `keyOffset`, `valueOffset` and
  `propFirst + propCount` stays inside its table.
- **FARF is internally consistent**: its cluster array fits the chunk, the
  remainder is a whole number of frames, it names no more atlases than the
  header holds (`LEVEL_FARFIELD_MAX_ATLASES`), every atlas names a material
  inside the material table, `azimuthCount` is non-zero, and every cluster
  samples an atlas the header names and owns `azimuthCount` frames inside the
  frame array. Nothing draws the far field yet; it is checked where the view
  is published so the first consumer inherits a bounded view rather than a
  raw chunk.
- **A PSEC mesh table fits its blob**, and each entry's `vertsOffset`,
  `normsOffset` and `uvsOffset` are 16-byte aligned and span `vertexCount`
  elements inside the blob. A sector declares no more than
  `LEVEL_MAX_MESHES_PER_SECTOR` meshes.

Fixed-width key fields — `LevelInfoChunk::name`, `LevelMaterialEntry::assetKey`
— and the ENTS string table are terminated by the reader as part of the same
pass. The compiler always writes them terminated, so a reader may make that a
property of what it loaded rather than of what it was given.

## Compiler pipeline

1. Parse the `.map` (`ps2lib.mapparse`): entities + brush face polygons via plane
   clipping. Faces named `skip`/`nodraw`/`clip`/`trigger*`/`hint`/`origin` are
   culled from render geometry.
2. Convert + scale vertices to engine space.
3. Grid from the world AABB. Per face, fan-triangulate then **tessellate** so no
   triangle edge exceeds `_max_edge` (default 4.0 world units) — mandatory on
   PS2: ps2gl's VU1 renderers never truly clip, any triangle with a vertex
   outside the ±2048 guard band or behind the near plane is ADC-dropped
   **whole** (`external/ps2gl/vu1/clip_cull.i`), so giant brush faces vanish
   piecewise as the camera moves. Splitting always halves the longest edge;
   shared edges may split differently on either side (T-junctions), which is
   invisible on coplanar faces but a known v2 refinement.
4. Each resulting **triangle** — not the whole face — is assigned to a cell by
   its own centroid: a face's footprint can span many cells (a large floor or
   a skybox wall), and binning by the whole face would dump every one of its
   tessellated triangles into a single cell regardless of how far apart they
   end up, defeating `_sector_size` entirely for anything but small faces. Per
   cell, group by material and bake one mesh each into a PSEC blob. Enforces
   `LEVEL_MAX_MESHES_PER_SECTOR` (32) and `LEVEL_SECTOR_MAX_BYTES` (512KB → one
   arena slot); over-budget is a hard error — shrink `_sector_size` or reduce
   geometry density in the offending cell.
5. Bake each material to a PAL8 TIM2, each point-entity `.obj` to BKM2. Missing
   sources warn and fall back (magenta texture / kept raw model reference). A
   material with a `TEXTURE` descriptor beside it — the same descriptor
   `tools/cook_assets.py` reads to cook it as a standalone `RASSETS/*.PS2A`
   resource — is **referenced from the boot archive instead of baked again**,
   provided it already fits this platform's `level_textures` dimension cap; a
   level that paints with it does not carry a second copy. A material's
   dimensions are otherwise (no descriptor, or the shared copy is oversized)
   first capped to the target platform's own `cooklist.json` `level_textures`
   policy (a level pins every material for its whole lifetime, so this is
   stricter than the `assets.TEXTURE` ceiling), then downscaled further if it
   still would not fit that platform's `IO_READ_BUFFER_SIZE`. This is why a
   level compiles **per platform** — see [PIPELINE.md](../PIPELINE.md).
6. Bake far-field impostors and assemble the archive.

## Far-field impostors (v1 limitations)

Each non-empty cell becomes one cluster with `LEVEL_FARFIELD_AZIMUTHS` (4:
N/E/S/W) orthographic views, rendered host-side as **flat-colour silhouettes**
(mean texel colour per face) with a z-buffer, packed into a PAL8 atlas whose
index 0 is transparent. This is deliberately coarse:

- 4-view azimuth snapping is visible when the camera rotates around a cluster
  (`azimuthCount` is data-driven — 8 views is an atlas-size change only).
- Flat per-face colour, no lighting or texture projection (a v1.5 upgrade).
- No cross-fade between impostor and streamed geometry yet.

The chunked FARF design leaves room for an alternative low-poly-mesh far field
later without a format break.

## Authoring & building

`tools/trenchbroom/TrenchBroom.exe` is a portable build with the engine's game
profile already registered (`tools/trenchbroom/games/Pantheon/`), so no
manual game-config setup is needed. When opening or creating a map, select
"Pantheon" and set its **Game Path to the repo's `assets/` folder** —
`GameConfig.cfg`'s search path is `.` (the Game Path itself), so this is what
resolves the material root to `assets/textures` and puts entity definitions at
the generated `tools/trenchbroom/games/Pantheon/Pantheon.fgd`. The `.fgd` is
regenerated from `tools/ECS/ECS.json` by the `ecs-generate` CMake target and
copied back into that committed profile directory — rebuild after changing an
ECS component so TrenchBroom picks up new entity classes.

**TrenchBroom only shows a texture in the material browser if it sits exactly
one directory level under `assets/textures/`** (a "material collection", e.g.
`assets/textures/props/box.jpg`) — a loose file directly in `textures/`, or one
nested two levels deep, is invisible there even though the cook stage and the
level compiler both still read it. See `assets/README.md`.

`.map` files saved into `assets/maps/` are compiled once per platform by the
`compile-levels-<platform>` CMake target into `dist/cooked/<platform>/levels/`
— the same shape as `cook-<platform>`/`dist/cooked/<platform>/rassets/` — and
folded into that platform's master archive by its packaging stage. See
[PIPELINE.md](../PIPELINE.md).

```
python3 tools/compile_level.py assets/maps/test.map --out build/levels \
    --textures assets/textures --models assets/models --platform ps2 \
    --report --debug-render occ.png
python3 tools/dump_level.py build/levels/TEST.PS2R
```

Omitting `--platform` compiles unrestricted (no `level_textures` cap, no
byte-budget floor tighter than a generous default) — useful for quick local
inspection, but not what any real build does.

## Budgets

| Constant                      | Value  | Meaning                                        |
|-------------------------------|--------|------------------------------------------------|
| `LEVEL_CHUNK_ALIGN`           | 16     | chunk and geometry-array start alignment       |
| `LEVEL_MAX_MATERIALS`         | 64     | materials per level; exceeding it fails the cook |
| `LEVEL_MAX_MESHES_PER_SECTOR` | 32     | one per material present in a cell             |
| `LEVEL_SECTOR_MAX_BYTES`      | 512 KB | one `ARENA_LEVEL_DATA` slot                    |
| `LEVEL_GS_PAGE_BUDGET`        | 200    | GS pages for level textures + atlases (of 264) |
| `LEVEL_FARFIELD_AZIMUTHS`     | 4      | impostor views per cluster                     |
| `LEVEL_FARFIELD_MAX_ATLASES`  | 4      | impostor atlases a far field may name          |
