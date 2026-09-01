#!/usr/bin/env python3
"""
validate_cooked.py - stage 4 gate: refuse to package a bad cook.

Checks a platform's cooked tree against the cook list that produced it, so a
content error is caught with the asset's name attached rather than surfacing on
the target as a missing texture. Runnable standalone, so CI can check a tree
without building one.

Usage:
  python3 tools/validate_cooked.py --platform win32
  python3 tools/validate_cooked.py --dir dist/cooked/ps2pal/rassets --cooklist engine/config/ps2/cooklist.json
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ps2lib import font, ps2a, theme as themelib, tim2
import cook_assets


class Report:
    """Collects failures so one run reports every problem, not just the first.
    A cook with six oversized textures should not take six builds to fix."""

    def __init__(self):
        self.errors = []
        self.warnings = []
        self.checked = 0

    def error(self, asset, message):
        self.errors.append(f"{asset}: {message}")

    def warn(self, asset, message):
        self.warnings.append(f"{asset}: {message}")

    def ok(self):
        return not self.errors


def validate_tree(directory, cooklist, report):
    if not os.path.isdir(directory):
        report.error(directory, "cooked directory does not exist - has the cook stage run?")
        return

    names = sorted(f for f in os.listdir(directory) if f.upper().endswith(".PS2A"))
    if not names:
        report.error(directory, "no cooked assets found")
        return

    policies = cooklist.get("assets", {})
    present = set()
    texture_bytes = 0

    for name in names:
        path = os.path.join(directory, name)
        report.checked += 1

        try:
            info = ps2a.read_ps2a(path)
        except (ValueError, OSError) as e:
            # Covers bad magic, a truncated payload and an over-long dep list:
            # read_ps2a is the same parse the runtime performs.
            report.error(name, str(e))
            continue

        key = f"RASSETS/{os.path.splitext(name)[0].upper()}.PS2A"
        if key in present:
            report.error(name, f"duplicate resource key {key}")
        present.add(key)

        policy = policies.get(info["type"], {})
        if not policy.get("enabled", False):
            report.error(name, f"type {info['type']} is not enabled in this platform's cook list")
            continue

        if info["type"] == "TEXTURE":
            texture_bytes += _validate_texture(name, info, policy, report)
        elif info["type"] == "FONT":
            _validate_font(directory, name, info, report)
        elif info["type"] == "THEME":
            try:
                themelib.describe(info["payload"])
            except ValueError as e:
                report.error(name, f"theme payload unreadable: {e}")

    _validate_deps(directory, names, present, report)

    budget = policies.get("TEXTURE", {}).get("budget_bytes")
    if budget and texture_bytes > budget:
        report.error("<textures>", f"cooked textures total {texture_bytes} bytes, over the {budget} byte budget")


def _validate_texture(name, info, policy, report):
    try:
        desc = tim2.describe(info["payload"])
    except ValueError as e:
        report.error(name, f"texture payload unreadable: {e}")
        return 0

    want = str(policy.get("format", "source")).lower()
    if want != "source" and desc["format"] != want:
        report.error(name, f"cooked as {desc['format']} but the cook list asks for {want}")

    max_w = policy.get("max_width")
    max_h = policy.get("max_height")
    if max_w and desc["width"] > max_w:
        report.error(name, f"width {desc['width']} exceeds the platform maximum {max_w}")
    if max_h and desc["height"] > max_h:
        report.error(name, f"height {desc['height']} exceeds the platform maximum {max_h}")

    return info["data_size"]


def _validate_font(directory, name, info, report):
    """A font is metrics plus an atlas dependency, and the two must agree. A
    disagreement draws every glyph from the wrong place, which looks like
    corruption rather than like a content error, so it is caught here."""
    try:
        desc = font.describe(info["payload"])
    except ValueError as e:
        report.error(name, f"font payload unreadable: {e}")
        return

    if not info["deps"]:
        report.error(name, "names no atlas dependency")
        return

    atlas_key = info["deps"][0]
    atlas_path = os.path.join(directory, os.path.basename(atlas_key))
    try:
        atlas = ps2a.read_ps2a(atlas_path)
        image = tim2.describe(atlas["payload"])
    except (ValueError, OSError) as e:
        report.error(name, f"atlas '{atlas_key}' could not be read: {e}")
        return

    if image["width"] != desc["atlas_width"] or image["height"] != desc["atlas_height"]:
        report.error(name, f"metrics describe a {desc['atlas_width']}x{desc['atlas_height']} atlas "
                           f"but '{atlas_key}' is {image['width']}x{image['height']}")

    # A pixel font magnified to a whole-pixel scale is exact; linearly filtered
    # it is blurred at every scale, so the wrong filter is a defect not a taste.
    if image["filter"] != "nearest":
        report.warn(name, f"atlas '{atlas_key}' is not cooked with the nearest filter")


def _validate_deps(directory, names, present, report):
    """A dependency naming an asset that was not cooked becomes a resource that
    never reports ready - a hang rather than an error, so it is caught here."""
    for name in names:
        path = os.path.join(directory, name)
        try:
            info = ps2a.read_ps2a(path)
        except (ValueError, OSError):
            continue  # already reported
        for dep in info["deps"]:
            if dep.upper() not in present:
                report.error(name, f"dependency '{dep}' was not cooked for this platform")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Validate a cooked asset tree")
    ap.add_argument("--platform", help="platform whose cooked tree and cook list to use")
    ap.add_argument("--dir", help="cooked directory (overrides --platform)")
    ap.add_argument("--cooklist", help="cook list file (overrides --platform)")
    args = ap.parse_args(argv)

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    directory = args.dir
    if directory is None:
        if not args.platform:
            print("validate_cooked: --platform or --dir is required")
            return 2
        directory = os.path.join(root, "dist", "cooked", args.platform.lower(), "rassets")

    cooklist_path = args.cooklist or cook_assets.cooklist_for_platform(root, args.platform)
    cooklist = cook_assets.load_cooklist(cooklist_path)

    report = Report()
    validate_tree(directory, cooklist, report)

    label = cooklist.get("platform", "<default>")
    print(f"validate_cooked: {report.checked} asset(s) in {directory} [cook list: {label}]")
    for w in report.warnings:
        print(f"  WARN  {w}")
    for e in report.errors:
        print(f"  FAIL  {e}")

    if report.ok():
        print("  all checks passed")
        return 0
    print(f"\n{len(report.errors)} problem(s) - not packaging this tree.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
