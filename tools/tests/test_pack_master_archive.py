"""Tests for tools/pack_master_archive.py — merging cooked rassets and every
compiled level's archive into the one master .PS2R that actually ships (see
docs/subsystems/ARCHIVE.md). Each per-level .PS2R is built the same way
tools/compile_level.py builds one (tools/pack_archive.py), then read back out
and folded in, byte-identical.
"""

import importlib.util
import os
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load(name, relpath):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / relpath)
    module = importlib.util.module_from_spec(spec)
    import sys
    sys.path.insert(0, str(ROOT / "tools"))
    spec.loader.exec_module(module)
    return module


pack_archive = _load("pack_archive", "pack_archive.py")
pack_master_archive = _load("pack_master_archive", "pack_master_archive.py")


def _write_level_archive(path, level_name, entries):
    """entries: list of (key, bytes), as tools/compile_level.py would order them."""
    pack_archive.write_archive(entries, str(path))


def test_merges_rassets_and_every_level(tmp_path):
    rassets_dir = tmp_path / "rassets"
    rassets_dir.mkdir()
    (rassets_dir / "BOX.PS2A").write_bytes(b"box-payload")

    levels_dir = tmp_path / "levels"
    levels_dir.mkdir()
    _write_level_archive(levels_dir / "CITY.PS2R", "CITY", [
        ("CITY.PS2L", b"city-core"),
        ("CITY/S000_000.SEC", b"city-sector"),
        ("CITY/WOOD.PS2A", b"city-wood-texture"),
    ])
    _write_level_archive(levels_dir / "ARENA.PS2R", "ARENA", [
        ("ARENA.PS2L", b"arena-core"),
        ("ARENA/S000_000.SEC", b"arena-sector"),
    ])

    dst = tmp_path / "RASSETS.PS2R"
    pack_master_archive.build_master_archive(str(rassets_dir), str(levels_dir), str(dst))

    toc = pack_archive.read_toc(str(dst))
    by_key = {e["key"]: e for e in toc["entries"]}

    assert set(by_key) == {
        "RASSETS/BOX.PS2A",
        "CITY.PS2L", "CITY/S000_000.SEC", "CITY/WOOD.PS2A",
        "ARENA.PS2L", "ARENA/S000_000.SEC",
    }
    assert pack_archive.read_payload(str(dst), by_key["RASSETS/BOX.PS2A"]) == b"box-payload"
    assert pack_archive.read_payload(str(dst), by_key["CITY/WOOD.PS2A"]) == b"city-wood-texture"
    assert pack_archive.read_payload(str(dst), by_key["ARENA.PS2L"]) == b"arena-core"


def test_rassets_come_first_then_levels_in_name_order(tmp_path):
    """Locality: small, always-needed rassets first, then each level's own
    entries kept together and in the order compile_level.py wrote them."""
    rassets_dir = tmp_path / "rassets"
    rassets_dir.mkdir()
    (rassets_dir / "FONT.PS2A").write_bytes(b"font")

    levels_dir = tmp_path / "levels"
    levels_dir.mkdir()
    _write_level_archive(levels_dir / "ZOO.PS2R", "ZOO", [("ZOO.PS2L", b"zoo-core")])
    _write_level_archive(levels_dir / "ALPHA.PS2R", "ALPHA", [("ALPHA.PS2L", b"alpha-core")])

    dst = tmp_path / "RASSETS.PS2R"
    pack_master_archive.build_master_archive(str(rassets_dir), str(levels_dir), str(dst))

    toc = pack_archive.read_toc(str(dst))
    keys_in_order = [e["key"] for e in toc["entries"]]
    assert keys_in_order == ["RASSETS/FONT.PS2A", "ALPHA.PS2L", "ZOO.PS2L"]


def test_colliding_key_between_rasset_and_level_raises(tmp_path):
    rassets_dir = tmp_path / "rassets"
    rassets_dir.mkdir()
    (rassets_dir / "CITY.PS2L").write_bytes(b"not-actually-a-level-core")

    levels_dir = tmp_path / "levels"
    levels_dir.mkdir()
    _write_level_archive(levels_dir / "CITY.PS2R", "CITY", [("RASSETS/CITY.PS2L", b"city-core")])

    dst = tmp_path / "RASSETS.PS2R"
    with pytest.raises(ValueError, match="duplicate canonical key"):
        pack_master_archive.build_master_archive(str(rassets_dir), str(levels_dir), str(dst))


def test_missing_directories_produce_an_empty_archive(tmp_path):
    dst = tmp_path / "RASSETS.PS2R"
    stats = pack_master_archive.build_master_archive(
        str(tmp_path / "no-rassets"), str(tmp_path / "no-levels"), str(dst))
    assert stats["entry_count"] == 0
    assert pack_archive.read_toc(str(dst))["entry_count"] == 0
