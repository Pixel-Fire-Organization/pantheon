"""Tests for tools/pack_archive.py — the .PS2R game-archive packer.

Verifies the format invariants the runtime (engine/src/EngineArchive.cpp) relies
on: sector-aligned payloads, TOC lookup by canonical key, byte-exact payload
round-trip, and canonical-key / FNV-1a parity with the C side.
"""

import importlib.util
import os
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load():
    path = ROOT / "tools" / "pack_archive.py"
    spec = importlib.util.spec_from_file_location("pack_archive", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


pa = _load()


# --- canonical key parity with Engine_Path_Canonical ------------------------

@pytest.mark.parametrize("raw,expected", [
    ("cdrom0:/RASSETS/BOX.PS2A;1", "RASSETS/BOX.PS2A"),
    ("RASSETS/BOX.PS2A", "RASSETS/BOX.PS2A"),
    ("RASSETS\\BOX.PS2A", "RASSETS/BOX.PS2A"),
    ("cdrom0:\\RASSETS\\box.ps2a;1", "RASSETS/BOX.PS2A"),
    ("mass0:/DLC/EXPANSION.PS2R", "DLC/EXPANSION.PS2R"),
    ("/RASSETS/BOX.PS2A", "RASSETS/BOX.PS2A"),
])
def test_canonical_key(raw, expected):
    assert pa.canonical_key(raw) == expected


def test_fnv1a32_known_answer():
    # Textbook FNV-1a 32 (offset 2166136261, prime 16777619). Locks the Python
    # side; the C Internal_Fnv1a32 uses the identical constants and loop.
    assert pa.fnv1a32("") == 2166136261
    assert pa.fnv1a32("a") == 0xE40C292C
    assert pa.fnv1a32("RASSETS/BOX.PS2A") == pa.fnv1a32(pa.canonical_key("RASSETS/BOX.PS2A"))


# --- round trip -------------------------------------------------------------

def test_roundtrip_and_alignment(tmp_path):
    payloads = {
        "RASSETS/BOX.PS2A": b"\x41\x32\x53\x50" + b"box-payload" * 300,   # ~3.6 KB
        "RASSETS/SMALL.PS2A": b"tiny",
        "RASSETS/EMPTY.PS2A": b"",
    }
    entries = list(payloads.items())
    out = tmp_path / "RASSETS.PS2R"
    stats = pa.write_archive(entries, str(out))
    assert stats["entry_count"] == 3

    toc = pa.read_toc(str(out))
    assert toc["magic"] == pa.ARCH_FILE_MAGIC
    assert toc["version"] == pa.ARCH_FILE_VERSION
    assert toc["data_offset"] % pa.ARCH_SECTOR_ALIGN == 0

    by_key = {e["key"]: e for e in toc["entries"]}
    assert set(by_key) == set(payloads)
    for key, data in payloads.items():
        entry = by_key[key]
        # Every payload starts on a DVD sector boundary (locality invariant).
        assert entry["offset"] % pa.ARCH_SECTOR_ALIGN == 0
        assert entry["size"] == len(data)
        assert entry["hash"] == pa.fnv1a32(key)
        assert pa.read_payload(str(out), entry) == data


def test_duplicate_key_rejected(tmp_path):
    out = tmp_path / "dup.PS2R"
    with pytest.raises(ValueError):
        pa.write_archive([("RASSETS/A.PS2A", b"x"), ("rassets/a.ps2a", b"y")], str(out))


def test_dir_packing(tmp_path):
    src = tmp_path / "rassets"
    src.mkdir()
    (src / "BOX.PS2A").write_bytes(b"box")
    (src / "TREE.PS2A").write_bytes(b"tree")
    out = tmp_path / "RASSETS.PS2R"
    pa.main(["--dir", str(src), "--prefix", "RASSETS", "--dst", str(out)])
    toc = pa.read_toc(str(out))
    keys = {e["key"] for e in toc["entries"]}
    assert keys == {"RASSETS/BOX.PS2A", "RASSETS/TREE.PS2A"}
