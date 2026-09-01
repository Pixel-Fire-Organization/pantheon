# Subsystem — Memory

## Purpose

Provide every other subsystem with storage whose cost is known before the game
runs. The engine targets hardware where allocation failure is fatal and
fragmentation is unrecoverable, so memory is reserved once at startup from a
platform-owned budget and handed out through three models, each chosen to make a
different access pattern O(1) and fragmentation-free.

The engine never calls the C allocator directly. All memory originates from the
platform's memory contract, which owns the budget and the alignment rules for
that hardware.

## Contract

**Reservation.** At startup the subsystem asks the platform to reserve the engine
memory map: one aligned arena block and one pool block, sized from that
platform's constants. The platform enforces its own ceiling and refuses a
reservation it cannot honour. Reservation happens before any renderer exists,
because renderers take their geometry staging from an arena slot as they are
constructed.

**Three allocation models**, each with a distinct guarantee:

| Model | Shape | Freeing | Fragmentation | Intended for |
|---|---|---|---|---|
| Segmented arenas | Fixed slots inside named segments, linear fill within a slot | Per slot or per segment only; never per allocation | None by construction | Long-lived engine state: config, level cores, streamed sectors, renderer scratch |
| Main pool | Fixed-size chunks on a free list | O(1) per chunk | None — every chunk is identical | Small, short-lived scratch: IO metadata, decode contexts, transient game objects |
| Platform allocations | Arbitrary size and alignment through the memory contract | Explicit, and **must** pair with the contract's own release | Possible — used sparingly | Sized-at-load buffers that outlive a frame: archive tables, model geometry |

**Arena segments** are named, fixed-capacity, and partitioned into equal slots.
Writing to a slot overwrites it; there is no per-asset free. A slot's start
address is aligned to the platform's slot alignment so hardware DMA paths can
consume it directly.

**Allocator pairing is part of the contract.** Memory taken from the platform
contract is released through the platform contract; memory from the C allocator
is released through the C allocator. These are not interchangeable — on some
platforms aligned allocations come from a separate heap and releasing one with
the wrong call corrupts it. Ownership types express this pairing so it cannot be
written incorrectly.

## Depends on

- **Platform** — for the memory contract: reservation, aligned allocation and
  release, heap statistics, and the budget ceiling. Per-platform budgets are in
  the platform specs: [PS2](../ps2/PLATFORM.md), [Win32](../win32/PLATFORM.md).

## Depended on by

- [Renderer](RENDERER.md) — geometry staging comes from the renderer arena segment.
- [Resource](RESOURCE.md) — decoded assets and the handle table.
- [Level](LEVEL.md) and [Sector](SECTOR.md) — level cores and streamed sectors
  occupy level-data slots.
- [IO](IO.md) — request metadata comes from the pool.
- [Archive](ARCHIVE.md) — mounted archive tables are platform allocations.

## Lifecycle

Reserved before every other subsystem and before any renderer, because
construction of a renderer already depends on it. Released last, after every
subsystem that borrowed from it has shut down. It has no partial state: either
the whole map is reserved or startup fails.

**Emptying waits for storage to go quiet first.** Reads run on their own thread
and their callbacks write into resource slots, so a reset that began while one
was in flight would hand the worker memory it had just taken back. The wait is
part of the reset, not something a caller remembers to do; it is bounded, and
giving up is reported rather than retried forever.

**The map is reserved once; its contents can be emptied many times.** Returning
the engine to a just-started state does not re-reserve anything. Each arena
segment goes back to a zero bump offset with every slot emptied and unlocked,
and the main pool is rebuilt over the block it already owns. Slot addresses and
capacities survive, so anything holding one still holds a valid one.

**The renderer segment is excluded from that, and the exclusion is load-bearing.**
A backend takes its geometry staging out of the renderer segment while it is
being constructed, and it is constructed once for the life of the process —
one backend in the engine offers no teardown entry point at all, so it could not
be rebuilt even if the engine wanted to. Emptying that segment would leave a
live renderer staging into memory the map has handed back. Emptying the other
segments is safe precisely because nothing survives across the reset holding
pointers into them.

## When not loaded

Not applicable — memory cannot be disabled. It is not part of the game's
selectable subsystem list; it is a precondition of the engine existing at all. A
failed reservation is a startup panic, not a degraded mode.

## Failure modes

- **Reservation exceeds the platform budget** — panic at startup naming the
  requested and available sizes. The engine does not start in a reduced
  configuration; a budget overrun is a build-time mistake to fix, not a runtime
  condition to survive.
- **Slot overflow** — a write larger than the slot is **refused**, never
  truncated: a truncated level core produces corruption far from its cause. The
  subsystem attempting it checks the slot capacity first and reports the failure
  with the names it has — the level or sector, and both sizes — because the
  arena knows only indices and could not produce a message worth reading.
- **Pool exhaustion** — allocation returns nothing and is logged. Callers are
  scratch users that can fail a frame's work without corrupting state, so this
  is recoverable rather than fatal.
- **Cross-allocator release** — releasing platform memory through the C allocator
  is undefined behaviour on at least one supported platform. Prevented by
  construction rather than detected.

## Limits

- Slots are fixed capacity: the largest storable item is one slot, and a format
  whose maximum size exceeds a platform's slot capacity is rejected at compile
  time rather than at load.
- Arena memory is never returned to the platform during play; segments are reset
  wholesale, typically at a level transition.
- Pool chunks are a single fixed size, so an allocation larger than one chunk is
  not expressible — such callers belong in an arena or a platform allocation.
- Heap statistics are best-effort and platform-defined; some platforms cannot
  report a meaningful used-versus-free split.
