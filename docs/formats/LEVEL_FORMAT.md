# Level Format (.ps2l v2) & the Level Compiler

Levels are authored in TrenchBroom (Valve-220 `.map`) and compiled offline into a
sectorized runtime format, whose entries ship folded into the one master archive
every asset the game owns lives in (see [subsystems/ARCHIVE.md](../subsystems/ARCHIVE.md)).
The runtime streams a square ring of full-detail (LOD0) sectors around the camera
and, beyond it, the reduced-detail (LOD1) sectors the core's visibility lists
name for the camera's cell (see [subsystems/SECTOR.md](../subsystems/SECTOR.md)).

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
default 4 — see the compiler pipeline below for why this is mandatory on PS2),
and `_bake_ambient` (the ambient term of the static lighting bake, 0 to 1,
default 0.12 — see "Static lighting").

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
| Sector | `<NAME>/S<cx>_<cz>.SEC` | per-cell full-detail geometry (PSEC), streamed |
| LOD1 sector | `<NAME>/L<cx>_<cz>.SEC` | per-cell reduced-detail geometry (PSEC), streamed |
| Material | `<NAME>/<TEX>.PS2A`, `<NAME>/M<hash>.PS2A` or `RASSETS/<MAT>.PS2A` | `RES_MATERIAL`, resident while a sector using it is |
| Texture | `<NAME>/<TEX>_TEX.PS2A` or `RASSETS/<TEX>.PS2A` | TIM2 a material wraps |
| Atlas | `<NAME>/ATLAS_<hash>.PS2A` or `<NAME>/LOD1_<hash>.PS2A`, each with a `_TEX` TIM2 | per-cell texture atlas, as a material |
| Model | `<NAME>/<MODEL>.PS2A` | BKM2 entity model |

Within one level, entries are laid out in access order (core, then sectors
row-major with each cell's LOD1 sector beside its full-detail one, then
materials/textures/models) so a sector crossing reads contiguous
sectors. Across levels, the master archive orders rassets first and then each
level's entries in level-name order — locality between two different levels'
sectors is not meaningful, since loading one no longer means unmounting
another (see [subsystems/LEVEL.md](../subsystems/LEVEL.md)).

### `.ps2l` core chunks

`LevelFileHeaderV2` + `LevelChunkEntry[]` then 16-byte-aligned payloads:

- **INFO** — level name, grid origin/size, cell counts, material/entity counts.
- **MATL** — `LevelMaterialEntry[]`: the canonical archive key of each
  material's `RES_MATERIAL` asset (see [MATERIAL_FORMAT.md](MATERIAL_FORMAT.md)),
  atlases included. The same cooked material a model references can be, and
  for a shared texture will be, the exact one a level references too. A key
  that would not fit the 63-byte field is replaced by a stable hashed one
  rather than truncated.
- **SGRD** — `LevelGridCell[cellsX*cellsZ]` row-major: per-cell PSEC size (0 =
  empty), world-space AABB, and an entity range (reserved; v1 spawns all entities
  at load).
- **ENTS** — a flat list of entity spawn records (`classname`, origin, key/values
  as string-table offsets). The engine hands these to the game's generated
  `Ecs_SpawnDispatch` (see [the ECS pipeline](../../tools/ECS/generate_ecs.py)).
- **VISI** — one visibility list per grid cell, row-major: a cell count equal
  to `cellsX * cellsZ`, then a table of that many byte offsets, from the start of
  the chunk, to each cell's list, then the lists: a count followed by that many
  (x, z) cell pairs, nearest first. A list names only cells that have a LOD1 sector;
  it is what the runtime streams LOD1 geometry from.
- **FARF** — reserved chunk type, no longer emitted: the billboard far field
  was replaced by LOD1 sectors. A reader skips it.
- **BSPT** — reserved chunk type; indoor BSP is a future addition (never emitted
  in v1). `SectorHeader.bvhOffset` is the matching per-sector hook.

### Sector payload (PSEC)

`SectorHeader` (version 2) + `BakedMeshEntry[]` (reusing the BKM2 v3 mesh entry;
`materialIndex` indexes the level MATL table) + 16-byte-aligned vec4/vec3/vec2
(/vec4 colour) geometry. The runtime builds `Mesh` views straight into the
arena slot — zero copy into the existing render path. Meshes are
degenerate-stitched triangle strips (or lists) built by the same stripifier as
baked models ([ps2lib.mesh.bake_mesh](../../tools/ps2lib/mesh.py)).

A mesh's `colorsOffset` is populated when the map places any static light
(see "Static lighting" below); it is 0 (absent) for a level with none, and
the runtime then leaves every vertex at its default white tint, identical to
the format's behaviour before static baking existed.

The header's AABB is the exact extent of the sector's vertices, and every vertex
lies inside the sector's own grid cell (compiler step 3), so a sector's bounds
are its cell's bounds or smaller and two neighbouring sectors never overlap.

An atlased mesh carries UVs already confined to its atlas cell: the compiler
splits every triangle along texture-tile boundaries and insets each piece half
a texel inside its cell (see "LOD1 sectors and atlases" below), so no backend
needs a wrap or clamp mode to sample one.

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
- **VISI is internally consistent**: its cell count equals `cellsX * cellsZ`,
  every offset in the table and every per-cell list fits the chunk, and every
  pair it names is inside the grid. The streamer reads a list through the table
  with no further checking.
- **A PSEC mesh names a material inside the material table**: its
  `materialIndex` is below `materialCount`, not merely below
  `LEVEL_MAX_MATERIALS`, since MATL holds only `materialCount` entries.
- **A PSEC mesh table fits its blob**, and each entry's `vertsOffset`,
  `normsOffset`, `uvsOffset` and `colorsOffset` are 16-byte aligned and span
  `vertexCount` elements inside the blob. A sector declares no more than
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
3. Grid from the world AABB. Per face, fan-triangulate, then **cut** every
   triangle along each sector border it crosses, so a cell holds exactly the
   geometry inside it: no triangle spans two cells, and two neighbouring sectors
   meet on a straight line. A corner made by a cut lies exactly on the border and
   is computed from its edge's endpoints in a fixed order, so the two sides of the
   cut, and any other triangle sharing that edge, get the same corner. A polygon
   lying on a border goes to one cell only, and a sliver too small to see is
   dropped.
4. **Tessellate** each piece so no triangle edge exceeds `_max_edge` (default 4.0
   world units) — mandatory on PS2: ps2gl's VU1 renderers never truly clip, any
   triangle with a vertex outside the ±2048 guard band or behind the near plane is
   ADC-dropped **whole** (`external/ps2gl/vu1/clip_cull.i`), so giant brush faces
   vanish piecewise as the camera moves. Splitting always halves the longest edge;
   shared edges may split differently on either side (T-junctions), which is
   invisible on coplanar faces but a known v2 refinement. The cut comes first so
   the tessellation lines up with the borders instead of being sliced by them.
5. Per cell, group by material and bake one mesh each into a PSEC blob. Enforces
   `LEVEL_MAX_MESHES_PER_SECTOR` (32) and `LEVEL_SECTOR_MAX_BYTES` (512KB → one
   arena slot); over-budget is a hard error — shrink `_sector_size` or reduce
   geometry density in the offending cell. Every cell is also built a second
   time, at reduced detail, into a LOD1 sector, and per cell the meshes whose
   textures tile within a small range are packed into one atlas (see "LOD1
   sectors and atlases" below).
6. Bake each brush material and each point-entity `.obj` to BKM2. Missing
   sources warn and fall back (magenta texture / kept raw model reference).
   A brush texture with a material authored under `assets/materials/`,
   mirroring its own relative path under `assets/textures/` (the same
   convention that makes TrenchBroom's own material-picker a material-picker
   — see [MATERIAL_FORMAT.md](MATERIAL_FORMAT.md)), **references that
   already-cooked `RES_MATERIAL` directly**, exempt from this level's
   dimension cap exactly like a shared standalone texture already is. A brush
   texture with no such material gets an engine-default-generated one (flat,
   no normal/ORM) wrapping its baked TIM2, so every existing `.map` keeps
   compiling unchanged. The wrapped TIM2 itself follows the same rule
   standalone textures always have: a texture with a `TEXTURE` descriptor
   beside it — the same descriptor `tools/cook_assets.py` reads to cook it as
   a standalone `RASSETS/*.PS2A` resource — is **referenced from the boot
   archive instead of baked again**, provided it already fits this platform's
   `level_textures` dimension cap; a level that paints with it does not carry
   a second copy. A texture's dimensions are otherwise (no descriptor, or the
   shared copy is oversized) first capped to the target platform's own
   `cooklist.json` `level_textures` policy (every resident sector pins its
   materials, and many sectors are resident at once, so this is stricter than
   the `assets.TEXTURE` ceiling), then downscaled further if it still would not fit that
   platform's `IO_READ_BUFFER_SIZE`. This is why a level compiles **per
   platform** — see [PIPELINE.md](../PIPELINE.md).
7. Bake static lighting: for every entity the map places that carries
   `LightComponent` (discovered generically from `tools/ECS/ECS.json`, never
   by a hardcoded classname — see "Static lighting" below), evaluate ambient
   plus that light at every static vertex, including a shadow-ray occlusion
   test against every rendered triangle of the map, and bake the sum into that
   vertex's colour. Skipped entirely (zero
   cost) when a map places no such entity.
8. Build the per-cell visibility lists and assemble the archive.

## LOD1 sectors and atlases

Every cell with geometry gets a second, reduced-detail sector, built from every
brush except `func_detail` brushwork (unless that entity sets
`include_in_lod1`). It is cut at the same sector borders as the full-detail
tier and keeps every height exactly: a LOD1 floor is the full-detail floor, not
a lowered or snapped copy of it, so the two tiers meet without a step.

How the tier is reduced depends on whether the target's renderer clips in
hardware.

- **Platforms that clip (every platform but PS2).** A face none of whose fine
  triangles lies inside another LOD1 brush — a triangle is hidden when its
  centroid, pushed a little into its own brush, lies inside another one — is
  tessellated at `LOD1_MAX_EDGE` (8 world units) instead of `_max_edge`, so a
  floor cell is a handful of triangles rather than dozens. A face with a
  hidden triangle keeps its fine triangles minus the hidden ones.
- **PS2, and a compile that names no platform.** The guard-band rule forbids
  large triangles, so the fine triangles are kept, minus the hidden ones, and
  their vertices are clustered onto a grid in X and Z only. The grid is chosen
  so a whole number of its steps lie across a sector and a sector border stays
  on a grid line; Y is untouched, so a floor keeps its height and a thin slab
  keeps its thickness. Triangles that collapse are dropped, and an edge made
  longer than `_max_edge` is split again.

Vertices are lit after any clustering, at their final positions (see "Static
lighting").

All of a LOD1 cell's textures are packed into one atlas; a full-detail cell
packs only the textures whose UVs stay within a small tiling range and keeps
the rest as separate meshes. Atlas textures are baked outside the platform's
`level_textures` cap.

Visibility is a fixed radius today: every cell lists every LOD1 cell within
twelve cells of it, nearest first. Portal-based lists are the intended
replacement (see `docs/backlog/Rework Level System.md`).

Known limitations:

- A LOD1 sector streams into an `ARENA_LEVEL_LOD1` slot, which on the smaller
  targets is far smaller than `LEVEL_SECTOR_MAX_BYTES`. The compiler checks each
  LOD1 sector against its platform's slot size and the cook fails when one does
  not fit; the remedy is a smaller `_sector_size`, or `func_detail` on the
  offending brushes.
- Clustering moves a LOD1 vertex up to half a grid step horizontally, so on PS2
  the far field can stand slightly proud of the full-detail floor at its outer
  edges, and a feature thinner than a grid step can collapse.

## Authoring checks the compiler reports

A face whose UVs cover under 4% of its texture on both axes draws as one flat
colour, because it samples a window a few texels wide. The compiler names each
such texture with a count of faces. It is almost always a face scale left at the
editor's default on a very large texture, or a texture made for a model's own UV
layout — a photoscan albedo with a black border, say, which then paints the
border. The check is a warning, not a refusal.

## Static lighting

Any entity a mapper composes with **`LightComponent`** (`tools/ECS/ECS.json`)
becomes a bake light — not a dedicated classname, so a custom entity (a lit
prop, say) can carry one alongside whatever else it already has. The
compiler discovers these generically, by checking each placed entity's
declared component list, the same way `tools/ECS/generate_ecs.py` already
decides what belongs in the generated FGD and spawn dispatch.

`LightComponent` reads the entity's own `TransformComponent`-adjacent,
native `.map` keys directly — `origin` for a point light's position,
`angles` (Quake pitch/yaw/roll) for a directional light's direction — the
same keys the compiler already reads for every other entity, not the
generic `TransformComponent.position`/`.angles` runtime convenience
properties. Its own properties are `light_type` (Directional or Point),
`color`, `intensity`, and `range` (point lights only).

The bake evaluates ambient plus every light at each static vertex against every
rendered triangle of the map — `func_detail` brushwork included, and geometry
in other cells included — through a uniform grid over the sector cells that the
shadow ray walks. A vertex's colour is therefore a function of its position and
normal alone: a vertex on a sector border has the same colour in both sectors,
a vertex the two tiers share has the same colour in both, and a shadow does not
stop at a cell border. The result is written into the affected sector meshes'
`colorsOffset` array (see "Sector payload" above).

The ambient term is `_bake_ambient`, 0.12 unless the map sets it. It is also the
whole colour of any face no light reaches — a face turned away from every light,
the underside of a floor, a surface in every light's shadow — so a scene whose
lights are all occluded or turned away reads as near-black, and raising
`_bake_ambient` is the way to lift it. A directional light's `angles` pitch
follows Quake's convention, in which a positive pitch looks down: a directional
light meant to shine from above takes a positive pitch, and a negative one
points it up, where it reaches only faces that point downward.

A light-bearing entity is also spawned through the ordinary entity path
unchanged (`docs/subsystems/LEVEL.md`), so a game wanting a *live*,
dynamic counterpart too (a flickering torch, say) calls
`Renderer::SetLight3D` from its own spawn handler — fully decoupled from
the bake, which only ever reads the entity's authored properties once,
offline.

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
level compiler both still read it. See `assets/README.md`. A richer,
hand-authored material for one of these textures lives under
`assets/materials/`, mirroring the same relative path — see
[MATERIAL_FORMAT.md](MATERIAL_FORMAT.md).

**Placing a static light**: add a `light` entity (or compose `LightComponent`
onto any other entity class) from the entity browser, once the `.fgd`
regeneration above has run; set `light_type`, `color`, `intensity` and, for a
point light, `range` in its properties, and position or orient it as any
other entity. See "Static lighting" above.

`.map` files saved into `assets/maps/` are compiled once per platform by the
`compile-levels-<platform>` CMake target into `dist/cooked/<platform>/levels/`
— the same shape as `cook-<platform>`/`dist/cooked/<platform>/rassets/` — and
folded into that platform's master archive by its packaging stage. See
[PIPELINE.md](../PIPELINE.md).

```
python3 tools/compile_level.py assets/maps/test.map --out build/levels \
    --textures assets/textures --models assets/models \
    --materials assets/materials --platform ps2 \
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
