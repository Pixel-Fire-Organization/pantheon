"""Tests for tools/loading_screen.py — the built-in loading screen's images.

The declaration is compiled into the binary (see docs/subsystems/SCENE.md), so
a malformed one must fail at configure time rather than producing a header
with an empty or bogus image list.
"""

import importlib.util
import json
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


loading_screen = _load("loading_screen", "tools/loading_screen.py")


def _declare(tmp_path, **over):
    doc = {"images": ["RASSETS/LOADING1.PS2A"], "cycleSeconds": 2.5}
    doc.update(over)
    path = tmp_path / "loading_screen.json"
    path.write_text(json.dumps(doc), encoding="utf-8")
    return path


def test_a_valid_declaration_loads(tmp_path):
    declaration = loading_screen.load(str(_declare(tmp_path)))
    assert declaration["images"] == ["RASSETS/LOADING1.PS2A"]


def test_empty_image_list_rejected(tmp_path):
    with pytest.raises(loading_screen.LoadingScreenError, match="no images"):
        loading_screen.load(str(_declare(tmp_path, images=[])))


def test_missing_images_field_rejected(tmp_path):
    path = tmp_path / "loading_screen.json"
    path.write_text(json.dumps({"cycleSeconds": 1.0}), encoding="utf-8")
    with pytest.raises(loading_screen.LoadingScreenError, match="no images"):
        loading_screen.load(str(path))


@pytest.mark.parametrize("bad", ["", 5, None])
def test_non_string_or_empty_image_rejected(tmp_path, bad):
    with pytest.raises(loading_screen.LoadingScreenError, match="non-empty string"):
        loading_screen.load(str(_declare(tmp_path, images=[bad])))


@pytest.mark.parametrize("bad", [0, -1.0, "3"])
def test_cycle_seconds_must_be_a_positive_number(tmp_path, bad):
    with pytest.raises(loading_screen.LoadingScreenError, match="cycleSeconds"):
        loading_screen.load(str(_declare(tmp_path, cycleSeconds=bad)))


def test_cycle_seconds_defaults_when_absent(tmp_path):
    path = tmp_path / "loading_screen.json"
    path.write_text(json.dumps({"images": ["RASSETS/LOADING1.PS2A"]}), encoding="utf-8")
    declaration = loading_screen.load(str(path))
    assert declaration.get("cycleSeconds", loading_screen.DEFAULT_CYCLE_SECONDS) == loading_screen.DEFAULT_CYCLE_SECONDS


def test_malformed_json_is_reported_as_such(tmp_path):
    path = tmp_path / "loading_screen.json"
    path.write_text("{ not json", encoding="utf-8")
    with pytest.raises(loading_screen.LoadingScreenError, match="not valid JSON"):
        loading_screen.load(str(path))


def test_emit_header_writes_every_image_and_the_cycle_duration(tmp_path):
    declaration = loading_screen.load(
        str(_declare(tmp_path, images=["RASSETS/A.PS2A", "RASSETS/B.PS2A"], cycleSeconds=4.0)))
    out = tmp_path / "generated" / "LoadingScreenAssets.h"
    loading_screen.emit_header(declaration, str(out))
    text = out.read_text(encoding="utf-8")
    assert '"RASSETS/A.PS2A"' in text
    assert '"RASSETS/B.PS2A"' in text
    assert "kImageCount = 2" in text
    assert "kCycleSeconds = 4.0f" in text


def test_the_shipped_declaration_is_valid():
    declaration = loading_screen.load(str(ROOT / "game" / "config" / "loading_screen.json"))
    assert declaration["images"]
