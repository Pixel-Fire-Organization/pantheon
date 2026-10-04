# Subsystem — Sector

*Codename: Atlas*

## Purpose

Keep the geometry near the streaming centre resident, and nothing else. A world
is larger than the memory budget of every platform the engine targets, so the
engine holds a small neighbourhood of it and moves that neighbourhood as the
centre moves.

## Contract

**A fixed ring of residents.** A small, constant number of grid cells around the
centre are resident at once: a square whose radius, in cells, is a platform
constant. The count is fixed per platform and does not grow with world size —
that is the property that makes an arbitrarily large world fit a fixed budget. A
wider ring moves the boundary between the two tiers away from the centre, at the
cost of more resident sectors, so a platform sets the radius its memory and its
read bandwidth can afford.

**A second, reduced-detail tier covers what the ring does not.** Beyond the
full-detail ring, the level's visibility list for the centre's cell names the
cells whose reduced-detail (LOD1) sectors should be resident. They occupy their
own arena and their own fixed-capacity array, so neither tier can starve the
other. The tier wants the nearest cells of that list that lie outside the ring,
as many as it has slots, and nothing else: a LOD1 sector is never requested for
a cell inside the full-detail ring. The wanted set is recomputed when the
centre's cell changes and whenever a slot frees; a resident outside it is
evicted, which is not the same as a resident the list no longer names — a list
can name more cells than the tier holds, and the cells it names stay wanted only
while they are among the nearest. Missing cells are requested nearest first, a
few per frame, so the tier fills over several frames rather than in one burst.
A list longer than the slots leaves its farthest cells unstreamed, and that is
reported once each time its size changes. A centre outside the grid reads the list of
the nearest cell inside it, so the tier keeps covering the edge of the world the player
is looking at; the ring stays centred on the true position, so with no cell of it inside
the grid no full-detail sector is resident and the tier alone has to cover the world,
which a world with more cells than slots cannot do.

**Neither tier opens a hole when the ring moves.** A LOD1 sector already
resident for a cell that has just entered the ring is kept until that cell's
full-detail sector is fully resolvable, then released and its slot reused. A
full-detail sector for a cell that has just left the ring is kept until the LOD1
sector for that cell is fully resolvable, then released. It is released at once
instead when the cell has no LOD1 sector in reach, and it is the first thing
given up when the ring itself needs a slot. The platform's spare full-detail
slots — the resident count less the ring's cells — bound how many cells can be
handed over this way at once; a platform with fewer spare slots than cells
leaving the ring accepts a brief hole for the rest rather than refusing to load
the ring. With every LOD1 slot in use, one far cell is swapped for another at
each recentre, because the cells that just entered the ring hold slots until
their full-detail sectors resolve.

**A full-detail sector draws only once it is fully resolvable.** Until every
mesh's material and that material's albedo texture are resident, the renderer
skips it, and a LOD1 sector covering the same cell stays opaque instead of
fading out. The world never shows untextured geometry for a moment while
textures stream in behind a sector that arrived first.

**Recentring is hysteretic.** The ring moves when the centre crosses a cell
boundary by more than a margin, not the moment it crosses. Without hysteresis, a
centre oscillating on a boundary would evict and reload the same cells every
frame — the worst possible streaming behaviour, and one that appears only when
someone stands still in the wrong place.

**Geometry is exposed as views into arena storage.** A resident sector meshes
point directly at the arena slot holding its data. Nothing is copied between the
slot and the renderer. This is why sector slots carry the arena hardware
alignment: the transfer path can consume them as they lie.

**A sector is validated before it becomes a view.** Because those views go
straight to the renderer with nothing between them and the transfer path, every
vertex, normal and texcoord offset a sector declares is checked against the
sector's own size and against the alignment the format guarantees, at a width
that cannot wrap on a 32-bit target. A sector that fails any of it, or that
declares more meshes than the ring holds, is refused rather than clamped: a
clamped mesh count silently loses geometry, and a clamped offset still reads
bytes that belong to something else.

**Residency is a state, and callers must respect it.** A sector is empty,
loading, or ready. Only ready sectors have valid geometry. The resident array is
fixed-capacity and sparse — entries are not contiguous, and consumers iterate the
whole array and skip empty ones.

**Reads complete asynchronously, and a late completion is discarded.** A sector
is requested and marked loading; its geometry arrives on a later frame. Every
resident carries a generation that eviction advances, and a completion whose
generation no longer matches is dropped, so a read for a cell that was evicted
while in flight never lands in a slot that has since been reused.

**Materials are pinned per mesh, for as long as the sector is resident.** Each
resident mesh carries the handle of the material its level table names, pinned
when the sector becomes ready and unpinned when it is evicted. Pins are
counted, so a material two resident sectors share survives either one being
evicted.

**Unloading releases both tiers, and a read that outlives it is discarded.**
Ending the subsystem releases every resident of both tiers — each mesh's
material is unpinned and the resident's generation advances before it is
cleared — and the level's unload then clears both sector arenas. A scene switch
unloads the level before the runtime reset drains IO, so a sector read that was
in flight completes afterwards; its completion finds the generation moved, or no
level at all, and is dropped. Beginning a level releases anything still
resident and never rewinds a generation, so a completion from the previous level
cannot match a resident of the next.

**Residency can be read back per cell.** The cell the ring is centred on, and
for any cell whether it has geometry at all and whether each tier is loading or
ready for it, can be queried; the game surface wraps both. A tier counts as
ready only when its sector is fully resolvable, the same test the renderer
applies before drawing it. The query is for tooling and debug overlays and never
influences streaming.

## Depends on

- [Level](LEVEL.md) — the spatial grid, and the level name the geometry's
  archive keys are built from.
- [Memory](MEMORY.md) — level-data arena slots hold full-detail sector
  geometry and the level-LOD1 arena holds reduced-detail geometry; each slot
  count bounds its tier.
- [Resource](RESOURCE.md) — material handles, pinned per resident sector.
- [IO](IO.md) — reading sector geometry.

## Depended on by

- [Renderer](RENDERER.md) — the resident set is the world geometry to draw.
- Game debug overlays — through the per-cell residency query.

## Lifecycle

Begun when a level loads and ended when it unloads; it holds nothing between
levels. Beginning resets the ring and primes it around the initial centre. It
must be updated with the streaming centre each frame. Ending frees every slot.

## When not loaded

The world static geometry is never resident and never drawn; the level core,
entities and materials still load. This is a meaningful configuration for logic
tests and for a headless host that needs entities and collision-relevant data but
draws nothing.

## Failure modes

- **Sector exceeds its arena slot** — refused and logged. The format caps sector
  size and every platform slot is checked against that cap when the engine is
  built, so this is a build-time guarantee rather than a runtime risk.
- **The ring needs a slot and none is empty** — a full-detail sector kept from
  the previous ring position is released to make room, farthest first, so the
  ring is never left short while one exists. The resident count is checked
  against the ring's size when the engine is built.
- **Read failure** — the sector returns to empty. A full-detail cell is
  requested again on the next update; a LOD1 cell is requested again on the next
  recentre or when a slot frees.
- **A completion for a released resident, or after the level was unloaded** —
  dropped without writing anything.
- **A LOD1 sector larger than a LOD1 slot** — refused and logged. LOD1 slots
  are much smaller than full-detail ones on the smaller targets, and the cooker
  does not yet check against them.
- **A sector whose mesh table, geometry spans or offset alignment does not
  hold** — refused and logged naming the mesh and the array; the cell stays
  empty and the rest of the ring is unaffected.
- **A sector declaring more meshes than the ring holds** — refused. The level
  cooker enforces the same ceiling, so this indicates content that did not come
  from it.
- **Centre never updated** — no failure is reported, and the resident set simply
  never moves. This is the most likely integration mistake, and it presents as
  the world ending at an invisible boundary.

## Limits

- The resident count is fixed per platform; the ring is a neighbourhood, not a
  view distance that can be tuned at runtime.
- Meshes per sector are capped by the level format.
- The LOD1 tier's reach is whatever the visibility list names, bounded by the
  LOD1 slot count; a list longer than the slots leaves its farthest cells
  unstreamed.
- The centre cell's visibility list is read directly through the chunk's offset
  table and walked only when the wanted set is recomputed — on a recentre, and on
  the frames after one while loads are outstanding or a slot frees. Every other
  frame the streamer does a fixed scan of the two resident arrays, bounded by the
  platform's slot counts and independent of the grid.
- A resident's generation is eight bits; a completion that stays in flight
  across 256 evictions of the same resident would be taken as current.
- Streaming is horizontal, following the two-dimensional grid in the level
  format.
