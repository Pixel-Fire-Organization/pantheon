#!/usr/bin/env python3
"""
inspect_asset.py - read-only dump of a cooked .ps2a asset.

A format is only debuggable if its contents can be seen without running the
engine. Prints the header, dependency list and payload summary.

Usage:
  python3 tools/inspect_asset.py dist/cooked/win32/rassets/BOX.PS2A
  python3 tools/inspect_asset.py dist/cooked/win32/rassets/       (whole tree)
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ps2lib import font, ps2a, theme as themelib, tim2


def describe_payload(info):
    """Decode just enough of the payload to be useful. Never fatal: a payload we
    cannot read is still worth reporting alongside its header.

    Returns (label, text) so the caller need not know which kinds decode."""
    decoders = {"TEXTURE": ("texture", tim2.describe_text), "FONT": ("font", font.describe_text), "THEME": ("theme", themelib.describe_text)}
    entry = decoders.get(info["type"])
    if not entry:
        return None
    label, decode = entry
    try:
        return label, decode(info["payload"])
    except Exception:  # noqa: BLE001 - diagnostics must survive a bad payload
        return None


def dump(path):
    try:
        info = ps2a.read_ps2a(path)
    except (ValueError, OSError) as e:
        print(f"{path}: {e}")
        return False

    print(f"{os.path.basename(path)}")
    print(f"  type       {info['type']}")
    print(f"  source ext {info['ext'] or '<none>'}")
    print(f"  payload    {info['data_size']} bytes  (file {info['total_size']})")

    detail = describe_payload(info)
    if detail:
        print(f"  {detail[0]:<10} {detail[1]}")

    if info["deps"]:
        print(f"  deps       {len(info['deps'])}")
        for d in info["deps"]:
            print(f"               {d}")
    else:
        print("  deps       none")
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description="Inspect cooked .ps2a assets")
    ap.add_argument("target", help="a .ps2a file, or a directory of them")
    args = ap.parse_args(argv)

    if os.path.isdir(args.target):
        names = sorted(f for f in os.listdir(args.target) if f.upper().endswith(".PS2A"))
        if not names:
            print(f"No .PS2A files in {args.target}")
            return 0
        ok = True
        for n in names:
            ok = dump(os.path.join(args.target, n)) and ok
            print("")
        return 0 if ok else 1

    return 0 if dump(args.target) else 1


if __name__ == "__main__":
    sys.exit(main())
