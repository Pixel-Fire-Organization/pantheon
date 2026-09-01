#!/usr/bin/env python3
"""
inspect_archive.py - read-only dump of a .PS2R container.

Prints the header, every entry with its offset and size, and the padding the
sector alignment costs. Useful next to a validation failure, and for confirming
that payloads really are packed in access order.

Usage:
  python3 tools/inspect_archive.py dist/win32/RASSETS.PS2R
  python3 tools/inspect_archive.py dist/win32/RASSETS.PS2R --entries
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_archive


def inspect(path, show_entries):
    toc = pack_archive.read_toc(path)
    entries = toc["entries"]
    file_size = os.path.getsize(path)

    print(f"{path}")
    print(f"  entries      {len(entries)}")
    print(f"  file size    {file_size} bytes")
    print(f"  data offset  {toc['data_offset']}")

    payload_bytes = sum(e["size"] for e in entries)
    print(f"  payload      {payload_bytes} bytes")

    if entries:
        # Everything between data_offset and EOF that is not payload is
        # alignment padding. Worth seeing: it is the price of sector alignment.
        span = file_size - toc["data_offset"]
        padding = span - payload_bytes
        pct = (100.0 * padding / span) if span else 0.0
        print(f"  padding      {padding} bytes ({pct:.1f}% of the data region)")

    seen = {}
    duplicates = []
    for e in entries:
        if e["key"] in seen:
            duplicates.append(e["key"])
        seen[e["key"]] = True
    if duplicates:
        print(f"  DUPLICATE KEYS: {', '.join(duplicates)}")

    if show_entries:
        print("")
        print(f"  {'offset':>10}  {'size':>9}  key")
        for e in sorted(entries, key=lambda x: x["offset"]):
            print(f"  {e['offset']:>10}  {e['size']:>9}  {e['key']}")

    return not duplicates


def main(argv=None):
    ap = argparse.ArgumentParser(description="Inspect a .PS2R archive")
    ap.add_argument("archive")
    ap.add_argument("--entries", action="store_true", help="list every entry")
    args = ap.parse_args(argv)

    if not os.path.isfile(args.archive):
        print(f"Not found: {args.archive}")
        return 1
    try:
        return 0 if inspect(args.archive, args.entries) else 1
    except Exception as e:  # noqa: BLE001 - report a malformed container plainly
        print(f"{args.archive}: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
