"""The theme editor's pure logic: field resolution and display state.

Deliberately excludes the tkinter widget code -- these tests run headless, so
only the half of theme_editor.py with no display dependency is covered here.
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import theme_editor as editor  # noqa: E402

DECLARATION = os.path.join(ROOT, "game", "config", "theme.json")


def _decl():
    with open(DECLARATION, "r", encoding="utf-8-sig") as fh:
        return json.load(fh)


def test_find_default_returns_the_one_marked():
    decl = _decl()
    default_entry = editor.find_default(decl)
    assert default_entry is not None
    assert default_entry.get("default") is True


def test_find_default_returns_none_when_none_marked():
    decl = _decl()
    for entry in decl["themes"]:
        entry.pop("default", None)
    assert editor.find_default(decl) is None


def test_find_default_returns_none_when_more_than_one_marked():
    decl = _decl()
    decl["themes"][1]["default"] = True
    assert editor.find_default(decl) is None


def test_field_value_reports_its_own_value_as_not_inherited():
    decl = _decl()
    default_entry = editor.find_default(decl)
    value, inherited = editor.field_value(decl, default_entry, "metrics", "textScale")
    assert value == default_entry["metrics"]["textScale"]
    assert inherited is False


def test_field_value_falls_through_to_the_default_theme():
    decl = _decl()
    default_entry = editor.find_default(decl)
    mainmenu = next(e for e in decl["themes"] if e["name"] == "MAINMENU")
    value, inherited = editor.field_value(decl, mainmenu, "metrics", "textScale")
    assert value == default_entry["metrics"]["textScale"]
    assert inherited is True


def test_field_value_is_none_when_nothing_provides_it():
    decl = {"themes": [{"name": "ONLY", "default": True}]}
    value, inherited = editor.field_value(decl, decl["themes"][0], "colors", "Text")
    assert value is None
    assert inherited is False


def test_set_and_clear_own_field_round_trip():
    entry = {"name": "X"}
    editor.set_own_field(entry, "metrics", "textScale", 5)
    assert entry["metrics"]["textScale"] == 5
    editor.clear_own_field(entry, "metrics", "textScale")
    assert "textScale" not in entry["metrics"]


def test_clear_own_field_on_a_field_never_set_is_a_no_op():
    entry = {"name": "X", "metrics": {}}
    editor.clear_own_field(entry, "metrics", "textScale")
    assert entry["metrics"] == {}


def test_preview_style_resolves_colours_metrics_and_fonts():
    decl = _decl()
    mainmenu = next(e for e in decl["themes"] if e["name"] == "MAINMENU")
    style = editor.preview_style(decl, mainmenu)
    default_entry = editor.find_default(decl)
    assert style["colors"] == default_entry["colors"]
    assert style["metrics"] == default_entry["metrics"]
    assert style["fonts"] == default_entry["fonts"]


def test_new_theme_entry_has_no_overrides():
    entry = editor.new_theme_entry("FRESH")
    assert entry["name"] == "FRESH"
    assert entry["colors"] == {}
    assert entry["metrics"] == {}
    assert entry["fonts"] == {}


def test_rgba_to_hex_and_back():
    assert editor.rgba_to_hex([18, 52, 86, 255]) == "#123456"
    assert editor.hex_to_rgb("#123456") == [18, 52, 86]


def test_metric_is_float_only_for_the_two_timing_fields():
    assert editor.metric_is_float("repeatDelaySeconds")
    assert editor.metric_is_float("repeatIntervalSeconds")
    assert not editor.metric_is_float("textScale")


def test_metric_range_matches_the_declared_ranges():
    from ps2lib import theme as themelib

    assert editor.metric_range("textScale") == themelib.METRIC_RANGES["textScale"]
    assert editor.metric_range("repeatDelaySeconds") == themelib.REPEAT_RANGE
