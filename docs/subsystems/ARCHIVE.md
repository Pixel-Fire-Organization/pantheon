# Subsystem — Archive

*Codename: Hades*

## Purpose

Serve asset bytes out of one master container file instead of many loose files
on disc. The container's on-disc layout is specified separately in
[formats/ARCHIVE_FORMAT.md](../formats/ARCHIVE_FORMAT.md); this document covers
the runtime read side only.

The motivation is mechanical, not organisational. On optical media a loose-file
layout costs a directory traversal and a long seek per asset. Packing every
asset the game owns — its standalone resources and every level, core and
sectors alike — into one container, in access order, means one seek and one
read per asset, with the drive head barely moving between them, and it means a
level switch touches no file system state at all: the container is already
open, and the only thing that changes is which keys get resolved against it.
See [formats/LEVEL_FORMAT.md](../formats/LEVEL_FORMAT.md) and
[Level](LEVEL.md) for how a level's entries reach this archive and how loading
one no longer opens anything of its own.

## Contract

**Mounting.** A container is mounted from a device path and stays mounted until
dropped. Mounting reads the header, the entry table and the string table into
memory and keeps the file open for streaming; payload bytes are never preloaded.
Mounting is blocking and belongs on a load screen, not in a frame. In the
shipped configuration this happens exactly once, for the master archive, at
engine startup — nothing mounts or unmounts again for the rest of the session,
including across a level load.

**Resolution.** Any form of an asset path — device path, baked dependency path,
canonical key — resolves to the same entry, because both the runtime and the
packing tool agree on one canonicalisation and one name hash. Hash matches are
confirmed by string comparison, so a collision cannot return the wrong asset.

**A second mount, if one exists, shadows the first.** When more than one
container is mounted, the most recently mounted wins on a colliding key. The
shipped configuration mounts only the master archive, so this does not arise
in normal play; the mechanism exists for a caller that deliberately mounts a
second container on top of it (a development overlay, for instance) and wants
its keys preferred with no indirection at the call site.

**Reads are span-bounded.** A read is expressed relative to a resolved entry and
is bounds-checked against it, so a corrupt offset cannot read another asset's
bytes or past the end of the container. Reads take the IO file-access lock and
are therefore safe to issue from the main thread while the IO worker is active.

**The entry table is validated at mount, not trusted at use.** Resolution reads
a key at an offset the container chose, so the container is only mounted once
every one of those offsets has been checked against the string table and the
table has been terminated. See
[formats/ARCHIVE_FORMAT.md](../formats/ARCHIVE_FORMAT.md) for exactly what a
reader must establish. The entry count is likewise sized at a width that cannot
wrap on a 32-bit target: a count that would wrap is a refusal, not a short
table the rest of the mount walks past.

**Mounting and unmounting hold the same lock as reading.** Resolution, mount and
unmount all take the IO file-access lock, and a read re-checks its mount under
that lock rather than before taking it. Without this, resolution walks a table
being freed, and a read that was queued behind an unmount continues with a
closed handle — both reachable by switching scene while a load is in flight,
which is precisely what a loading screen is. A mount is therefore either fully
present or fully absent to every other thread, never half-dropped.

**Duplication is accepted, not automatic.** Two entries may carry the same
payload bytes under different keys; nothing here deduplicates them, because a
content-addressed index would add a lookup layer and a failure mode to save
space that is not scarce on the target media. This is a ceiling, not a floor: a
producer is free to avoid the duplication itself by never writing a second
entry for content that already has one, and naming the existing entry's
canonical key instead. See the level compiler's use of this in
[formats/LEVEL_FORMAT.md](../formats/LEVEL_FORMAT.md), which references a
material already cooked as a standalone resource instead of baking a second,
level-local copy of it into the same archive.

**A mount can be enumerated.** What is mounted, where it was mounted from, how
many entries it holds and what those entries are, are all readable without
reading a payload. Diagnostics need to show an archive that resolution alone
cannot describe: a lookup that misses says only that a key was not found, never
which keys the archive actually contains -- which is the question being asked
whenever a lookup misses. With one archive holding the whole game, this is also
the only place to answer "what did this build actually ship" without reading
the disc's raw sectors.

## Depends on

- [IO](IO.md) — the file-access lock, and the platform file primitives reached
  through it.
- [Memory](MEMORY.md) — entry and string tables are platform allocations held for
  the mount's lifetime, released through the same contract that provided them.

## Depended on by

- [IO](IO.md) — for path-to-span resolution. See the note in the IO spec about
  this deliberate inversion.
- [Resource](RESOURCE.md) and [Level](LEVEL.md) — indirectly; neither addresses
  archives directly, which is the point.

## Lifecycle

Started after IO and before Resource. Startup allocates no containers; the engine
then mounts the master archive (`RASSETS.PS2R`) if one exists — by this point it
carries every standalone resource and every compiled level the build produced,
not only "boot" content, though the constant naming it (`ARCH_BOOT_ARCHIVE_NAME`)
and its mount timing keep the old name. A missing master archive is **not** an
error — standalone resources fall back to loose files on disc, which is how an
unpacked development tree runs; a level still needs *some* archive mounted,
since its core and sectors are never written as loose files (see
[Level](LEVEL.md)). Shutdown unmounts everything, closing files and releasing
tables.

## When not loaded

A standalone resource resolves as a loose file. This is a supported
configuration: it is how content is iterated during development, where
repacking a container per texture change would be intolerable. It is slower on
disc-based hardware and is not how a build ships. A level cannot be loaded in
this configuration — its core and sectors exist only inside a `.PS2R`, never as
loose files — so iterating on level content still needs at least that level's
own intermediate archive mounted (see [Level](LEVEL.md)).

## Failure modes

- **Missing container** — mount returns failure and the caller continues; for the
  master archive this is expected and logged as information, not an error.
- **Bad magic or unsupported version** — mount is refused and logged. The engine
  does not attempt partial recovery: a container it cannot parse may be
  truncated, and reading it would produce corruption attributed to the wrong
  asset.
- **No free mount slot** — mount fails; the slot count is a platform constant and
  exhausting it is a content-structure error.
- **An entry naming a string offset outside the table** — mount is refused
  naming the entry. One wrong entry condemns the table: nothing distinguishes it
  from a table that is wrong about others too.
- **An entry count that cannot be sized on this target** — mount is refused
  rather than allocating what fits and reading what does not.
- **Out-of-range span read** — refused and logged rather than clamped.
- **A read whose mount was dropped while it waited for the file lock** — refused
  rather than issued against a closed handle.

## Limits

- Mount slots are fixed per platform.
- Containers are read-only at runtime; producing one is a build-pipeline stage,
  described in [PIPELINE.md](../PIPELINE.md).
- Entry tables live in memory for the whole mount, so container size is bounded
  by table size, not payload size. **This is the master archive's real ceiling
  on a memory-constrained platform**: because it now carries every level's
  sectors and materials alongside every standalone resource, its entry count
  scales with the whole game, not with whatever is currently in play, and that
  table stays resident for the entire mount — see
  [formats/ARCHIVE_FORMAT.md](../formats/ARCHIVE_FORMAT.md)'s note on the same
  point. A game whose level count or per-level sector count grows large enough
  to make this resident table itself a budget problem needs a different
  mounting strategy than "one archive, mounted once" — that is not what this
  engine does today.
- There is no compression. Payloads are stored as cooked, so a read is a copy
  rather than a decode.
