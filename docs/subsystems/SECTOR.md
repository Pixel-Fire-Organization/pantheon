# Subsystem — Sector

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

**Textures resolve per mesh.** Each resident mesh carries a resolved texture
handle from the level material table, so drawing needs no further lookup.

## Depends on

- [Level](LEVEL.md) — the spatial grid, and the level name the geometry's
  archive keys are built from.
- [Memory](MEMORY.md) — level-data arena slots hold sector geometry; the slot
  count bounds the ring.
- [Resource](RESOURCE.md) — material texture handles.
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
- **Read failure** — the sector stays not-ready and is retried on a later
  recentre.
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
- Recentring is currently synchronous: crossing a boundary reads the new cells
  before the frame completes, which is visible as a hitch on slow media. The
  design admits an asynchronous replacement without changing this contract, since
  the loading state already exists and consumers already skip non-ready sectors.
- Streaming is horizontal, following the two-dimensional grid in the level
  format.
