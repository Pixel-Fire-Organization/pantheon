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
    for name in ("ps2", "win32", "vita", "psp", "nx", "macos"):
        data = json.loads((ROOT / "engine/config" / name / "cooklist.json").read_text())
        assert data["platform"] == name
        assert set(data["assets"]) <= allowed_classes
        fmt = data["assets"]["TEXTURE"].get("format")
        assert fmt in schema["properties"]["assets"]["properties"]["TEXTURE"]["properties"]["format"]["enum"]


# --- material map baking must respect the platform's IO read buffer ---------
# A cooked material map (albedo/normal/ORM) is read back through EngineIO's
# single fixed-size shared read buffer at runtime, exactly like a level's own
# baked-in materials (see tools/tests/test_compile_level.py's matching
# LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM tests) -- an asset bigger than that is
# rejected at load, silently as far as cook/validate_cooked are concerned
# (see EX-0018 in docs/fixed_issues/issues.json).

_PLATFORM_HEADERS_FOR_IO_BUFFER = {
    "ps2": ROOT / "engine" / "include" / "platform" / "ps2" / "PlatformConstantsPs2.h",
    "vita": ROOT / "engine" / "include" / "platform" / "vita" / "PlatformConstantsVita.h",
    "win32": ROOT / "engine" / "include" / "platform" / "win32" / "PlatformConstants.h",
    "psp": ROOT / "engine" / "include" / "platform" / "psp" / "PlatformConstants.h",
    "nx": ROOT / "engine" / "include" / "platform" / "nx" / "PlatformConstants.h",
    "macos": ROOT / "engine" / "include" / "platform" / "macos" / "PlatformConstants.h",
}


def _product_of_io_buffer_literals(expression, source):
    """Same narrow "product of integer literals" parse
    test_compile_level.py's own _product_of_literals uses, kept as its own
    copy here rather than a cross-test-file import."""
    import re
    product = 1
    for token in expression.replace(" ", "").split("*"):
        assert token.isdigit(), f"{source}: IO_READ_BUFFER_SIZE is not a product of integer literals ({expression!r})"
        product *= int(token)
    return product


def test_io_read_buffer_size_by_platform_matches_every_platform_header():
    """cook_assets.IO_READ_BUFFER_SIZE_BY_PLATFORM must not exceed each
    platform's own IO_READ_BUFFER_SIZE, the same invariant
    test_compile_level.py's test_level_texture_max_bytes_matches_every_
    platform_header already holds compile_level.py's own copy to."""
    import re
    for platform, header_path in _PLATFORM_HEADERS_FOR_IO_BUFFER.items():
        header = header_path.read_text(encoding="utf-8")
        m = re.search(r"#define\s+IO_READ_BUFFER_SIZE\s+\(([^)]+)\)", header)
        assert m, f"IO_READ_BUFFER_SIZE not found in {header_path}"
        buffer_size = _product_of_io_buffer_literals(m.group(1), header_path)
        assert cook_assets.IO_READ_BUFFER_SIZE_BY_PLATFORM[platform] <= buffer_size


def test_cook_assets_io_read_buffer_sizes_match_compile_level():
    """The two tools keep independent copies of the same per-platform
    ceiling (cook_assets.py bakes standalone material maps, compile_level.py
    bakes a level's own materials) -- they must agree, or a level and a
    standalone material asset for the same platform would silently disagree
    about what that platform's IO read buffer actually holds."""
    compile_level = _load("compile_level", "compile_level.py")
    assert cook_assets.IO_READ_BUFFER_SIZE_BY_PLATFORM == compile_level.LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM


def test_bake_material_map_respects_the_io_read_buffer_size(tmp_path):
    """A material map with no dimension cap tight enough to matter on its
    own (or none at all, like albedo) must still be downscaled until its
    cooked .ps2a fits max_bytes -- the same safety net
    compile_level.py's own _bake_material already has for a level's baked-in
    materials (test_compile_level.py's
    test_bake_material_respects_the_level_texture_dimension_cap)."""
    PILImage = pytest.importorskip("PIL.Image")

    big = PILImage.new("RGB", (1024, 1024), (32, 64, 128))
    src = tmp_path / "HUGE.png"
    big.save(src)

    texture_policy = {"format": "rgba32", "max_width": 4096, "max_height": 4096}
    dst_dir = tmp_path / "out"
    dst_dir.mkdir()
    small_budget = 64 * 1024

    cook_assets._bake_material_map(str(src), "HUGE_ALBEDO", texture_policy, str(dst_dir), max_bytes=small_budget)

    out_path = dst_dir / "HUGE_ALBEDO.PS2A"
    assert out_path.is_file()
    assert out_path.stat().st_size <= small_budget


def test_bake_material_payload_applies_the_current_platforms_io_buffer_cap(tmp_path):
    """End to end through _bake_material_payload (the entry point
    cook_assets.py's MATERIAL cook path actually calls): every one of a
    material's maps must come out no bigger than the requesting platform's
    own IO_READ_BUFFER_SIZE_BY_PLATFORM entry, with no dimension cap
    declared in the cook list at all -- reproducing the exact shape of
    EX-0018 (a real material authored against 4096x4096 source art)."""
    PILImage = pytest.importorskip("PIL.Image")

    big = PILImage.new("RGB", (4096, 4096), (200, 120, 40))
    big.save(tmp_path / "ALBEDO.png")

    meta = {"type": "MATERIAL", "shaderType": "pbr_standard", "albedo": "ALBEDO.png"}
    cooklist = {
        "platform": "win32",
        "assets": {"TEXTURE": {"format": "rgba32", "max_width": 4096, "max_height": 4096},
                   "MATERIAL": {"enabled": True, "bake_normal": True, "bake_orm": True}},
    }
    dst_dir = tmp_path / "out"
    dst_dir.mkdir()

    result = cook_assets._bake_material_payload(meta, str(tmp_path / "MAT.json"), str(tmp_path), str(dst_dir), cooklist)
    assert result is not None

    budget = cook_assets.IO_READ_BUFFER_SIZE_BY_PLATFORM["win32"]
    baked = list(dst_dir.glob("*.PS2A"))
    assert baked, "expected at least the albedo map to be baked"
    for f in baked:
        assert f.stat().st_size <= budget, f"{f.name} is {f.stat().st_size} bytes, over win32's {budget}-byte IO buffer"
