# Subsystem — Sector

*Codename: Atlas*

## Purpose

Keep the geometry near the streaming centre resident, and nothing else. A world
is larger than the memory budget of every platform the engine targets, so the
engine holds a small neighbourhood of it and moves that neighbourhood as the
centre moves.

## Contract

**A fixed ring of residents.** A small, constant number of grid cells around the
centre are resident at once. The count is fixed per platform and does not grow
with world size — that is the property that makes an arbitrarily large world fit
a fixed budget.

**A second, reduced-detail tier covers what the ring does not.** Beyond the
full-detail ring, the level's visibility list for the centre's cell names the
cells whose reduced-detail (LOD1) sectors should be resident. They occupy their
own arena and their own fixed-capacity array, so neither tier can starve the
other. A LOD1 sector is never streamed for a cell inside the full-detail ring.
A few are requested per frame, so the tier fills over several frames rather
than in one burst; when the centre's cell changes, those the new list no longer
names are evicted.

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
- **Slot exhaustion during recentre** — the incoming sector is skipped and
  logged; the ring stays partially populated and geometry is missing rather than
  wrong.
- **Read failure** — the sector returns to empty and is retried on a later
  recentre (full detail) or a later frame (LOD1).
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
- The streamer walks the visibility chunk from its start every frame to reach
  the centre cell's list, so its per-frame cost grows with the grid.
- A resident's generation is eight bits; a completion that stays in flight
  across 256 evictions of the same resident would be taken as current.
- Streaming is horizontal, following the two-dimensional grid in the level
  format.
