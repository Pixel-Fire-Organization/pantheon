# Subsystem — Resource

## Purpose

Own the lifetime of everything loaded from an asset file: textures, models, and
their dependencies. Callers hold small integer handles rather than pointers, so
what sits behind a handle can be relocated without the game noticing, and so a
handle can be stored in a cooked asset's material or a level's material list
without any of them owning a pointer.

A handle stays valid until the programmer releases it, and nothing else releases
it for them. That is the whole reason a stale handle cannot alias another asset:
the only way a slot changes owner is an explicit unload.

The cooked asset container format is specified in
[formats/ASSET_FORMAT.md](../formats/ASSET_FORMAT.md).

## Contract

**Handles, not pointers.** A load returns a handle immediately, before the data
exists. The handle is valid from that moment; the *data* is not. Callers ask
whether a handle is ready and fetch the underlying object only then. A fetch on a
handle that is not ready yields nothing rather than a partially-decoded object.

**A fixed handle table.** Capacity is a platform constant. There is no growth
path: an engine that silently doubles its table hides a content problem until it
fails on the smallest target.

**Asynchronous by default.** Every type streams through [IO](IO.md) and decodes
from memory. Nothing blocks the frame that requested it.

**Declared dependencies load first.** An asset names its dependencies in its own
header, and they are loaded before it is reported ready. A model is never
ready before the textures it references. This is what makes a single load request
sufficient for a whole object graph.

**Reference counting and pinning, and no eviction at all.** A dependency's
reference count rises with each dependent. Pinning marks an entry the game
cannot tolerate losing mid-frame — HUD fonts, persistent UI. Nothing is ever
reclaimed automatically: a full table is an error the caller resolves with an
explicit unload, exactly as an over-budget texture is. This is the resource
management philosophy in `.github/copilot-instructions.md` — no silent eviction,
no implicit LRU — applied to the handle table and not only to the texture
budget.

The alternative was implemented and is the reason this paragraph is explicit.
Reusing the least recently used slot for a new asset leaves every handle still
naming that slot pointing at whatever moved in: a texture where a model was
expected, or another level's material, reported as ready. Handles carry no
generation, so nothing downstream can tell. Recency is still tracked, but only
so a full table can name the entry that has gone unused longest and make the
error actionable.

**An in-flight load can be cancelled, and its completion is discarded.**
Unloading an entry that is still streaming does not wait for the read. The slot's
generation advances, and the read's completion recognises that the slot is no
longer its own and drops the result rather than writing a decoded asset — and a
texture handle — into a slot that is now empty or belongs to something else.

**Type may be declared or inferred.** A caller that knows the type states it; one
that does not — a level's required-resource list, which may hold anything — lets
the header decide. Both paths converge before decoding.

**Texture budget is accounted in bytes; the platform defines the cost and the
renderer defines the ceiling.** What
a texture occupies is hardware-specific: one platform rounds to page granularity
in a fixed video memory region, another simply consumes heap. The subsystem asks
the platform what a given texture costs and compares the total against a
platform-supplied ceiling. Byte accounting is what makes that comparison mean the
same thing everywhere.

**The table can be enumerated.** Every live slot, its key, type, state,
reference count, pinned flag and footprint are readable. A budget figure says
how much is spent; only the table says on what, which is the difference between
knowing a load was refused and knowing what to release.

## Depends on

- [IO](IO.md) — all asset reads.
- [Memory](MEMORY.md) — decoded payload storage and load contexts.
- [Renderer](RENDERER.md) — texture upload; a decoded texture is not usable until
  the active backend has accepted it.
- **Platform** — texture footprint accounting and the budget ceiling.

## Depended on by

- [Level](LEVEL.md) — level required-resource lists.
- [Sector](SECTOR.md) — sector materials.
- [Scene](SCENE.md) — a scene's declared resource list, and the built-in
  loading screen's own images.
- Game code, through the public game API.

## Lifecycle

Started after IO and Archive, since it needs both to resolve and read. It must be
updated once per frame to advance the frame counter that records recency; without
that update, a full-table error cannot name a useful candidate to release.
Shutdown force-unloads everything, pinned entries included, and advances every
slot's generation so a read still in flight cannot land in one of them.

## When not loaded

No asset can be loaded. A game in this configuration must generate its content
procedurally. Level and Sector both depend on Resource and cannot be requested
without it.

## Failure modes

- **Table full** — load fails and returns an invalid handle, naming the entry
  that is unpinned, unreferenced and least recently used so the caller knows what
  to release. The engine makes no room for itself; doing so would trade a clear
  failure for a handle that silently names the wrong asset.
- **Budget exceeded** — reported as an actionable error naming the asset and the
  overage. There is no silent downscaling: an engine that quietly halves a
  texture makes the eventual overflow harder to attribute than the error would
  have been.
- **Missing dependency** — the dependent fails to become ready and logs which
  dependency was missing and what referenced it.
- **Bad magic or unsupported version** — the load is refused rather than
  reinterpreted.
- **Unsupported type** — sound is not implemented on any platform; a
  request logs an error and fails immediately rather than returning a handle that
  will never become ready.

## Limits

- Handle table capacity, and the texture budget, are fixed per platform.
- Dependency count per asset is fixed by the asset format.
- Models decode from a baked, unindexed representation; there is no runtime mesh
  optimisation or index generation.
- Sound is unimplemented across the engine, not merely on one platform.
- A theme is validated at decode rather than at use, because it is copied into
  live state rather than read field by field. A theme that fails any check never
  becomes ready, so a caller cannot apply half of one.
- A font is metrics only. Its atlas is an ordinary texture named as its
  dependency, so it is budgeted, uploaded and released by the texture path
  rather than by a second one, and a font is never ready before its atlas is.
- There is no reload path. A handle names a slot for as long as the caller holds
  it, and releasing it is the caller's act.
- A decoded theme carries its style block *and* its font keys, because a theme is
  both. Nothing in it reaches the interface until a caller applies it, so a theme
  that is only inspected — the Testbed's asset browser previews them — changes
  nothing.
