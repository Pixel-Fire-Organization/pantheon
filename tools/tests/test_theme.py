"""The cooked theme format, and the checks that keep a bad one out of a build.

A theme is copied into live engine state rather than parsed, so nothing about it
is validated incidentally. Every refusal below is therefore load-bearing.
"""

import json
import os
import re
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from ps2lib import ps2a, theme as themelib  # noqa: E402
import theme as theme_tool  # noqa: E402

DECLARATION = os.path.join(ROOT, "game", "config", "theme.json")


def _decl():
    with open(DECLARATION, "r", encoding="utf-8-sig") as fh:
        return json.load(fh)


def _colors():
    return {role: [1, 2, 3, 255] for role in themelib.COLOR_ROLES}


def _metrics(**over):
    m = {n: 4 for n in themelib.INT_METRICS}
    m["textScale"] = 2
    m["barHeight"] = 8
    m["cursorSize"] = 10
    m["scrollBarWidth"] = 6
    m["repeatDelaySeconds"] = 0.35
    m["repeatIntervalSeconds"] = 0.06
    m.update(over)
    return m


# --- the format round-trips -------------------------------------------------

def test_round_trip_preserves_colours_metrics_and_fonts():
    blob, ext = themelib.write_theme(_colors(), _metrics(), {"Body": "ENGINE_FONT"})
    assert ext == ".thm"
    d = themelib.describe(blob)
    assert d["colors"]["Text"] == [1, 2, 3, 255]
    assert d["metrics"]["textScale"] == 2
    assert abs(d["metrics"]["repeatDelaySeconds"] - 0.35) < 1e-6
    assert d["fonts"] == {"Body": "ENGINE_FONT"}


def test_payload_is_exactly_the_declared_layout():
    blob, _ = themelib.write_theme(_colors(), _metrics())
    assert len(blob) == themelib.HEADER_SIZE + themelib.STYLE_BYTES


# --- the writer refuses what the runtime could not use ----------------------

def test_a_missing_colour_role_is_refused():
    colors = _colors()
    del colors["Focus"]
    with pytest.raises(themelib.ThemeError, match="Focus"):
        themelib.write_theme(colors, _metrics())


def test_a_zero_text_scale_is_refused():
    # It divides by zero in every routine that fits text to a width.
    with pytest.raises(themelib.ThemeError, match="textScale"):
        themelib.write_theme(_colors(), _metrics(textScale=0))


def test_a_negative_padding_is_refused():
    with pytest.raises(themelib.ThemeError, match="panelPadding"):
        themelib.write_theme(_colors(), _metrics(panelPadding=-1))


def test_an_out_of_range_repeat_is_refused():
    with pytest.raises(themelib.ThemeError, match="repeatDelaySeconds"):
        themelib.write_theme(_colors(), _metrics(repeatDelaySeconds=0.0))


def test_an_overlong_font_key_is_refused():
    with pytest.raises(themelib.ThemeError, match="longer"):
        themelib.write_theme(_colors(), _metrics(), {"Body": "X" * themelib.KEY_MAX})


# --- the reader refuses a payload it cannot trust ---------------------------

def test_bad_magic_is_refused():
    blob, _ = themelib.write_theme(_colors(), _metrics())
    with pytest.raises(themelib.ThemeError, match="magic"):
        themelib.describe(b"XXXX" + blob[4:])


def test_a_future_version_is_refused_rather_than_migrated():
    blob, _ = themelib.write_theme(_colors(), _metrics())
    bumped = blob[:4] + (themelib.VERSION + 1).to_bytes(2, "little") + blob[6:]
    with pytest.raises(themelib.ThemeError, match="version"):
        themelib.describe(bumped)


def test_a_style_block_of_the_wrong_size_is_refused():
    blob, _ = themelib.write_theme(_colors(), _metrics())
    bad = blob[:6] + (themelib.STYLE_BYTES + 4).to_bytes(2, "little") + blob[8:]
    with pytest.raises(themelib.ThemeError, match="style block"):
        themelib.describe(bad)


def test_a_corrupted_payload_is_caught_by_the_checksum():
    # The whole reason this payload carries one: a copy validates nothing.
    blob, _ = themelib.write_theme(_colors(), _metrics())
    corrupted = bytearray(blob)
    corrupted[themelib.HEADER_SIZE + 3] ^= 0xFF
    with pytest.raises(themelib.ThemeError, match="checksum"):
        themelib.describe(bytes(corrupted))


def test_a_truncated_payload_is_refused():
    blob, _ = themelib.write_theme(_colors(), _metrics())
    with pytest.raises(themelib.ThemeError, match="bytes"):
        themelib.describe(blob[:-8])


# --- the declaration cannot drift from the engine ---------------------------

def test_colour_roles_match_the_engine_enum():
    assert theme_tool.engine_color_roles() == themelib.COLOR_ROLES


def test_font_roles_match_the_engine_enum():
    assert theme_tool.engine_font_roles() == themelib.FONT_ROLES


def test_style_size_matches_the_engine_header():
    header = os.path.join(ROOT, "engine", "include", "graphics", "ThemeFormat.h")
    with open(header, "r", encoding="utf-8") as fh:
        text = fh.read()

    def define(name):
        m = re.search(r"#define\s+%s\s+(\d+)" % name, text)
        assert m, "%s missing from ThemeFormat.h" % name
        return int(m.group(1))

    assert define("THEME_STYLE_BYTES") == themelib.STYLE_BYTES
    assert define("THEME_HEADER_SIZE") == themelib.HEADER_SIZE
    assert define("THEME_VERSION") == themelib.VERSION
    assert define("THEME_KEY_MAX") == themelib.KEY_MAX
    magic = re.search(r"#define\s+THEME_MAGIC\s+0x([0-9A-Fa-f]+)", text)
    assert magic and bytes.fromhex(magic.group(1))[::-1] == themelib.MAGIC


def test_a_role_the_engine_declares_but_the_theme_omits_fails_the_build(tmp_path):
    decl = _decl()
    for entry in decl["themes"]:
        entry.get("colors", {}).pop("Border", None)
    path = tmp_path / "theme.json"
    path.write_text(json.dumps(decl), encoding="utf-8")
    with pytest.raises(theme_tool.ThemeDeclarationError, match="Border"):
        theme_tool.load(str(path))


def test_a_colour_the_engine_does_not_declare_fails_the_build(tmp_path):
    decl = _decl()
    decl["themes"][0]["colors"]["NotARole"] = [0, 0, 0, 255]
    path = tmp_path / "theme.json"
    path.write_text(json.dumps(decl), encoding="utf-8")
    with pytest.raises(theme_tool.ThemeDeclarationError, match="NotARole"):
        theme_tool.load(str(path))


def test_duplicate_theme_names_fail_the_build(tmp_path):
    decl = _decl()
    decl["themes"].append(dict(decl["themes"][1]))
    path = tmp_path / "theme.json"
    path.write_text(json.dumps(decl), encoding="utf-8")
    with pytest.raises(theme_tool.ThemeDeclarationError, match="duplicate"):
        theme_tool.load(str(path))


# --- exactly one theme is the default ----------------------------------------

def test_zero_default_themes_fails_the_build(tmp_path):
    decl = _decl()
    for entry in decl["themes"]:
        entry.pop("default", None)
    path = tmp_path / "theme.json"
    path.write_text(json.dumps(decl), encoding="utf-8")
    with pytest.raises(theme_tool.ThemeDeclarationError, match="no theme is marked"):
        theme_tool.load(str(path))


def test_more_than_one_default_theme_fails_the_build(tmp_path):
    decl = _decl()
    decl["themes"][1]["default"] = True
    path = tmp_path / "theme.json"
    path.write_text(json.dumps(decl), encoding="utf-8")
    with pytest.raises(theme_tool.ThemeDeclarationError, match="more than one"):
        theme_tool.load(str(path))


# --- a theme is colours, metrics and fonts together, not colours alone ------

def test_a_theme_may_override_some_colours_and_inherit_the_rest():
    decl = _decl()
    default_entry = decl["themes"][0]
    entry = {"name": "X", "colors": {"Text": [9, 9, 9, 255]}}
    merged = theme_tool.resolved_colors(default_entry, entry)
    assert merged["Text"] == [9, 9, 9, 255]
    assert merged["Border"] == default_entry["colors"]["Border"]


def test_a_theme_may_override_some_metrics_and_inherit_the_rest():
    decl = _decl()
    default_entry = decl["themes"][0]
    entry = dict(default_entry, name="X", default=False, metrics={"textScale": 5})
    merged = theme_tool.resolved_metrics(default_entry, entry)
    assert merged["textScale"] == 5
    assert merged["panelPadding"] == default_entry["metrics"]["panelPadding"]


def test_a_theme_with_no_fonts_inherits_the_default_themes_fonts():
    decl = _decl()
    default_entry = decl["themes"][0]
    entry = {"name": "X"}
    merged = theme_tool.resolved_fonts(default_entry, entry)
    assert merged["Default"] == default_entry["fonts"]["Default"]


def test_an_unknown_metric_override_key_fails_the_build(tmp_path):
    decl = _decl()
    decl["themes"][1]["metrics"] = {"notAMetric": 1}
    path = tmp_path / "theme.json"
    path.write_text(json.dumps(decl), encoding="utf-8")
    with pytest.raises(theme_tool.ThemeDeclarationError, match="notAMetric"):
        theme_tool.load(str(path))


def test_a_metrics_override_that_goes_out_of_range_fails_the_build(tmp_path):
    decl = _decl()
    decl["themes"][1]["metrics"] = {"textScale": 0}
    path = tmp_path / "theme.json"
    path.write_text(json.dumps(decl), encoding="utf-8")
    with pytest.raises(theme_tool.ThemeDeclarationError, match="textScale"):
        theme_tool.load(str(path))


def test_a_theme_naming_an_unknown_font_role_fails_the_build(tmp_path):
    decl = _decl()
    decl["themes"][1]["fonts"] = {"NotARole": "X"}
    path = tmp_path / "theme.json"
    path.write_text(json.dumps(decl), encoding="utf-8")
    with pytest.raises(theme_tool.ThemeDeclarationError, match="NotARole"):
        theme_tool.load(str(path))


def test_the_default_theme_must_declare_a_default_font(tmp_path):
    decl = _decl()
    decl["themes"][0]["fonts"] = {}
    path = tmp_path / "theme.json"
    path.write_text(json.dumps(decl), encoding="utf-8")
    with pytest.raises(theme_tool.ThemeDeclarationError, match="Default"):
        theme_tool.load(str(path))


def test_cooking_uses_each_themes_own_merged_metrics():
    decl = _decl()
    default_entry = decl["themes"][0]
    decl["themes"] = [
        default_entry,
        dict(default_entry, name="A", default=False, metrics={"textScale": 4}),
        dict(default_entry, name="B", default=False, metrics={}),
    ]
    payloads = theme_tool.cook_payloads(decl)
    a = themelib.describe(payloads["THEME_A"])
    b = themelib.describe(payloads["THEME_B"])
    assert a["metrics"]["textScale"] == 4
    assert b["metrics"]["textScale"] == default_entry["metrics"]["textScale"]


def test_cooking_uses_the_default_themes_fonts_when_a_theme_names_none():
    decl = _decl()
    payloads = theme_tool.cook_payloads(decl)
    mainmenu = themelib.describe(payloads["THEME_MAINMENU"])
    midnight = themelib.describe(payloads["THEME_MIDNIGHT"])
    assert mainmenu["fonts"] == midnight["fonts"]


def test_generated_table_assigns_metrics_per_theme(tmp_path):
    decl = _decl()
    default_entry = decl["themes"][0]
    decl["themes"] = [
        default_entry,
        dict(default_entry, name="A", default=False, metrics={"textScale": 4}),
        dict(default_entry, name="B", default=False, metrics={}),
    ]
    table = theme_tool.emit_table(decl, str(tmp_path / "UiThemeTable.cpp"))
    text = open(table, encoding="utf-8").read()
    body = text.split("UiStyle Ui_BuiltinTheme(UiBuiltinTheme theme)", 1)[1]
    case_a = body.split("case UiBuiltinTheme::A:", 1)[1].split("break;", 1)[0]
    case_b = body.split("case UiBuiltinTheme::B:", 1)[1].split("break;", 1)[0]
    assert "s.textScale = 4;" in case_a
    assert "s.textScale = {};".format(default_entry["metrics"]["textScale"]) in case_b


# --- the shipped declaration ------------------------------------------------

def test_shipped_declaration_is_valid_and_cooks():
    decl = theme_tool.load(DECLARATION)
    payloads = theme_tool.cook_payloads(decl)
    assert len(payloads) == len(decl["themes"])
    for name, blob in payloads.items():
        assert name.startswith("THEME_")
        d = themelib.describe(blob)
        assert len(d["colors"]) == len(themelib.COLOR_ROLES)


def test_shipped_declaration_generates_a_usable_table(tmp_path):
    decl = theme_tool.load(DECLARATION)
    ids = theme_tool.emit_ids(decl, str(tmp_path / "UiThemeIds.h"))
    table = theme_tool.emit_table(decl, str(tmp_path / "UiThemeTable.cpp"))
    ids_text = open(ids, encoding="utf-8").read()
    table_text = open(table, encoding="utf-8").read()
    for entry in decl["themes"]:
        assert entry["name"] in ids_text
        assert entry["name"] in table_text
    # Every role must be assigned in every theme, or a colour is left black.
    for role in themelib.COLOR_ROLES:
        assert table_text.count("UiColor::%s)" % role) == len(decl["themes"])


def test_shipped_cooklists_enable_themes_on_every_platform():
    for name in ("ps2", "win32", "vita", "psp", "nx"):
        path = os.path.join(ROOT, "engine", "config", name, "cooklist.json")
        with open(path, "r", encoding="utf-8") as fh:
            cl = json.load(fh)
        assert cl["assets"]["THEME"]["enabled"] is True, name
