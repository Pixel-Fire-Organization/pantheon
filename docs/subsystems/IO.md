# Subsystem — IO

## Purpose

Turn a request for a file into bytes, without blocking the frame. IO is the
engine's only path from storage to memory: every asset, level and archive read
passes through it. It is a facade — the portable request machinery lives here,
while opening, seeking and reading are platform primitives.

## Contract

**Asynchronous reads.** A caller enqueues a path with a callback and continues.
The request is serviced on a worker thread; the callback runs later on the main
thread, during the engine's IO pump, never from the worker. Callers therefore
need no locking of their own.

**A single shared read buffer.** All reads land in one static buffer, not one per
request. Exactly one request occupies it at a time, enforced by a binary
semaphore. The callback receives a pointer into that buffer, valid **only for the
duration of the callback** — a caller that needs the bytes afterwards must copy
them. The buffer is released once the callback returns and the request slot is
idle, so the worker cannot overwrite bytes a callback still holds.

This is deliberate, and the alternative was measured: a per-slot buffer design
multiplies the buffer count by the request-queue depth and pushes static storage
past what a constrained platform can map, faulting during startup zeroing before
any engine code runs. Serialising through one buffer costs throughput only in
theory — on disc-based hardware the drive dominates latency.

**A dispatch state that blocks re-queueing.** A request stays in a dispatching
state for the whole callback, so it cannot be recycled while a pointer to its
data is live. Together with the buffer semaphore these are two independent
guards; both are required.

**A file-access lock.** Shared between the worker and any main-thread read
(archives read synchronously), so the two never touch platform file state
concurrently.

**The archive seam.** Path-to-bytes resolution happens here and in the resource
header peek, and nowhere else. A path resolves either to a span inside a mounted
archive or to a loose file. This is what lets archives exist without any caller
knowing they do.

## Depends on

- **Platform** — worker thread and semaphore creation, and the synchronous file
  primitives (build path, open, seek, read, size, close).
- [Memory](MEMORY.md) — request metadata is pool-allocated; the shared read
  buffer is static.
- [Archive](ARCHIVE.md) — consulted to resolve a path to a span. This is the one
  place the layering is deliberately inverted: IO is lower-level than Archive but
  asks it for resolution, because the alternative is duplicating the request
  machinery inside every storage backend.

## Depended on by

- [Resource](RESOURCE.md) — every asset load.
- [Level](LEVEL.md) and [Sector](SECTOR.md) — level cores and streamed sectors.

## Lifecycle

Started early: after memory, before Archive and Resource. Starting it spawns the
worker thread and creates its semaphores.

**Shutdown is ordered, and the worker acknowledges it.** Outstanding requests are
drained *before* anything the worker resolves through is torn down — the archive
it resolves every path against, and the memory its reads land in. Only then is
the worker asked to stop, and shutdown waits for the worker's own
acknowledgement before releasing the semaphores. That acknowledgement is not a
join: one supported platform cannot join a thread at all and another closes the
handle without waiting, so the signal has to come from the worker itself. A
worker that never acknowledges leaves the semaphores allocated and reports it —
leaking at process exit is preferable to freeing state a live thread still
reads.

**A runtime reset drains, and a drain that gives up is fatal there.** A reset
resets the arenas a read lands in and remounts the archive a read resolves
through, so it cannot proceed with one in flight. At shutdown the same failure is
reported rather than fatal: the process is leaving either way, and the worker is
stopped immediately afterwards.

The engine must pump IO once per frame. Without that pump, reads complete on the
worker but no callback ever fires, and every caller waits forever.

## When not loaded

Nothing can be loaded from storage. Resource, Level and Sector all depend on IO
and cannot be requested without it — the engine refuses such a configuration at
startup rather than failing later. A configuration without IO is only meaningful
for a game whose content is entirely generated in memory.

## Failure modes

- **File larger than the shared buffer** — rejected immediately, before any read.
  The callback receives an empty result and a logged error naming the path and
  both sizes. This is a bounded-memory guarantee, not a transient failure: the
  same file will always be too large, so it is a content or budget error.
- **File missing or unreadable** — callback receives an empty result; the caller
  decides whether that is fatal. The engine logs the resolved path, including
  which archive was consulted, because a missing-asset report that omits the
  resolved path is nearly useless.
- **Request queue full** — enqueue fails and returns false; the caller retries on
  a later frame.
- **The worker is never scheduled** — reads still complete, but at the rate the
  main thread happens to yield, which can be orders of magnitude slower than the
  medium. Nothing fails and nothing is logged: assets simply arrive late, and the
  symptom is a world that renders untextured for several seconds and then
  corrects itself. A platform whose scheduler does not time-slice between
  priorities must place the worker **above** the main thread, because a frame
  loop that spins on display hardware may not yield at all. This has happened,
  and the read rate it produced was one disc sector per second — set by how often
  the main thread paused to log.
- **Callback never returns** — deadlocks all IO, since the buffer semaphore is
  held. Callbacks must not block, and must not enqueue a read of their own and
  wait for it.
- **A drain that gives up with requests outstanding** — reported, and fatal at a
  runtime reset, where continuing would release memory a read is still writing
  into. The drain bound is a platform constant.
- **A worker that does not acknowledge shutdown** — reported; the semaphores are
  deliberately left allocated rather than freed under it.

## Limits

- One in-flight read reaches memory at a time; the queue holds requests, not
  concurrent transfers.
- Maximum readable file size equals the shared buffer size, which is
  platform-defined. See the platform specs for per-platform values.
- Callback data is borrowed, never owned. Nothing may retain the pointer.
- Read-only. There is no asynchronous write path; platforms that support writing
  expose it only as a synchronous primitive.
