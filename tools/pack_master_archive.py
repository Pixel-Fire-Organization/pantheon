#!/usr/bin/env python3
"""
pack_master_archive.py — merge cooked rassets and every compiled level into one
master .PS2R, mounted once at Engine_Init and never unmounted or re-mounted for
a level switch (see docs/subsystems/ARCHIVE.md, docs/subsystems/LEVEL.md).

Each level still compiles to its own standalone .PS2R under
dist/cooked/<platform>/levels/ (tools/compile_level.py) — that file keeps
working as a build-time artefact tools/dump_level.py can inspect in isolation.
This tool never writes to it; it only reads each one's table of contents and
payloads back out (byte-identical) and folds them into the one archive that
actually ships, alongside the platform's rassets/. Reuses pack_archive.py's
reader/writer, so the merged file's byte layout is the same .PS2R format.

Usage:
    python3 tools/pack_master_archive.py \\
        --rassets dist/cooked/win32/rassets --prefix RASSETS \\
        --levels dist/cooked/win32/levels \\
        --dst dist/win32/RASSETS.PS2R
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_archive


def _rasset_entries(rassets_dir, prefix):
    if not rassets_dir or not os.path.isdir(rassets_dir):
        return []
    return pack_archive._collect_dir(rassets_dir, prefix)


def _level_entries(levels_dir):
    """Read every already-compiled level .PS2R back out into (key, bytes)
    pairs, one level at a time in name order, each level's own entries kept in
    their original locality order (core, sectors row-major, then materials/
    models/farfield) - only the boundary between levels is new."""
    entries = []
    if not levels_dir or not os.path.isdir(levels_dir):
        return entries
    for name in sorted(os.listdir(levels_dir)):
        if not name.upper().endswith(".PS2R"):
            continue
        path = os.path.join(levels_dir, name)
        toc = pack_archive.read_toc(path)
        for e in toc["entries"]:
            entries.append((e["key"], pack_archive.read_payload(path, e)))
    return entries


def build_master_archive(rassets_dir, levels_dir, dst, prefix="RASSETS"):
    """entries: rassets first (small, always-needed), then every level in name
    order. A canonical key colliding between two sources - a level and a
    rasset, or two levels - is a content error and raises, the same as it
    would packing either alone (see pack_archive.write_archive)."""
    entries = _rasset_entries(rassets_dir, prefix) + _level_entries(levels_dir)
    if not entries:
        print(f"pack_master_archive: no input files; writing empty archive '{dst}'", file=sys.stderr)
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    return pack_archive.write_archive(entries, dst)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Merge cooked rassets + compiled levels into one master .PS2R")
    ap.add_argument("--rassets", help="cooked rassets directory (e.g. dist/cooked/<platform>/rassets)")
    ap.add_argument("--prefix", default="RASSETS", help="disc prefix for --rassets keys")
    ap.add_argument("--levels", help="compiled levels directory (e.g. dist/cooked/<platform>/levels)")
    ap.add_argument("--dst", required=True, help="output master .PS2R path")
    args = ap.parse_args(argv)

    stats = build_master_archive(args.rassets, args.levels, args.dst, args.prefix)
    print(f"pack_master_archive: wrote {args.dst} ({stats['entry_count']} entries, {stats['total_size']} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
