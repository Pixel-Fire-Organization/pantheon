"""Tests for the cook stage, the cooked-asset validator and the inspectors.

The validator is the gate that stands between a bad cook and a container, so
these tests care most about it FAILING correctly. A validator that only ever
passes is worse than none: it converts a loud content error into a silent one.
"""

import importlib.util
import json
import pathlib
import struct

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"


def _load(name, relpath):
    import sys
    sys.path.insert(0, str(TOOLS))
    spec = importlib.util.spec_from_file_location(name, TOOLS / relpath)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ps2a = _load("ps2lib.ps2a", "ps2lib/ps2a.py")
tim2 = _load("ps2lib.tim2", "ps2lib/tim2.py")
cook_assets = _load("cook_assets", "cook_assets.py")
validate_cooked = _load("validate_cooked", "validate_cooked.py")
inspect_archive = _load("inspect_archive", "inspect_archive.py")


def _texture_blob(width=64, height=64, fmt="rgba32"):
    """A minimal valid TIM2 of the requested size and encoding."""
    if fmt == "rgba32":
        payload = b"\x00\x00\x00\x80" * (width * height)
        return tim2.assemble_tim2(width, height, [payload], tim2.TIM2_IMGTYPE_RGBA32, None)
    indices = bytes(width * height)
    clut = b"".join(struct.pack("<BBBB", i, i, i, 0x80) for i in range(256))
    return tim2.assemble_tim2(width, height, [indices], tim2.TIM2_IMGTYPE_IDTEX8, clut)


def _write_asset(directory, name, blob, deps=(), type_id=0):
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / name
    path.write_bytes(ps2a.write_ps2a(type_id, blob, list(deps), ".tm2"))
    return path


def _cooklist(**texture):
    policy = {"enabled": True, "format": "source"}
    policy.update(texture)
    return {"platform": "test", "assets": {"TEXTURE": policy, "MODEL": {"enabled": True}}}


def _run(directory, cooklist):
    report = validate_cooked.Report()
    validate_cooked.validate_tree(str(directory), cooklist, report)
    return report


# --- the format round-trips -------------------------------------------------

def test_ps2a_roundtrip(tmp_path):
    blob = _texture_blob()
    path = _write_asset(tmp_path, "A.PS2A", blob, deps=["RASSETS/B.PS2A"])
    info = ps2a.read_ps2a(str(path))
    assert info["type"] == "TEXTURE"
    assert info["deps"] == ["RASSETS/B.PS2A"]
    assert info["data_size"] == len(blob)


def test_tim2_describe_reports_the_encoded_format():
    assert tim2.describe(_texture_blob(fmt="rgba32"))["format"] == "rgba32"
    assert tim2.describe(_texture_blob(fmt="pal8"))["format"] == "pal8"


# --- the validator accepts a good tree --------------------------------------

def test_valid_tree_passes(tmp_path):
    _write_asset(tmp_path, "A.PS2A", _texture_blob())
    assert _run(tmp_path, _cooklist()).ok()


# --- ...and rejects every way a cook can be wrong ---------------------------

def test_bad_magic_is_rejected(tmp_path):
    path = _write_asset(tmp_path, "A.PS2A", _texture_blob())
    data = bytearray(path.read_bytes())
    data[0:4] = b"XXXX"
    path.write_bytes(bytes(data))
    assert not _run(tmp_path, _cooklist()).ok()


def test_truncated_payload_is_rejected(tmp_path):
    path = _write_asset(tmp_path, "A.PS2A", _texture_blob())
    data = path.read_bytes()
    path.write_bytes(data[:-64])  # header still claims the full payload
    assert not _run(tmp_path, _cooklist()).ok()


def test_format_the_cooklist_did_not_ask_for_is_rejected(tmp_path):
    _write_asset(tmp_path, "A.PS2A", _texture_blob(fmt="rgba32"))
    report = _run(tmp_path, _cooklist(format="pal8"))
    assert not report.ok()
    assert "pal8" in report.errors[0]


def test_oversized_texture_is_rejected(tmp_path):
    _write_asset(tmp_path, "A.PS2A", _texture_blob(width=128, height=128))
    assert not _run(tmp_path, _cooklist(max_width=64, max_height=64)).ok()


def test_disabled_type_is_rejected(tmp_path):
    _write_asset(tmp_path, "A.PS2A", _texture_blob())
    cooklist = {"platform": "test", "assets": {"MODEL": {"enabled": True}}}
    assert not _run(tmp_path, cooklist).ok()


def test_missing_dependency_is_rejected(tmp_path):
    # A dependency that was never cooked becomes a resource that never reports
    # ready - a hang on the target, so it must be caught here.
    _write_asset(tmp_path, "A.PS2A", _texture_blob(), deps=["RASSETS/GONE.PS2A"])
    report = _run(tmp_path, _cooklist())
    assert not report.ok()
    assert "GONE" in report.errors[0].upper()


def test_present_dependency_is_accepted(tmp_path):
    _write_asset(tmp_path, "A.PS2A", _texture_blob(), deps=["RASSETS/B.PS2A"])
    _write_asset(tmp_path, "B.PS2A", _texture_blob())
    assert _run(tmp_path, _cooklist()).ok()


def test_texture_budget_is_enforced(tmp_path):
    _write_asset(tmp_path, "A.PS2A", _texture_blob(width=128, height=128))
    assert not _run(tmp_path, _cooklist(budget_bytes=1024)).ok()


def test_empty_tree_is_rejected(tmp_path):
    assert not _run(tmp_path, _cooklist()).ok()


def test_missing_tree_is_rejected(tmp_path):
    assert not _run(tmp_path / "never-cooked", _cooklist()).ok()


# --- cook list resolution ---------------------------------------------------

def test_regional_variants_share_their_base_cooklist():
    pal = cook_assets.cooklist_for_platform(str(ROOT), "ps2pal")
    ntsc = cook_assets.cooklist_for_platform(str(ROOT), "ps2ntsc")
    assert pal == ntsc
    assert pathlib.Path(pal).is_file()


def test_shipped_cooklists_match_the_schema():
    schema = json.loads((ROOT / "tools/schemas/cooklist.schema.json").read_text())
    allowed_classes = set(schema["properties"]["assets"]["properties"])
    for name in ("ps2", "win32", "vita", "psp", "nx"):
        data = json.loads((ROOT / "engine/config" / name / "cooklist.json").read_text())
        assert data["platform"] == name
        assert set(data["assets"]) <= allowed_classes
        fmt = data["assets"]["TEXTURE"].get("format")
        assert fmt in schema["properties"]["assets"]["properties"]["TEXTURE"]["properties"]["format"]["enum"]
