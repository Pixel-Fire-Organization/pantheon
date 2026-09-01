"""The cooked font format, and the checks that keep a bad one out of a build.

Structure only, never pixels: the atlas is authored art and its bytes are not a
contract, but the metrics table and every refusal below are.
"""

import os
import re
import subprocess
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from ps2lib import font, ps2a, tim2  # noqa: E402
import validate_cooked  # noqa: E402


def _glyphs(count=font.GLYPH_COUNT, advance=6, w=5, h=7):
    return [{"u": 0, "v": 0, "w": w, "h": h, "bearingX": 0, "bearingY": 0, "advance": advance} for _ in range(count)]


def _cells(count=2, w=9, h=9):
    return [{"u": 0, "v": 0, "w": w, "h": h, "bearingX": 0, "bearingY": 0, "advance": w} for _ in range(count)]


def _font(**over):
    args = {"glyphs": _glyphs(), "atlas_w": 128, "atlas_h": 64, "line_height": 7,
            "baseline": 7, "space_advance": 6, "cells": []}
    args.update(over)
    return font.write_font(args["glyphs"], args["atlas_w"], args["atlas_h"], args["line_height"],
                           args["baseline"], args["space_advance"], args.get("missing_index", 0),
                           args["cells"])


# --- the format round-trips -------------------------------------------------

def test_round_trip_preserves_every_field():
    blob, ext = _font(cells=_cells())
    assert ext == ".fnt"
    d = font.describe(blob)
    assert d["glyph_count"] == font.GLYPH_COUNT
    assert d["first_code"] == font.FIRST_CODE
    assert d["atlas_width"] == 128 and d["atlas_height"] == 64
    assert d["line_height"] == 7 and d["baseline"] == 7
    assert d["cell_count"] == 2
    assert len(d["glyphs"]) == font.GLYPH_COUNT
    assert len(d["cells"]) == 2


def test_payload_size_is_exactly_the_declared_layout():
    blob, _ = _font()
    assert len(blob) == font.HEADER_SIZE + font.GLYPH_COUNT * font.GLYPH_SIZE


def test_payload_size_grows_by_exactly_one_record_per_cell():
    blob, _ = _font(cells=_cells(count=3))
    assert len(blob) == font.HEADER_SIZE + font.GLYPH_COUNT * font.GLYPH_SIZE + 3 * font.GLYPH_SIZE


def test_uniform_advances_are_reported_as_monospaced():
    assert font.describe(_font()[0])["monospaced"] is True
    varied = _glyphs()
    varied[10]["advance"] = 3
    assert font.describe(_font(glyphs=varied)[0])["monospaced"] is False


# --- the writer refuses what the runtime could not draw ---------------------

def test_wrong_glyph_count_is_refused():
    with pytest.raises(font.FontError, match="expected"):
        _font(glyphs=_glyphs(count=10))


def test_non_power_of_two_atlas_is_refused():
    # The console stores texture dimensions as exponents, so this is unusable
    # there however permissive another platform might be.
    with pytest.raises(font.FontError, match="power-of-two"):
        _font(atlas_w=120)


def test_glyph_outside_the_atlas_is_refused():
    bad = _glyphs()
    bad[5]["u"] = 126
    bad[5]["w"] = 8
    with pytest.raises(font.FontError, match="outside"):
        _font(glyphs=bad)


def test_zero_advance_is_refused():
    bad = _glyphs()
    bad[40]["advance"] = 0
    with pytest.raises(font.FontError, match="zero advance"):
        _font(glyphs=bad)


def test_substitute_glyph_outside_the_table_is_refused():
    with pytest.raises(font.FontError, match="missing glyph"):
        _font(missing_index=font.GLYPH_COUNT)


def test_a_font_with_no_cells_is_exactly_as_valid_as_one_with_some():
    assert font.describe(_font()[0])["cell_count"] == 0


def test_cell_outside_the_atlas_is_refused():
    bad = _cells()
    bad[1]["u"] = 126
    bad[1]["w"] = 8
    with pytest.raises(font.FontError, match="outside"):
        _font(cells=bad)


def test_a_cell_may_have_a_zero_advance_unlike_a_glyph():
    # Icons are not stacked in a run of their own the way glyphs are, so
    # nothing about a zero advance is an authoring slip for one.
    cells = _cells()
    cells[0]["advance"] = 0
    d = font.describe(_font(cells=cells)[0])
    assert d["cells"][0]["advance"] == 0


def test_too_many_cells_is_refused():
    with pytest.raises(font.FontError, match="cells"):
        _font(cells=_cells(count=font.MAX_CELLS + 1))


def test_cell_table_declared_at_the_wrong_offset_is_refused():
    blob, _ = _font(cells=_cells())
    # The offset field sits right after cellCount, both in the header's last
    # two uint16 slots.
    corrupted = bytearray(blob)
    corrupted[26:28] = (9999).to_bytes(2, "little")
    with pytest.raises(font.FontError, match="cell table"):
        font.describe(bytes(corrupted))


# --- the reader refuses a payload it cannot trust ---------------------------

def test_bad_magic_is_refused():
    blob, _ = _font()
    with pytest.raises(font.FontError, match="magic"):
        font.describe(b"XXXX" + blob[4:])


def test_a_future_version_is_refused_rather_than_guessed():
    blob, _ = _font()
    bumped = blob[:4] + (font.VERSION + 1).to_bytes(2, "little") + blob[6:]
    with pytest.raises(font.FontError, match="version"):
        font.describe(bumped)


def test_truncated_payload_is_refused():
    blob, _ = _font()
    with pytest.raises(font.FontError):
        font.describe(blob[:-4])


# --- the constants cannot drift from the runtime ----------------------------

def test_layout_constants_match_the_engine_header():
    header = os.path.join(ROOT, "engine", "include", "graphics", "FontFormat.h")
    with open(header, "r", encoding="utf-8") as fh:
        text = fh.read()

    def define(name):
        m = re.search(r"#define\s+%s\s+(\d+)" % name, text)
        assert m, "%s missing from FontFormat.h" % name
        return int(m.group(1))

    assert define("FONT_HEADER_SIZE") == font.HEADER_SIZE
    assert define("FONT_GLYPH_SIZE") == font.GLYPH_SIZE
    assert define("FONT_VERSION") == font.VERSION
    magic = re.search(r"#define\s+FONT_MAGIC\s+0x([0-9A-Fa-f]+)", text)
    assert magic and bytes.fromhex(magic.group(1))[::-1] == font.MAGIC


def test_shipped_default_font_cooks_and_is_readable():
    src = os.path.join(ROOT, "assets", "engine", "ENGINE_FONT.FNT")
    if not os.path.isfile(src):
        pytest.skip("the default font source is not present")
    import json
    with open(src, "r", encoding="utf-8-sig") as fh:
        metrics = json.load(fh)
    blob, _ = font.write_font(metrics["glyphs"], metrics["atlas_width"], metrics["atlas_height"],
                              metrics["line_height"], metrics["baseline"], metrics["space_advance"],
                              metrics["missing_index"], metrics.get("cells"))
    d = font.describe(blob)
    assert d["glyph_count"] == font.GLYPH_COUNT
    # Lowercase is the whole reason this font exists alongside the built-in one.
    assert d["glyphs"][ord("a") - font.FIRST_CODE]["w"] > 0
    assert d["glyphs"][ord("g") - font.FIRST_CODE]["bearingY"] > 0
    # The icon and controller-glyph set the interface's atlas cells serve.
    assert d["cell_count"] == len(metrics["cells"]) > 0


# --- the tree gate ----------------------------------------------------------

def _tree(tmp_path, *, atlas=True, atlas_size=(128, 64), filt="nearest"):
    d = tmp_path / "rassets"
    d.mkdir()
    blob, _ = _font()
    (d / "ENGINE_FONT.PS2A").write_bytes(
        ps2a.write_ps2a(ps2a.TYPE_MAP["FONT"], blob, ["RASSETS/ENGINE_FONT_ATLAS.PS2A"], ".fnt"))
    if atlas:
        w, h = atlas_size
        image = tim2.assemble_tim2(w, h, [b"\x00" * (w * h)], tim2.TIM2_IMGTYPE_IDTEX8,
                                   b"\x00" * 1024, tim2.tex1_for_filter(filt))
        (d / "ENGINE_FONT_ATLAS.PS2A").write_bytes(
            ps2a.write_ps2a(ps2a.TYPE_MAP["TEXTURE"], image, [], ".tm2"))
    return d


COOKLIST = {"platform": "test", "assets": {"TEXTURE": {"enabled": True, "format": "source"},
                                           "FONT": {"enabled": True}}}


def _validate(directory):
    report = validate_cooked.Report()
    validate_cooked.validate_tree(str(directory), COOKLIST, report)
    return report


def test_a_consistent_font_tree_passes(tmp_path):
    assert _validate(_tree(tmp_path)).ok()


def test_a_font_whose_atlas_was_not_cooked_is_rejected(tmp_path):
    report = _validate(_tree(tmp_path, atlas=False))
    assert not report.ok()
    assert any("ENGINE_FONT_ATLAS" in e for e in report.errors)


def test_metrics_disagreeing_with_the_atlas_are_rejected(tmp_path):
    report = _validate(_tree(tmp_path, atlas_size=(64, 64)))
    assert not report.ok()
    assert any("atlas" in e for e in report.errors)


def test_a_linearly_filtered_atlas_is_warned_about(tmp_path):
    report = _validate(_tree(tmp_path, filt="linear"))
    assert report.ok()
    assert any("nearest" in w for w in report.warnings)


def test_font_is_rejected_where_the_cook_list_disables_it(tmp_path):
    report = validate_cooked.Report()
    disabled = {"platform": "test", "assets": {"TEXTURE": {"enabled": True, "format": "source"},
                                               "FONT": {"enabled": False}}}
    validate_cooked.validate_tree(str(_tree(tmp_path)), disabled, report)
    assert not report.ok()
    assert any("not enabled" in e for e in report.errors)


def test_shipped_cooklists_enable_fonts_on_every_platform():
    for name in ("ps2", "win32", "vita", "psp", "nx"):
        path = os.path.join(ROOT, "engine", "config", name, "cooklist.json")
        import json
        with open(path, "r", encoding="utf-8") as fh:
            cl = json.load(fh)
        assert cl["assets"]["FONT"]["enabled"] is True, name
