# Format — Game archive (`.PS2R`)

A flat single-file container: one header, a table of contents, a string table,
and the raw payloads. No folders, no per-file headers on disc.

Little-endian on every platform. This layout is a contract between the runtime
reader and the packaging stage of the build pipeline; both sides must change
together. The runtime behaviour built on it is specified in
[subsystems/ARCHIVE.md](../subsystems/ARCHIVE.md); how archives are produced is
in [PIPELINE.md](../PIPELINE.md).

## Layout

```
+-----------------------------+  offset 0
| header (32 B)               |  magic "PS2R", version, entryCount,
|                             |  stringsOffset, stringsSize, dataOffset
+-----------------------------+  offset 32
| TOC entry x entryCount      |  { nameHash, nameOffset, offset, size }, 16 B each
+-----------------------------+  stringsOffset
| string table                |  NUL-terminated canonical keys
+-----------------------------+  dataOffset (sector-aligned)
| payload 0                   |  each payload starts on a sector boundary
| (pad to next sector)        |
| payload 1                   |
| ...                         |
+-----------------------------+
```

| Field | Width | Meaning |
|---|---|---|
| `magic` | 4 | `"PS2R"` little-endian |
| `version` | 2 | format version; a reader refuses anything it does not know |
| `flags` | 2 | reserved, zero |
| `entryCount` | 4 | number of TOC entries |
| `stringsOffset` / `stringsSize` | 4 + 4 | string table location and extent |
| `dataOffset` | 4 | first payload byte, sector-aligned |

Each TOC entry holds a 32-bit FNV-1a hash of the canonical key for fast
rejection, plus an offset into the string table. **A hash hit is always confirmed
by string comparison**, so a collision cannot resolve to the wrong asset.

## What a reader must establish before it trusts a name offset

A name offset is a number chosen by the file, and the comparison that confirms a
hash hit reads a C string at it. Two properties make that read bounded, and a
reader establishes both at mount time rather than assuming them:

- **Every name offset is inside the string table.** An entry whose offset is at
  or past `stringsSize` makes the mount fail; it is not skipped, because an
  entry table that is wrong about one entry is not trustworthy about the rest.
- **The string table ends in a NUL.** The packer always writes terminated keys,
  so a reader may require it rather than tolerate its absence, and may terminate
  its own copy so the property holds of the mount rather than of the file.

A container declaring entries with no string table at all is refused for the
same reason: there is nowhere for any of those names to be.

The entry count is also a file-chosen number that a reader multiplies by the
entry width to size its table. That product is computed at a width that cannot
wrap on a 32-bit target, so a count chosen to wrap it cannot produce a short
allocation the rest of the mount then walks past.

## Sector alignment

Payloads are aligned to a 2048-byte optical sector. A read therefore never
straddles an extra sector and every seek target lands on a sector boundary. The
cost is padding between payloads; the benefit is that on disc-based hardware the
drive does the minimum work per asset. The alignment value is part of the format
because the packer and the reader must agree on it.

## Canonical asset key

The packer and the runtime derive the same key for every asset, so a lookup
succeeds regardless of how the path was written:

| Input path | Canonical key |
|---|---|
| `cdrom0:/RASSETS/BOX.PS2A;1` | `RASSETS/BOX.PS2A` |
| `RASSETS\BOX.PS2A` (baked dependency) | `RASSETS/BOX.PS2A` |
| `mass0:/RASSETS/BOX.PS2A` (a non-cdrom device token) | `RASSETS/BOX.PS2A` |
| `D:/games/build/RASSETS/BOX.PS2A` (desktop root) | `RASSETS/BOX.PS2A` |

**A key is the path relative to the resource root.** The root is whatever the
platform prepends when it turns a relative asset path into a real one — a device
token on a console, the directory the executable lives in on a desktop — so
canonicalisation is exactly that step run backwards, and removes the root it
knows rather than guessing where the root ended.

That distinction is the whole point. A rule that cut the path at its first colon
worked on a console, where the root is a short device token, and silently failed
on a desktop, where the first colon belongs to a drive letter and the rest of an
absolute path survived into the key. Every archived asset then missed, and fell
back to a loose file that a packaged build does not have.

A path that carries some *other* device token — one asset reached through a root
that is not the active one — still has that token removed, but only when the name
before the colon is longer than a single character. A drive letter is one
character, which is what keeps the two cases apart.

After the root is removed: strip any `;N` version suffix, convert backslashes to
forward slashes, upper-case, and drop leading slashes.

This must be implemented identically in the engine and in the packaging tool.
Canonicalisation is also what prevents the same asset occupying two resource
slots when it is loaded once by device path and once by baked dependency string.

## Constraints

- Payload order is chosen by the packer. Packing in access order is what makes
  locality pay off; the format does not enforce it.
- No compression. Payloads are stored exactly as cooked, so a read is a copy.
- No per-payload checksum. Corruption is detected at the asset level, by the
  format in [ASSET_FORMAT.md](ASSET_FORMAT.md), not here.
- Archives are read-only at runtime. Nothing appends to a mounted archive.
- The string table must be non-empty whenever the container holds any entry, and
  its last byte must be a NUL.
- The whole TOC and string table are read into memory on mount, so a very large
  entry count costs resident memory even when few assets are read. The shipped
  master archive (see [subsystems/ARCHIVE.md](../subsystems/ARCHIVE.md)) holds
  entries for every level in the game at once, so this cost scales with total
  game content, not with whatever is currently in play.
