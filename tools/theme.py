#!/usr/bin/env python3
"""Theme declaration reader and generator.

game/config/theme.json is the single source of truth for the interface's look. The
themes it declares are compiled into the binary, because the interface must be
able to draw before any filesystem exists and because a built-in theme is what a
refused load falls back to. The same declaration also cooks to loadable theme
assets.

One theme is marked "default": true. Every other theme's colours, metrics and
fonts are resolved by starting from the default theme's own values and
overriding field by field with whatever the theme itself names -- a theme that
declares nothing for a given field simply inherits it.

    python3 tools/theme.py --emit-ids   build/generated/theme/UiThemeIds.h
    python3 tools/theme.py --emit-table build/generated/theme/UiThemeTable.cpp
"""

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ps2lib import theme as themelib

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DECLARATION = os.path.join(_ROOT, "game", "config", "theme.json")
ENGINE_UI_HEADER = os.path.join(_ROOT, "engine", "include", "ui", "EngineUi.h")
ENGINE_THEME_FORMAT_HEADER = os.path.join(_ROOT, "engine", "include", "graphics", "ThemeFormat.h")


class ThemeDeclarationError(ValueError):
    """A declaration that cannot produce a theme, with the reason."""


def _enum_values(header_path, enum_name):
    with open(header_path, "r", encoding="utf-8") as fh:
        text = fh.read()
    m = re.search(r"enum class %s\s*:\s*\w+\s*\{(.*?)\}" % re.escape(enum_name), text, re.S)
    if not m:
        raise ThemeDeclarationError(f"could not find enum class {enum_name} in {header_path}")

    values = []
    for line in m.group(1).splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if not line:
            continue
        name = line.split("=")[0].strip()
        if not name or name == "Count":
            continue
        values.append(name)
    return values


def engine_color_roles(header_path=ENGINE_UI_HEADER):
    """Read the colour roles out of the engine's own enum.

    The declaration is checked against this rather than against a second copy of
    the list, so a role added to the engine and forgotten in the theme fails the
    build instead of producing a theme with a missing colour.
    """
    return _enum_values(header_path, "UiColor")


def engine_font_roles(header_path=ENGINE_THEME_FORMAT_HEADER):
    """Read the font roles out of the engine's own enum, the same way
    engine_color_roles reads UiColor, so the two cannot drift."""
    return _enum_values(header_path, "UiFontRole")


ALL_METRIC_NAMES = frozenset(themelib.INT_METRICS) | {"repeatDelaySeconds", "repeatIntervalSeconds"}


def _find_default(decl):
    """The one theme every other theme inherits unset colours, metrics and
    fonts from."""
    defaults = [entry for entry in (decl.get("themes") or []) if entry.get("default")]
    if len(defaults) == 1:
        return defaults[0]
    if not defaults:
        raise ThemeDeclarationError('no theme is marked "default": true; exactly one must be')
    names = ", ".join(entry.get("name", "?") for entry in defaults)
    raise ThemeDeclarationError(f'more than one theme is marked "default": true ({names}); exactly one must be')


def resolved_colors(default_entry, entry):
    """A theme's colours: the default theme's own, with anything this theme
    names overridden role by role."""
    merged = dict(default_entry.get("colors") or {})
    merged.update(entry.get("colors") or {})
    return merged


def resolved_metrics(default_entry, entry):
    """A theme's metrics: the default theme's own, with anything this theme
    names overridden field by field.

    A theme is a style -- colours, metrics and fonts together -- so `CONTRAST`,
    sized for a small or poor display, can ask for a larger textScale without
    every other theme paying for it too.
    """
    merged = dict(default_entry.get("metrics") or {})
    merged.update(entry.get("metrics") or {})
    return merged


def resolved_fonts(default_entry, entry):
    """A theme's fonts: the default theme's own, with anything this theme
    names overridden role by role."""
    merged = dict(default_entry.get("fonts") or {})
    merged.update(entry.get("fonts") or {})
    return merged


def validate_declaration(decl):
    """Check an in-memory declaration the same way `load` checks a file, so a
    caller editing a declaration in memory (a visual editor, a test) can
    validate before ever writing it to disk."""
    themes = decl.get("themes") or []
    if not themes:
        raise ThemeDeclarationError("no themes declared")

    default_entry = _find_default(decl)
    roles = engine_color_roles()
    font_roles = engine_font_roles()

    seen = set()
    for entry in themes:
        name = entry.get("name")
        if not name or not re.fullmatch(r"[A-Z][A-Z0-9_]*", name):
            raise ThemeDeclarationError(f"theme name {name!r} must be upper snake case")
        if name in seen:
            raise ThemeDeclarationError(f"duplicate theme name {name!r}")
        seen.add(name)

        own_colors = entry.get("colors") or {}
        extra = [r for r in own_colors if r not in roles]
        if extra:
            raise ThemeDeclarationError(f"theme {name}: {', '.join(extra)} name no colour role")
        missing = [r for r in roles if r not in resolved_colors(default_entry, entry)]
        if missing:
            raise ThemeDeclarationError(f"theme {name}: no colour for {', '.join(missing)}")

        own_metrics = entry.get("metrics") or {}
        unknown = [k for k in own_metrics if k not in ALL_METRIC_NAMES]
        if unknown:
            raise ThemeDeclarationError(f"theme {name}: metrics override names unknown metric(s) {', '.join(unknown)}")
        try:
            themelib._check_metrics(resolved_metrics(default_entry, entry))
        except themelib.ThemeError as e:
            raise ThemeDeclarationError(f"theme {name}: {e}")

        own_fonts = entry.get("fonts") or {}
        unknown_fonts = [r for r in own_fonts if r not in font_roles]
        if unknown_fonts:
            raise ThemeDeclarationError(f"theme {name}: fonts name unknown role(s) {', '.join(unknown_fonts)}")

    if not resolved_fonts(default_entry, default_entry).get("Default"):
        raise ThemeDeclarationError(f"theme {default_entry['name']}: the default theme must declare a \"Default\" font")

    return decl


def load(path):
    with open(path, "r", encoding="utf-8-sig") as fh:
        decl = json.load(fh)
    return validate_declaration(decl)


def _write(path, text):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def emit_ids(decl, out_path):
    """The enumerator a caller names a built-in theme by."""
    lines = [
        "#pragma once",
        "",
        "// Generated by tools/theme.py from the title's theme declaration.",
        "// Do not edit: regenerate by building.",
        "",
        "#include <cstdint>",
        "",
        "/// A theme compiled into the binary. Always available, because the",
        "/// interface must be able to draw before a filesystem exists.",
        "enum class UiBuiltinTheme : uint8_t",
        "{",
    ]
    for i, entry in enumerate(decl["themes"]):
        detail = entry.get("description", "")
        lines.append("    {} = {},{}".format(entry["name"], i, (" // " + detail) if detail else ""))
    lines += ["", "    Count = {}".format(len(decl["themes"])), "};", ""]
    _write(out_path, "\n".join(lines))
    return out_path


def emit_table(decl, out_path):
    """The colour, metrics and font tables for every theme, all three per
    theme rather than shared, so a theme is a whole style."""
    roles = engine_color_roles()
    default_entry = _find_default(decl)

    lines = [
        "// Generated by tools/theme.py from the title's theme declaration.",
        "// Do not edit: regenerate by building.",
        "",
        '#include "ui/EngineUi.h"',
        "",
        "namespace",
        "{",
        "    UiRgba Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)",
        "    {",
        "        UiRgba c;",
        "        c.r = r;",
        "        c.g = g;",
        "        c.b = b;",
        "        c.a = a;",
        "        return c;",
        "    }",
        "} // namespace",
        "",
        "const char* Ui_BuiltinThemeName(UiBuiltinTheme theme)",
        "{",
        "    switch (theme)",
        "    {",
    ]
    for entry in decl["themes"]:
        lines.append('    case UiBuiltinTheme::{}:'.format(entry["name"]))
        lines.append('        return "{}";'.format(entry["name"]))
    lines += [
        "    case UiBuiltinTheme::Count:",
        "        break;",
        "    }",
        '    return "?";',
        "}",
        "",
        "UiStyle Ui_BuiltinTheme(UiBuiltinTheme theme)",
        "{",
        "    UiStyle s;",
        "    for (uint8_t i = 0; i < static_cast<uint8_t>(UiColor::Count); ++i)",
        "        s.colors[i] = Rgba(0, 0, 0, 255);",
        "",
    ]
    lines.append("    s.reserved[0] = 0;")
    lines += ["", "    switch (theme)", "    {"]

    for entry in decl["themes"]:
        entry_colors = resolved_colors(default_entry, entry)
        entry_metrics = resolved_metrics(default_entry, entry)
        lines.append("    case UiBuiltinTheme::{}:".format(entry["name"]))
        for role in roles:
            r, g, b, a = entry_colors[role]
            lines.append("        s.colors[static_cast<uint8_t>(UiColor::{})] = Rgba({}, {}, {}, {});".format(role, r, g, b, a))
        for metric_name in themelib.INT_METRICS:
            lines.append("        s.{} = {};".format(metric_name, entry_metrics[metric_name]))
        lines.append("        s.repeatDelaySeconds = {}f;".format(entry_metrics["repeatDelaySeconds"]))
        lines.append("        s.repeatIntervalSeconds = {}f;".format(entry_metrics["repeatIntervalSeconds"]))
        lines.append("        break;")
    lines += [
        "    case UiBuiltinTheme::Count:",
        "        break;",
        "    }",
        "    return s;",
        "}",
        "",
        "const char* Ui_BuiltinRoleFont(UiBuiltinTheme theme, UiFontRole role)",
        "{",
        "    switch (theme)",
        "    {",
    ]
    for entry in decl["themes"]:
        entry_fonts = resolved_fonts(default_entry, entry)
        lines.append("    case UiBuiltinTheme::{}:".format(entry["name"]))
        lines.append("        switch (role)")
        lines.append("        {")
        for role in themelib.FONT_ROLES:
            lines.append("        case UiFontRole::{}:".format(role))
            lines.append('            return "{}";'.format(entry_fonts.get(role, "")))
        lines.append("        case UiFontRole::Count:")
        lines.append("            break;")
        lines.append("        }")
        lines.append("        break;")
    lines += [
        "    case UiBuiltinTheme::Count:",
        "        break;",
        "    }",
        '    return "";',
        "}",
        "",
    ]
    _write(out_path, "\n".join(lines))
    return out_path


def cook_payloads(decl):
    """Every declared theme as a cooked payload, keyed by asset name."""
    default_entry = _find_default(decl)
    out = {}
    for entry in decl["themes"]:
        blob, _ = themelib.write_theme(
            resolved_colors(default_entry, entry),
            resolved_metrics(default_entry, entry),
            resolved_fonts(default_entry, entry),
        )
        out["THEME_" + entry["name"]] = blob
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description="Theme declaration reader and generator")
    parser.add_argument("--declaration", default=DEFAULT_DECLARATION)
    parser.add_argument("--emit-ids")
    parser.add_argument("--emit-table")
    args = parser.parse_args(argv)

    try:
        decl = load(args.declaration)
    except (ThemeDeclarationError, themelib.ThemeError, OSError, ValueError) as e:
        print(f"theme: {e}", file=sys.stderr)
        return 1

    if args.emit_ids:
        print("theme: wrote", emit_ids(decl, args.emit_ids))
    if args.emit_table:
        print("theme: wrote", emit_table(decl, args.emit_table))
    if not args.emit_ids and not args.emit_table:
        print(f"theme: {len(decl['themes'])} theme(s) declared, all valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
