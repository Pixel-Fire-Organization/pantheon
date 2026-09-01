# Guideline — secure coding

Rules for the code in this engine that can be made to misbehave by what it is
*given* rather than by what it *is*: parsers over bytes read off disc, work
that completes on a later frame or another thread, teardown paths, buffers
handed to an OS API, and the build and CI scripts that assemble a shipping
container.

Every rule here was broken in this repository first. The security pass of
2026-09-16 found ten classes of defect, `EX-0005` through `EX-0015` in
[fixed_issues/issues.json](../fixed_issues/issues.json), and
[backlog/security_findings.md](../backlog/security_findings.md) is the audit
record — the trust boundary, what was examined and found sound, what was
accepted as a known gap. Each section below names the entries it comes from,
so the reasoning can be read in full rather than trusted.

Read this **before** writing a reader, a callback, a shutdown path, a platform
file operation or a workflow — not while reviewing one. The companion,
[SECURITY_REVIEW.md](SECURITY_REVIEW.md), is how to check that the tree obeys
these rules.

---

## 1. Know what is untrusted

On a console the threat is not a remote attacker; it is a corrupt disc, a
modified archive, a repacked image, a loose-file dev build with a stale asset,
or a save written by a previous version. The consequence of trusting any of
them is the same as it would be for an attacker: an out-of-bounds read, and on
the PS2 a DMA from an arbitrary address. The fixes therefore refuse and report;
they do not sanitise.

Treat as untrusted, always:

| Input | Comes from | Read by |
|---|---|---|
| Archive header, entry table, string table | The container on disc | Archive |
| Asset header and every payload — texture, model, font, theme | Cooked assets on disc | Resource, and each format's parser |
| Level core chunks and sector blobs | The level in the archive | Level, Sector |
| Achievement record, action overlay | Writable storage; the only inputs a player can edit by hand | Achievement, Action |
| Command line | The launcher | Startup |
| Text from the OS — key characters, dialog results | The platform input backend | Platform, UI |
| Anything a CI job reads from the event that triggered it | GitHub | Workflows |

A number read from any of these is an *input*, not a *fact*. It becomes a fact
only after the check that makes it one, and that check lives in the reader,
never in the consumer.

## 2. Reading bytes off disc

*From `EX-0005` (archive), `EX-0006` (level, sector), `EX-0007` (model,
texture), `EX-0012` (font), `EX-0015` (far field).*

**Establish, then publish.** A reader validates every offset, count and size
in a blob against the blob's own size before any pointer into it leaves the
reader. It stages what it decodes and publishes once, at the end. A caller that
receives a view receives a bounded one; nothing downstream re-derives a bound,
because nothing downstream has the information to.

**Refuse whole; never clamp.** A blob that fails one check is refused
entirely, with a message naming the field and the value. A count clamped into
range still indexes memory that belongs to something else, and an entry table
that is wrong about one entry is not trustworthy about the rest. This is the
resource-management philosophy — no silent eviction, no silent downscaling —
applied to bytes.

**Widen before you add or multiply.** `size_t` is 32 bits on three of the four
platforms. Any `offset + length` or `count * width` where either operand came
off disc is computed in `uint64_t` first and compared afterwards. A vertex
count near 2^28 multiplied by 16 wraps to a small number that passes every
32-bit comparison, and a 64-bit host never shows it. The level span helper in
`engine/include/level/` is the shape every such check takes.

**Check at the width you keep, not the width you read.** A 16-bit field stored
into a byte is checked against the byte's range. A line height of 256 passed a
non-zero check and was stored as zero.

**Alignment is a bound too.** A struct cast over disc bytes needs the offset
aligned for the widest field in the struct; a 16-bit view over odd bytes traps
on both MIPS targets, and a DMA source on the PS2 needs 16. The cooker writes
aligned offsets, so the reader may require them. Check with the alignment
helper, not by inspection.

**Strings are terminated by the reader, and every string offset is checked
against its table.** A key read out of a fixed-width field, or found at an
offset in a string table, is a run of bytes until the reader writes the
terminator at the last byte and confirms the offset is inside the table. Do
both before the first string comparison or copy.

**Counts are checked against ceilings before anything is sized from them**,
and the ceiling is a format constant the cooker enforces too. Content that the
runtime would refuse must never come out of the cook: the cooker raises where
the runtime refuses, so the refusal is a build error rather than a black
screen on hardware.

**Allocation from a count goes through the overflow-guarded path.** The
platform array helper returns empty when the byte size would wrap; a raw
`count * sizeof(T)` handed to the allocator does not.

**Every published view is validated, including the ones nothing reads yet.**
A view that exists will be consumed, and its first consumer inherits whatever
the publisher established. The far-field chunk was span-checked and published
raw for a consumer that did not exist; when one arrived it would have started
from an unbounded pointer.

**Each format spec carries the list.** `docs/formats/<NAME>.md` has a "what a
reader must establish" section. A new check is added to it in the same change,
so the next reader of the format — or the next port of the parser — starts
from the list rather than from the code.

## 3. Work that completes later

*From `EX-0008` (async load completion), `EX-0010` (slot reuse).*

**Anything that completes later carries a generation of what it will write
to.** A callback that indexes a table by a slot number it was given at queue
time checks, on completion, that the slot is still in the state it was left in
*and* that its generation has not moved. Either mismatch means the slot was
released — and possibly reoccupied — while the work was in flight; the result
is discarded, not written. The dependency mechanism had this from the start;
the load context did not, and a texture id could be released by the wrong
owner.

**Advance the generation on every release path.** An unload, a force-unload of
everything, a failed decode: each is a release, and each advances the
generation of the slot before the slot is cleared. One path that forgets is
the hole a stale completion walks through.

**A slot is never reused behind a live handle.** Public handles carry no
generation, so nothing downstream can tell a reused slot from the original. A
full table is an error naming the stalest releasable entry; it is never
resolved by evicting one. This is the rule the texture budget already had, and
the spec that said otherwise was the bug.

**A call that must apply-or-refuse by the time it returns drains; it does not
rely on a stale completion.** The theme loader depended on its own cancelled
load being resurrected by the callback it should have discarded. If a caller
needs the result now, wait for it explicitly.

## 4. Two threads and one table

*From `EX-0009` (IO worker against teardown).*

**A table read on the worker is read under the lock that writes it.** The IO
worker resolves every request through the mount table; every path that reads,
mounts or unmounts a slot takes the file-access semaphore, and the lock covers
the whole operation — not just the OS call in the middle. Unmount held the
lock around the file close and freed the entry table outside it.

**Re-read state inside the lock, not before it.** A check made before taking
the lock is a check against a state that may be gone by the time the lock is
held. The sync read passed its in-use check, blocked on the semaphore behind
an unmount, and continued with a closed handle.

**Teardown order: drain, stop the worker, wait for it, release what it read,
then free memory.** Every shutdown path — process exit and runtime reset alike
— drains outstanding requests first, stops the worker, and waits for the
acknowledgement the worker itself gives (bounded), because one platform cannot
join a thread and another closes the handle without waiting. Only then is the
archive dropped and the arenas released.

**A worker that will not stop leaves its state allocated.** Leaking a
semaphore at process exit is harmless; freeing one a live thread is about to
wait on is not. Report it and return.

**A drain that fails before a reset is a panic, not a warning.** A runtime
reset releases the arenas a read lands in and remounts the archive it resolves
through; continuing past a failed drain runs the worker over freed state.
Continuing past the same failure at process exit is acceptable only because
the worker is stopped next either way.

## 5. Validate everything, then commit once

*From `EX-0011` (theme fonts).*

**Decode into the caller's object; touch no live state.** A decoder that
writes part of its result into a global as it goes — because that global
happens to be where the value lives — installs the fields of a refused
payload, and installs them for a payload that was only ever decoded to be
inspected. Every field a payload carries is part of the staged result, and the
caller commits all of it after every check has passed, or none of it.

**"Refused changes nothing" is a spec-level guarantee.** If a spec says a
refused theme leaves the interface as it was, the code makes that true for
every field of the theme, including the ones added later.

## 6. Buffers handed to an OS API

*From `EX-0013` (Win32 shell folder path).*

**The buffer size comes from the contract of the API, not from an engine
constant that is numerically close.** The shell API documents its own path
length; the engine path limit is a format constant with a different purpose.
That they differ by four bytes is why the overrun was small; that they differ
at all is why it existed.

**This is platform code, and platform code is where the rules of the OS
apply.** Shared engine code never sees these buffers. A platform file
operation that takes one reads the documentation of the API for its size and
its termination behaviour, and states both in the platform spec if they are
surprising.

## 7. Tools, build scripts and CI

*From `EX-0014` (supply chain).*

**Subprocesses take an argument vector, never a shell string.** Where a shell
is unavoidable — the WSL relay — every interpolated value is quoted with
`shlex.quote`, *and* any command substitution around a quoted value is itself
quoted, or the output of the substitution is word-split. Quoting the input to
the path converter and not the `$(...)` around it protected against a quote in
the path and still broke on a space.

**No `eval`, no `exec`, on anything that came from a file.** A regex-extracted
expression from a header is a string from a file. Parse the shape you expect —
a product of integer literals — and refuse anything else.

**Pin everything CI fetches.** Actions by commit SHA with the tag kept as a
trailing comment; containers by digest; Python packages by exact version in a
requirements file. A tag is mutable, a major-version tag is a moving target,
and the `v2` of one third-party action was a branch. What cannot be pinned —
OS packages from a distribution repository — is recorded as an accepted gap in
the backlog with the reason, never left silent.

**Least privilege by default.** Every workflow has a top-level `permissions`
block granting `contents: read`; a job that needs more declares its own. A job
without one runs with the default token scope, and a third-party action inside
it runs with that scope too.

**Never expand an event field into a `run:` step.** Branch names, PR titles,
commit messages and issue bodies are attacker-controlled text. Passing one
through an environment variable is safe; expanding it into the script is
injection.

## 8. Report the refusal

A guard that returns `false` silently is indistinguishable from success at a
call site that ignores the return. Every refusal in this guideline logs an
error naming the input, the field, the value and the bound it violated —
"chunk 3 spans 4096..4128, past the 4112-byte core" — because the next person
to see it is a content author with a corrupt file, not the author of the
check.

## Definition of done

A change touching any of the areas above is complete when:

- [ ] Every offset, count and size read from an input is checked against the
      size of the input before any pointer into it is published, at a width
      that cannot wrap a 32-bit `size_t`, and against the alignment a view at
      it needs
- [ ] Every value is checked at the width it is stored at
- [ ] Every string read from an input is terminated by the reader, and every
      string offset is bounded against its table
- [ ] Every ceiling the reader enforces is a format constant the cooker
      enforces too
- [ ] The "what a reader must establish" section of the format spec lists
      every check, and the failure modes of the subsystem spec list every
      refusal
- [ ] Anything that completes later carries a generation and discards on
      mismatch; every release path advances the generation
- [ ] Every table the IO worker reads is read under the lock that writes it,
      the lock covers the whole operation, and state is re-read inside it
- [ ] Every shutdown and reset path drains, stops the worker, waits for its
      acknowledgement, then releases
- [ ] A decoder stages into the object of the caller and the caller commits
      once
- [ ] Every OS buffer is sized from the contract of the API
- [ ] Every subprocess is an argument vector; every unavoidable shell string
      quotes its values and its substitutions; nothing is `eval`ed
- [ ] Every action, container and package a workflow fetches is pinned, and
      every workflow has a least-privilege `permissions` block
- [ ] Every refusal is logged naming the input, the field, the value and the
      bound
- [ ] `docs/backlog/security_findings.md` records anything examined and found
      sound, and anything accepted as a known gap with its reason
