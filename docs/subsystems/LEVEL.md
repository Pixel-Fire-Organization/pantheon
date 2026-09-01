# Subsystem — Level

## Purpose

Bring a compiled world into memory and keep it coherent: its description, its
materials, its entities, and the streaming origin that decides which parts of its
geometry are resident. Level owns everything about a world that is small and
always needed; [Sector](SECTOR.md) owns the part that is large and needed only
nearby.

The compiled on-disc form is specified in
[formats/LEVEL_FORMAT.md](../formats/LEVEL_FORMAT.md).

## Contract

**A level's entries live in the master archive**, alongside every standalone
resource and every other level — there is no per-level container to open.
Loading reads its core by name into resident arena storage and works entirely
from those chunk views thereafter; unloading just clears the arena. Nothing is
mounted or unmounted for a level switch, because nothing was mounted for this
level in the first place — see [Archive](ARCHIVE.md).

**The core is resident; geometry is not.** The core holds what must always be
addressable: level info, the material table, the spatial grid, entity records,
and the optional far-field description. It is read once and kept. Sector geometry
is streamed and may be absent for most of the world at any moment.

**Chunk pointers are views, not copies.** The descriptor exposes pointers
directly into the arena slot holding the core. Nothing is duplicated, and the
descriptor contents are valid exactly as long as the level is loaded.

**The core is validated whole before any view is published.** Because the views
are casts into bytes read off disc, every offset and length the core declares is
checked against the core's own size, and every chunk offset against the
alignment a struct view at it requires — an unaligned word access traps outright
on both MIPS targets, so a one-byte shift is a crash rather than a bad read.
Values the rest of the subsystem divides or indexes by are checked in the same
pass: a zero or non-finite cell size, an empty grid, a grid chunk too small for
the cell count it declares, a material table too small for the material count,
every entity record, property index and string offset in the entity chunk, and
every atlas, cluster and frame reference in the far-field chunk.
Fixed-width key fields are terminated as part of that pass, so a key that
reaches a loader is a string rather than a run of bytes. A core that fails any
of this is refused whole and the level does not load; nothing is clamped into
range, because a clamped offset still reads bytes that belong to something else.

**Materials are requested and pinned on load.** A level requests its material
textures and pins the resulting handles as part of loading, marking them as
content the world cannot lose while it is current. The pixel data itself streams in over
the following frames and is resolved at draw time, exactly as models are — a
level can therefore be current before all of its textures are resident.
Unloading unpins them, optionally retaining pins across a transition when the
next level shares materials.

**Entities are spawned through the game.** The engine reads entity records and
hands them to a spawn handler the game registers. The engine has no entity model
of its own; it transports records and lets the game decide what they become.

**Loading is blocking.** It belongs on a load screen. This is deliberate: a level
load touches every subsystem at once, and interleaving it with a running frame
would mean every subsystem tolerating a half-built world.

**One level at a time.** There is a single current level. Cross-fading two worlds
is not supported.

## Depends on

- [Archive](ARCHIVE.md) — the master archive a level's entries are read from.
- [Resource](RESOURCE.md) — material textures.
- [Memory](MEMORY.md) — level-data arena slots hold the core.
- [Sector](SECTOR.md) — primed on load, released on unload.

## Depended on by

- [Sector](SECTOR.md) — reads the grid and the archive keys from the current level.
- [Renderer](RENDERER.md) — draws the current level resident sectors.
- [Scene](SCENE.md) — a `LevelScene`'s fixed pipeline loads, streams and draws
  the current level directly; game code no longer needs to call this
  subsystem's own entry points once it is scene-based.
- Game code that is not scene-based, for entity spawning and streaming centre
  updates through `game::LoadLevel`/`SetStreamingCenter` directly.

## Lifecycle

Started after Resource. It holds no level until asked. The streaming centre must
be updated each frame with the position that should be surrounded by resident
geometry — normally the camera or player. Without that update the resident set
never moves and geometry ends where it was when the level loaded. Unloading
releases sectors, unpins materials, and clears the arena, in that order. The
master archive stays mounted throughout — nothing about unloading touches it.

**A runtime reset unloads the current level as part of it**, the same as it
tears down Resource and Archive — the renderer reads the current level and its
resident sectors directly, independent of whatever is calling this subsystem,
so a level left current across a reset would keep drawing behind whatever the
reset was for even though nothing asked for it any more.

## When not loaded

No world can be loaded. Sector depends on Level and cannot be requested without
it. Games that build their scenes procedurally, and tools that only need asset
loading, run in this configuration.

## Failure modes

- **Core not found in the mounted archive** — load fails and the caller stays on
  its load screen; the engine does not enter a half-loaded state. This is what a
  missing master archive, or a level whose entries were never packed into it,
  looks like.
- **Core exceeds its arena slot** — refused. Slot capacity versus the format
  maximum core size is checked when the engine is built, so this indicates a
  platform whose budget cannot host the content, and it surfaces at build time
  rather than on the target.
- **A core whose chunk table, chunk span or chunk alignment does not hold** —
  refused naming the chunk and what failed. The level does not load and the
  descriptor is left empty.
- **A core missing a chunk another chunk's counts require** — a material table
  absent while the info chunk declares materials, a grid smaller than its own
  cell count — refused for the same reason.
- **A core declaring more materials than the engine table holds** — refused. The
  level cooker enforces the same ceiling, so this indicates content that did not
  come from it.
- **An entity chunk whose records, properties or string offsets leave it** —
  refused before any entity is spawned, rather than part-way through the world.
- **A far-field chunk whose atlases, clusters or frames point outside it or
  outside the material table** — refused the same way, although nothing draws
  the far field yet: the view is bounded where it is published, not where it is
  first consumed.
- **Material texture fails to load** — logged with the material and the level;
  the level still loads, and geometry using that material draws untextured. A
  missing texture is a content error that should be visible, not fatal.
- **No spawn handler registered** — each entity record is read, logged as an
  error naming its class, and discarded. The world geometry is still correct,
  which makes the symptom legible: the level renders and is empty.

## Limits

- One level resident at a time.
- Material count, entity property count and far-field extent are fixed by the
  level format and identical on every platform.
- The core is read synchronously and entirely; there is no partial core.
- The streaming centre is two-dimensional. Worlds are streamed across a
  horizontal plane, so tall vertically-stacked worlds gain nothing from
  streaming.
