# Format — Achievement record (`achievements.dat`)

What a player has earned. One fixed-size file per title, written by the engine
and read by nothing else.

It is the authoritative record: the console's own achievement system is a mirror
that may be absent, degraded or refusing, and this file is what survives that.
The runtime contract around it is in
[../subsystems/ACHIEVEMENT.md](../subsystems/ACHIEVEMENT.md); where each platform
keeps it is in that platform's own spec.

**Little-endian**, unlike the trophy container it replaces, because every
platform this engine targets is little-endian and the file never leaves the
console that wrote it. There is no portability requirement to pay for.

## Layout

Twenty-eight bytes, fixed. The whole file is one structure; there are no
variable-length sections and nothing to seek past.

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0x00 | `magic` | 4 | `0x56484341` — `"ACHV"` read as little-endian |
| 0x04 | `checksum` | 4 | Over everything from `0x08` to the end |
| 0x08 | `version` | 2 | Layout version; `1` |
| 0x0A | `declared` | 2 | Achievements the title declared when this was written |
| 0x0C | `unlocked` | 16 | One bit per achievement, identifier order |
| | | **0x1C** | **Total** |

Fields are ordered largest first, so the structure has no padding on any
supported target and its in-memory image is its on-disc image.

### `unlocked`

A bitfield of `ACHV_MAX_ENTRIES` bits — the format constant in the engine's
achievement header, mirrored by `tools/achievements.py` and checked against it by
that tool's tests. Achievement *n* is bit `n & 7` of byte `n >> 3`. Set means
earned.

The field is always the full 16 bytes regardless of how many achievements a title
declares. A fixed size means the file is written and read as one block with no
allocation and no seeking, and it means a title that adds achievements does not
change the layout — which is the case that would otherwise silently reinterpret
an existing player's record.

### `declared`

How many achievements existed when the record was last written. It is **not** a
bound on the bitfield and nothing is rejected for disagreeing with it.

It exists to tell two situations apart that look identical from the bits alone: a
title that has grown new achievements since the player last played, and a record
belonging to a different title entirely. The first is ordinary and the record is
kept; the second is what the magic and checksum are for.

### `checksum`

FNV-1a over bytes `0x08` onward, which is `version`, `declared` and the bitfield.
It deliberately excludes itself and the magic.

FNV-1a rather than a CRC because it needs no lookup table. A CRC-32 table is a
kilobyte of otherwise idle memory on a console with thirty-two megabytes, to
detect corruption in twenty bytes.

This detects a truncated or scribbled file. It is **not** a defence against a
player editing their own save, and nothing here pretends to be: the file is on
storage the player owns, the achievements are local, and an engine that spent
effort on tamper-proofing a single-player record would be spending it against its
own user.

## Reading

A file that is absent, shorter than the layout, carries the wrong magic, declares
an unsupported version, or fails its checksum is treated as **nothing unlocked**.
None of those is a fault the player caused or can fix, and refusing to run
because a record is unreadable would cost them the ability to earn anything
further on top of whatever they already lost.

A malformed record is reported once and overwritten by the next unlock. An absent
one is not reported at all — it is every new player.

## Writing

The file is written whole, at the moment of an unlock, and again at shutdown if
anything changed. Writing on unlock rather than only at shutdown is deliberate:
the consoles this engine targets are switched off rather than shut down, and a
record that only persisted on a clean exit would routinely lose what it was for.

Twenty-eight bytes is small enough that rewriting the whole file costs less than
tracking which part changed.

**There is no write-and-rename.** The platforms here do not all offer an atomic
rename, and a half-written twenty-eight-byte file fails its checksum and is
treated as nothing unlocked — which is the same outcome the rename would have
been protecting against, without the code.
