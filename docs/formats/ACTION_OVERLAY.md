# Format — Action overlay record (`actions.dat`)

A player's rebindings: a sparse list of source replacements against the compiled
action map, written to the title's own writable location. It is a record rather
than an asset — nothing cooks it, the resource manager never sees it, and it is
read once at startup and rewritten when the player changes a control.

The consumer and its guarantees are [subsystems/ACTION.md](../subsystems/ACTION.md).
The writable location is the platform's, built from the title declaration, the
same as the [achievement record](ACHIEVEMENT_RECORD.md).

Little-endian throughout, for the same reason every other format in this engine
assumes it: all three targets are little-endian, and a big-endian target would
need a byte-swapping reader rather than the direct read this format is shaped
for.

## The record is sparse, and replaces rather than describes

An entry names one binding of one action and gives the sources that now satisfy
it. Bindings the player never touched have no entry. This matters for two
reasons that are part of the contract.

**A full map would be a second copy of the declaration**, and the two would
diverge the first time the declaration changed — the reader would then have to
decide which one was authoritative for an action present in only one of them.
Sparse replacement has no such case: an entry either names a binding that still
exists or it does not.

**The record cannot introduce anything.** It replaces the sources of a declared
binding and can do nothing else: no new action, no new binding, no new context.
A record is therefore never able to make the running map larger than the compiled
one, which is what lets every table above it be sized at compile time.

## The map identity is checked, not assumed

The header carries a digest of the compiled map. A record whose digest does not
match the running map is discarded whole, and the compiled defaults are used.

This is not defensive coding. An action's identifier is its position, and a patch
that adds or reorders actions in the declaration reassigns those positions. A
record applied across that change would silently rebind a control the player never
touched — the failure would appear as a corrupted control scheme with no
plausible cause, long after the patch that produced it. Discarding loses the
player's changes, which is the lesser harm and is reportable.

## Layout

```
+-----------------------------+
| header (16 bytes)           |
+-----------------------------+
| entry 0                     |  entryCount entries, 12 bytes each
| entry 1                     |
| ...                         |
+-----------------------------+
```

### Header

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0 | `magic` | 4 | `"PSAO"` |
| 4 | `version` | 2 | layout version of this record |
| 6 | `entryCount` | 2 | entries following; never above the overlay limit |
| 8 | `mapDigest` | 4 | digest of the compiled map this was written against |
| 12 | `checksum` | 4 | over every entry |

A record whose `entryCount` exceeds the compiled overlay limit is rejected rather
than truncated: a truncated one would apply some of the player's changes and not
others, which is harder to diagnose than none of them.

### Entry

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0 | `action` | 2 | index into the compiled action table |
| 2 | `binding` | 1 | which of that action's candidates this replaces |
| 3 | `sourceCount` | 1 | sources following; all must be satisfied together |
| 4 | `sources` | 2 each | up to the per-binding source limit, zero-padded |

`action` is an index rather than a name because names are not in the running
build: the identifier header is generated, and the strings are an authoring
artefact. The digest is what makes the index safe to store.

An entry whose `action` is past the compiled table, or whose `binding` is past
that action's candidate count, invalidates the record — it is evidence the digest
check was passed by a file that should not have passed it, and applying the rest
would be trusting a record already known to be wrong.

### Source reference

One source in 16 bits:

| Bits | Field | Meaning |
|---|---|---|
| 15..13 | device | which of the four device groups |
| 12..10 | kind | button, stick, trigger, contact |
| 9..0 | code | which control within that group |

**Gamepad buttons are stored by bit position, not by their mask.** The engine's
gamepad button values are a hardware mask running to sixteen bits, which does not
fit the code field and would not be denser if it did. Storing the position keeps
every device group in one uniform reference and keeps the field width honest: a
value that needs sixteen bits in one group and four in another is not one field.
The reader converts on load; the mask is never what reaches disc.

Codes in the other groups are the engine enumerator values directly, which are
dense and well inside the field.

A reference whose device, kind or code has no meaning in the running build
invalidates the record, for the same reason an out-of-range action does. The
digest makes this unreachable in practice, so reaching it is a reader bug or a
damaged file, and neither is a state to continue from.

## What is deliberately absent

- **No names.** Nothing in the record is human-readable, and it is not intended
  to be hand-edited. A control scheme meant to be authored is a change to the
  declaration, not to this file.
- **No per-player sets.** One record per title, not one per port. Adding them
  would be a header change and an entry key change, and is listed as a limit in
  the subsystem spec rather than reserved for here.
- **No shaping or timing.** Deadzone response, combination windows and repeat
  rates stay in the compiled map. The player rebinds which control does a thing,
  not how the engine interprets it.
- **No record of what was replaced.** Restoring a default is removing the entry,
  which the compiled map already describes. Storing the original as well would
  be a second copy that could disagree with the declaration.
