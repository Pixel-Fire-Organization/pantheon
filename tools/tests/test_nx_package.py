"""Tests for the nx package reader.

The shipped declaration must stay packageable, and the failure modes that
matter are the silent ones: the container tool embeds whatever icon it is handed
and the launcher draws it wrong or not at all. Examples are packaged from their
own title declarations, so identity folding is checked against more than one.
"""

import json
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import nx_package  # noqa: E402

CONFIG = ROOT / "game" / "config" / "platform" / "nx" / "package.json"
SCHEMA = ROOT / "tools" / "schemas" / "package.schema.json"
TITLE = ROOT / "game" / "config" / "title.json"


def _jpeg(path, width, height):
    """Write the smallest byte sequence the size reader accepts as a JPEG."""
    sof = b"\xFF\xC0" + (17).to_bytes(2, "big") + b"\x08" + height.to_bytes(2, "big") + width.to_bytes(2, "big") + b"\x03" + b"\x00" * 9
    path.write_bytes(b"\xFF\xD8" + b"\xFF\xE0\x00\x04\x00\x00" + sof + b"\xFF\xD9")


def _declaration(tmp_path, icon_name="icon.jpg", **extra):
    config = {"platform": "nx", "nro": {"icon": icon_name}}
    config.update(extra)
    path = tmp_path / "package.json"
    path.write_text(json.dumps(config), encoding="utf-8")
    return path


# --- the shipped declaration ------------------------------------------------

def test_shipped_declaration_loads():
    config = nx_package.load(str(CONFIG), str(SCHEMA), str(TITLE))
    declaration = json.loads(TITLE.read_text(encoding="utf-8"))
    assert config["platform"] == "nx"
    assert config["title"]["name"] == declaration["name"]
    assert config["title"]["author"] == declaration["developer"]
    assert config["title"]["version"] == declaration["version"]


def test_identity_is_not_restated_in_the_platform_declaration():
    raw = json.loads(CONFIG.read_text(encoding="utf-8"))
    assert "title" not in raw


def test_no_title_identifier_is_needed():
    """A homebrew executable has no identifier to be filed under, so the shared
    declaration need not carry one for this platform."""
    declaration = json.loads(TITLE.read_text(encoding="utf-8"))
    assert "nx" not in declaration["ids"]
    config = nx_package.load(str(CONFIG), str(SCHEMA), str(TITLE))
    assert "id" not in config["title"]


def test_shipped_icon_is_the_launcher_size():
    icon = CONFIG.parent / json.loads(CONFIG.read_text(encoding="utf-8"))["nro"]["icon"]
    assert nx_package.jpeg_size(str(icon)) == nx_package.ICON_SIZE


def test_shipped_declaration_matches_the_schema():
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    raw = json.loads(CONFIG.read_text(encoding="utf-8"))
    assert set(raw) <= set(schema["properties"])
    assert set(raw["nro"]) <= set(schema["properties"]["nro"]["properties"])


def test_an_example_is_packaged_under_its_own_identity():
    example_title = ROOT / "examples" / "primitives" / "config" / "title.json"
    config = nx_package.load(str(CONFIG), str(SCHEMA), str(example_title))
    assert config["title"]["name"] == json.loads(example_title.read_text(encoding="utf-8"))["name"]


# --- the schema still requires an identifier where a container carries one ---

def test_schema_still_requires_an_identifier_of_the_playstation_containers():
    jsonschema = pytest.importorskip("jsonschema")
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    without_id = {"platform": "psp", "title": {"name": "T", "version": "01.00"}}
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(without_id, schema)
    jsonschema.validate({"platform": "nx", "title": {"name": "T", "version": "01.00"}, "nro": {"icon": "i.jpg"}}, schema)


# --- validation refuses what the launcher would accept and draw wrongly ------

def test_wrong_icon_size_is_refused(tmp_path):
    _jpeg(tmp_path / "icon.jpg", 128, 128)
    with pytest.raises(nx_package.NxPackageError, match="128x128, expected 256x256"):
        nx_package.load(str(_declaration(tmp_path)), str(SCHEMA), str(TITLE))


def test_png_icon_is_refused(tmp_path):
    (tmp_path / "icon.png").write_bytes(b"\x89PNG\r\n\x1a\n" + b"\x00" * 32)
    with pytest.raises(nx_package.NxPackageError, match="not a JPEG"):
        nx_package.load(str(_declaration(tmp_path, "icon.png")), str(SCHEMA), str(TITLE))


def test_missing_icon_is_refused(tmp_path):
    with pytest.raises(nx_package.NxPackageError, match="missing file"):
        nx_package.load(str(_declaration(tmp_path)), str(SCHEMA), str(TITLE))


def test_missing_staged_file_is_refused(tmp_path):
    _jpeg(tmp_path / "icon.jpg", 256, 256)
    path = _declaration(tmp_path, files=[{"src": "absent.bin", "dst": "absent.bin"}])
    with pytest.raises(nx_package.NxPackageError, match="files\\[\\] entry"):
        nx_package.load(str(path), str(SCHEMA), str(TITLE))


def test_wrong_platform_is_refused(tmp_path):
    path = tmp_path / "package.json"
    path.write_text(json.dumps({"platform": "psp"}), encoding="utf-8")
    with pytest.raises(nx_package.NxPackageError, match="expected 'nx'"):
        nx_package.load(str(path), str(SCHEMA), str(TITLE))


# --- what the fragment reads -------------------------------------------------

def test_emitted_cmake_carries_identity_icon_and_files(tmp_path):
    _jpeg(tmp_path / "icon.jpg", 256, 256)
    (tmp_path / "extra.bin").write_bytes(b"x")
    path = _declaration(tmp_path, files=[{"src": "extra.bin", "dst": "EXTRA.BIN"}])
    config = nx_package.load(str(path), str(SCHEMA), str(TITLE))

    out = tmp_path / "package.cmake"
    nx_package.emit_cmake(config, str(tmp_path), str(out))
    text = out.read_text(encoding="utf-8")

    declaration = json.loads(TITLE.read_text(encoding="utf-8"))
    assert 'set(ENGINE_NX_TITLE_NAME "{}")'.format(declaration["name"]) in text
    assert 'set(ENGINE_NX_TITLE_AUTHOR "{}")'.format(declaration["developer"]) in text
    assert 'set(ENGINE_NX_TITLE_VERSION "{}")'.format(declaration["version"]) in text
    assert "icon.jpg" in text
    assert "|EXTRA.BIN" in text


def test_emitted_cmake_never_uses_the_vendor_namespace(tmp_path):
    """devkitPro's CMake owns every NX_ variable; shadowing one silently breaks
    its tool lookup."""
    _jpeg(tmp_path / "icon.jpg", 256, 256)
    config = nx_package.load(str(_declaration(tmp_path)), str(SCHEMA), str(TITLE))
    out = tmp_path / "package.cmake"
    nx_package.emit_cmake(config, str(tmp_path), str(out))
    for line in out.read_text(encoding="utf-8").splitlines():
        if line.startswith("set("):
            assert line.startswith("set(ENGINE_NX_")


def test_a_semicolon_in_the_name_is_refused(tmp_path):
    _jpeg(tmp_path / "icon.jpg", 256, 256)
    path = tmp_path / "package.json"
    path.write_text(json.dumps({"platform": "nx", "nro": {"icon": "icon.jpg"}, "title": {"name": "A;B"}}), encoding="utf-8")
    with pytest.raises(nx_package.NxPackageError, match="cannot pass through"):
        nx_package.load(str(path), str(SCHEMA), str(TITLE))
