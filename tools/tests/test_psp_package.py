"""Tests for the PSP package reader.

The shipped declaration must stay packageable, and the failure modes that
matter here are the silent ones: a container slot written out of order, or an
image of the wrong size, produces a container the system menu accepts and then
draws wrongly. Both are checked.
"""

import json
import os
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import psp_package  # noqa: E402

CONFIG = ROOT / "game" / "config" / "platform" / "psp" / "package.json"
SCHEMA = ROOT / "tools" / "schemas" / "package.schema.json"
TITLE = ROOT / "game" / "config" / "title.json"


# --- the shipped declaration ------------------------------------------------

def test_shipped_declaration_loads():
    config = psp_package.load(str(CONFIG), str(SCHEMA), str(TITLE))
    assert config["platform"] == "psp"
    assert config["title"]["id"] == json.loads(TITLE.read_text(encoding="utf-8"))["ids"]["psp"]


def test_identity_is_not_restated_in_the_platform_declaration():
    """Identity comes from the shared title declaration and is folded in when
    this is read, so the name a console shows and the directory a save is filed
    under cannot disagree."""
    raw = json.loads(CONFIG.read_text(encoding="utf-8"))
    assert "title" not in raw
    config = psp_package.load(str(CONFIG), str(SCHEMA), str(TITLE))
    assert config["title"]["name"]
    assert config["title"]["version"]


def test_shipped_declaration_matches_the_schema():
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    raw = json.loads(CONFIG.read_text(encoding="utf-8"))
    assert set(raw) <= set(schema["properties"])
    assert set(raw.get("eboot", {})) <= set(schema["properties"]["eboot"]["properties"])


# --- container slots --------------------------------------------------------

def test_absent_slots_are_placeheld_never_omitted():
    """The container is a fixed sequence. Omitting an absent slot shifts every
    later one, which the system menu shows as the wrong image rather than as an
    error."""
    config = psp_package.load(str(CONFIG), str(SCHEMA), str(TITLE))
    slots = psp_package.pbp_slots(config, str(CONFIG.parent))
    assert len(slots) == len(psp_package.EBOOT_SLOTS)

    declared = config.get("eboot") or {}
    for name, value in zip(psp_package.EBOOT_SLOTS, slots):
        if name in declared:
            assert value.endswith(os.path.basename(declared[name]))
        else:
            assert value == psp_package.SLOT_PLACEHOLDER


def test_slot_order_is_the_hardware_order():
    assert psp_package.EBOOT_SLOTS == ("icon", "icon_anim", "overlay", "picture", "sound")


# --- validation refuses what the console would accept and draw wrongly -------

def test_wrong_icon_size_is_refused(tmp_path):
    import struct
    import zlib

    def png(path, w, h):
        raw = b"".join(b"\x00" + b"\x00\x00\x00\xff" * w for _ in range(h))

        def chunk(tag, data):
            body = tag + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

        path.write_bytes(
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b"")
        )

    (tmp_path / "eboot").mkdir()
    png(tmp_path / "eboot" / "icon0.png", 64, 64)  # not 144x80
    (tmp_path / "package.json").write_text(
        json.dumps({"platform": "psp", "eboot": {"icon": "eboot/icon0.png"}}), encoding="utf-8"
    )

    with pytest.raises(psp_package.PspPackageError, match="64x64, expected 144x80"):
        psp_package.load(str(tmp_path / "package.json"), str(SCHEMA), str(TITLE))


def test_missing_slot_file_is_refused(tmp_path):
    (tmp_path / "package.json").write_text(
        json.dumps({"platform": "psp", "eboot": {"icon": "eboot/gone.png"}}), encoding="utf-8"
    )
    with pytest.raises(psp_package.PspPackageError, match="missing file"):
        psp_package.load(str(tmp_path / "package.json"), str(SCHEMA), str(TITLE))


def test_wrong_platform_is_refused(tmp_path):
    (tmp_path / "package.json").write_text(json.dumps({"platform": "vita"}), encoding="utf-8")
    with pytest.raises(psp_package.PspPackageError, match="expected 'psp'"):
        psp_package.load(str(tmp_path / "package.json"), str(SCHEMA), str(TITLE))


# --- emitted build inputs ---------------------------------------------------

def test_sfo_args_carry_the_declared_version_and_category():
    config = psp_package.load(str(CONFIG), str(SCHEMA), str(TITLE))
    args = psp_package.sfo_args(config)
    joined = " ".join(args)
    assert "APP_VER={}".format(config["title"]["version"]) in joined
    assert "CATEGORY=MG" in joined
    assert "MEMSIZE=" in joined


def test_emit_cmake_defines_every_variable_the_fragment_reads(tmp_path):
    """The fragment lifts these by name; a renamed one fails at configure time
    with an empty string rather than an error."""
    config = psp_package.load(str(CONFIG), str(SCHEMA), str(TITLE))
    out = tmp_path / "package.cmake"
    psp_package.emit_cmake(config, str(CONFIG.parent), str(out))
    text = out.read_text(encoding="utf-8")
    for var in ("PSP_TITLE_ID", "PSP_TITLE_NAME", "PSP_TITLE_VERSION", "PSP_SFO_ARGS", "PSP_PBP_SLOTS", "PSP_PACKAGE_FILES"):
        assert "set({}".format(var) in text, var


def test_umd_data_names_the_title(tmp_path):
    config = psp_package.load(str(CONFIG), str(SCHEMA), str(TITLE))
    out = tmp_path / "UMD_DATA.BIN"
    psp_package.emit_umd_data(config, str(out))
    assert out.read_bytes().startswith(config["title"]["id"].encode("ascii"))


# --- the cook list is the hardware half, and stays separate -----------------

def test_cooklist_caps_match_the_hardware_sampler_limit():
    """512 in each axis is a hardware limit, not a budget: a larger texture
    cannot be sampled at all."""
    cooklist = json.loads((ROOT / "engine" / "config" / "psp" / "cooklist.json").read_text(encoding="utf-8"))
    texture = cooklist["assets"]["TEXTURE"]
    assert texture["max_width"] == 512
    assert texture["max_height"] == 512
    assert texture["format"] == "source"

    header = (ROOT / "engine" / "include" / "platform" / "psp" / "PlatformConstants.h").read_text(encoding="utf-8")
    assert "#define GFX_MAX_TEXTURE_WIDTH 512" in header
    assert "#define GFX_MAX_TEXTURE_HEIGHT 512" in header
