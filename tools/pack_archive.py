#!/usr/bin/env python3
"""
pack_archive.py — PS2 Engine game-archive (.PS2R) packer.

Bundles a set of files into one flat container: a fixed header, a TOC of
{nameHash, nameOffset, offset, size} entries, a string table, then the raw
payloads — each aligned to a 2048-byte DVD sector. The runtime read side is
engine/src/EngineArchive.cpp; the on-disc format is engine/include/EngineArchive.h.

The archive key is the canonical asset key (Engine_Path_Canonical): device-token
and ";1"-stripped, forward-slashed, upper-cased. Both sides derive it identically,
so a runtime lookup by device path ("cdrom0:/RASSETS/BOX.PS2A;1") or by baked
dependency string ("RASSETS/BOX.PS2A") resolves to the same entry.

Usage:
    # Pack every file in a directory under a disc prefix:
    python3 tools/pack_archive.py --dir dist/cooked/win32/rassets --prefix RASSETS \\
        --dst build/.../RASSETS.PS2R

    # Or from an explicit ordered manifest (controls disc order for locality):
    python3 tools/pack_archive.py --manifest boot.json --dst out.PS2R
    # manifest: [ { "path": "a/BOX.PS2A", "key": "RASSETS/BOX.PS2A" }, ... ]
"""

import argparse
import json
import os
import struct
import sys

ARCH_FILE_MAGIC = 0x52325350  # "PS2R" little-endian
ARCH_FILE_VERSION = 1
ARCH_SECTOR_ALIGN = 2048

_HEADER_FMT = "<IHHIIIIII"  # magic, version, flags, entryCount, strOff, strSize, dataOff, res[2]
_HEADER_SIZE = struct.calcsize(_HEADER_FMT)  # 32
_TOC_FMT = "<IIII"  # nameHash, nameOffset, offset, size
_TOC_SIZE = struct.calcsize(_TOC_FMT)  # 16

assert _HEADER_SIZE == 32 and _TOC_SIZE == 16


def canonical_key(path):
    """Mirror of Engine_Path_Canonical (engine/src/EngineCore.cpp)."""
    colon = path.find(":")
    p = path[colon + 1:] if colon >= 0 else path
    p = p.replace("\\", "/").upper()
    semi = p.rfind(";")
    if semi >= 0 and p[semi + 1:].isdigit():
        p = p[:semi]
    return p.lstrip("/")


def fnv1a32(s):
    """Mirror of Internal_Fnv1a32 (engine/src/EngineArchive.cpp)."""
    h = 2166136261
    for b in s.encode("ascii"):
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def _align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def write_archive(entries, out_path):
    """entries: iterable of (key, data_bytes), in the desired disc order.

    Returns a small stats dict. Raises ValueError on a duplicate canonical key.
    """
    keyed = []
    seen = set()
    for key, data in entries:
        ck = canonical_key(key)
        if ck in seen:
            raise ValueError(f"duplicate canonical key '{ck}'")
        seen.add(ck)
        keyed.append((ck, data))

    # String table: NUL-terminated canonical keys.
    strings = bytearray()
    name_offsets = []
    for ck, _ in keyed:
        name_offsets.append(len(strings))
        strings += ck.encode("ascii") + b"\x00"

    entry_count = len(keyed)
    strings_offset = _HEADER_SIZE + entry_count * _TOC_SIZE
    strings_size = len(strings)
    data_offset = _align_up(strings_offset + strings_size, ARCH_SECTOR_ALIGN)

    # Assign each payload a sector-aligned absolute offset, in order.
    toc = []
    cursor = data_offset
    placed = []
    for (ck, data), noff in zip(keyed, name_offsets):
        off = _align_up(cursor, ARCH_SECTOR_ALIGN)
        toc.append((fnv1a32(ck), noff, off, len(data)))
        placed.append((off, data))
        cursor = off + len(data)

    total_size = _align_up(cursor, ARCH_SECTOR_ALIGN) if placed else data_offset
    buf = bytearray(total_size)

    struct.pack_into(_HEADER_FMT, buf, 0,
                     ARCH_FILE_MAGIC, ARCH_FILE_VERSION, 0, entry_count,
                     strings_offset, strings_size, data_offset, 0, 0)
    pos = _HEADER_SIZE
    for name_hash, noff, off, size in toc:
        struct.pack_into(_TOC_FMT, buf, pos, name_hash, noff, off, size)
        pos += _TOC_SIZE
    buf[strings_offset:strings_offset + strings_size] = strings
    for off, data in placed:
        buf[off:off + len(data)] = data

    with open(out_path, "wb") as fh:
        fh.write(buf)

    return {"entry_count": entry_count, "data_offset": data_offset, "total_size": total_size}


def read_toc(path):
    """Parse an archive's header + TOC + string table (not payloads)."""
    with open(path, "rb") as fh:
        blob = fh.read()
    (magic, version, flags, entry_count, strings_offset, strings_size,
     data_offset, _r0, _r1) = struct.unpack_from(_HEADER_FMT, blob, 0)
    if magic != ARCH_FILE_MAGIC:
        raise ValueError(f"bad magic 0x{magic:08X} in '{path}'")
    strings = blob[strings_offset:strings_offset + strings_size]
    entries = []
    pos = _HEADER_SIZE
    for _ in range(entry_count):
        name_hash, noff, off, size = struct.unpack_from(_TOC_FMT, blob, pos)
        pos += _TOC_SIZE
        end = strings.index(b"\x00", noff)
        entries.append({
            "key": strings[noff:end].decode("ascii"),
            "hash": name_hash,
            "offset": off,
            "size": size,
        })
    return {
        "magic": magic, "version": version, "flags": flags,
        "entry_count": entry_count, "data_offset": data_offset,
        "strings_offset": strings_offset, "strings_size": strings_size,
        "entries": entries,
    }


def read_payload(path, entry):
    """Read a single entry's payload bytes (entry from read_toc)."""
    with open(path, "rb") as fh:
        fh.seek(entry["offset"])
        return fh.read(entry["size"])


def _collect_dir(directory, prefix):
    entries = []
    for name in sorted(os.listdir(directory)):
        full = os.path.join(directory, name)
        if not os.path.isfile(full):
            continue
        with open(full, "rb") as fh:
            data = fh.read()
        entries.append((f"{prefix}/{name}", data))
    return entries


def _collect_manifest(manifest_path):
    with open(manifest_path, "r", encoding="utf-8") as fh:
        items = json.load(fh)
    base = os.path.dirname(os.path.abspath(manifest_path))
    entries = []
    for item in items:
        src = item["path"]
        if not os.path.isabs(src):
            src = os.path.join(base, src)
        key = item.get("key") or os.path.basename(src)
        with open(src, "rb") as fh:
            entries.append((key, fh.read()))
    return entries


def main(argv=None):
    ap = argparse.ArgumentParser(description="Pack files into a .PS2R game archive")
    ap.add_argument("--dir", help="directory whose files are packed (non-recursive)")
    ap.add_argument("--prefix", help="disc prefix for --dir keys, e.g. RASSETS")
    ap.add_argument("--manifest", help="JSON list of {path, key} for explicit order")
    ap.add_argument("--dst", required=True, help="output .PS2R path")
    args = ap.parse_args(argv)

    if args.manifest:
        entries = _collect_manifest(args.manifest)
    elif args.dir:
        prefix = args.prefix or os.path.basename(os.path.normpath(args.dir)).upper()
        entries = _collect_dir(args.dir, prefix)
    else:
        ap.error("provide --dir or --manifest")

    if not entries:
        print(f"pack_archive: no input files; writing empty archive '{args.dst}'", file=sys.stderr)

    os.makedirs(os.path.dirname(os.path.abspath(args.dst)), exist_ok=True)
    stats = write_archive(entries, args.dst)
    print(f"pack_archive: wrote {args.dst} ({stats['entry_count']} entries, {stats['total_size']} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
