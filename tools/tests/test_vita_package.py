"""Tests for tools/vita_package.py — the Vita package config reader and emitter.

The validator exists because every mistake it catches is otherwise silent until
the console refuses to install the package, with an error code that names
nothing. These tests are therefore weighted towards the rejection paths: each one
stands for an afternoon someone would otherwise spend.
"""

import importlib.util
import json
import pathlib
import re
import struct
import zlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load():
    path = ROOT / "tools" / "vita_package.py"
    spec = importlib.util.spec_from_file_location("vita_package", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


vp = _load()


def _png(path, width, height, colour=3, depth=8, trns=False):
    """Write a minimal but structurally valid PNG. Only the header is inspected,
    so the image data is a single empty deflate stream."""

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, depth, colour, 0, 0, 0)
    body = vp.PNG_SIGNATURE + chunk(b"IHDR", ihdr)
    if colour == 3:
        body += chunk(b"PLTE", b"\0\0\0")
    if trns:
        body += chunk(b"tRNS", b"\0")
    body += chunk(b"IDAT", zlib.compress(b"\0")) + chunk(b"IEND", b"")
    path.write_bytes(body)
    return path


def _config(tmp_path, **overrides):
    """A minimal valid config with its store-front images written alongside."""
    sce = tmp_path / "sce_sys" / "livearea" / "contents"
    sce.mkdir(parents=True, exist_ok=True)
    _png(tmp_path / "sce_sys" / "icon0.png", 128, 128)
    _png(sce / "bg0.png", 840, 500)
    _png(sce / "startup.png", 280, 158)

    config = {
        "platform": "vita",
        "title": {"id": "PSEN00001", "name": "Test Title", "version": "01.00"},
        "livearea": {
            "icon": "sce_sys/icon0.png",
            "background": "sce_sys/livearea/contents/bg0.png",
            "startup": "sce_sys/livearea/contents/startup.png",
            "style": "a1",
        },
    }
    config.update(overrides)
    path = tmp_path / "package.json"
    path.write_text(json.dumps(config), encoding="utf-8")
    return config, path


def _check(config, tmp_path, variant=None):
    return vp.validate(config, str(tmp_path), variant)


def test_achv_max_entries_matches_the_engine_header():
    """ACHV_MAX_ENTRIES is a FORMAT constant: the trophy container layout and this
    tool both depend on it, so the two copies must not drift."""
    header = (ROOT / "engine" / "include" / "core" / "EngineAchievement.h").read_text(encoding="utf-8")
    m = re.search(r"#define\s+ACHV_MAX_ENTRIES\s+(\d+)", header)
    assert m, "ACHV_MAX_ENTRIES not found in EngineAchievement.h"
    assert int(m.group(1)) == vp.ACHV_MAX_ENTRIES


def test_repo_config_validates_for_both_variants():
    raw = vp.load_config(str(ROOT / "game" / "config" / "platform" / "vita" / "package.json"))
    config_dir = str(ROOT / "game" / "config" / "platform" / "vita")
    for variant in ("vita", "vitatv"):
        resolved = vp.resolve_variant(raw, variant)
        vp.validate(resolved, config_dir, variant)


def test_repo_config_matches_its_schema():
    pytest.importorskip("jsonschema")
    raw = vp.load_config(str(ROOT / "game" / "config" / "platform" / "vita" / "package.json"))
    vp.schema_validate(raw, str(ROOT / "tools" / "schemas" / "package.schema.json"))


@pytest.mark.parametrize("bad", ["PSEN0001", "psen00001", "PSEN000012", "PS2N0001X", ""])
def test_bad_title_id_rejected(tmp_path, bad):
    config, _ = _config(tmp_path)
    config["title"]["id"] = bad
    with pytest.raises(vp.PackageError, match="title.id"):
        _check(config, tmp_path)


@pytest.mark.parametrize("bad", ["1.00", "01.0", "0100", "aa.bb", ""])
def test_bad_version_rejected(tmp_path, bad):
    config, _ = _config(tmp_path)
    config["title"]["version"] = bad
    with pytest.raises(vp.PackageError, match="title.version"):
        _check(config, tmp_path)


def test_empty_title_name_rejected(tmp_path):
    config, _ = _config(tmp_path)
    config["title"]["name"] = ""
    with pytest.raises(vp.PackageError, match="title.name"):
        _check(config, tmp_path)


def test_wrong_icon_size_names_file_and_both_sizes(tmp_path):
    config, _ = _config(tmp_path)
    _png(tmp_path / "sce_sys" / "icon0.png", 127, 128)
    with pytest.raises(vp.PackageError) as exc:
        _check(config, tmp_path)
    message = str(exc.value)
    assert "icon0.png" in message and "127x128" in message and "128x128" in message


def test_non_indexed_image_rejected(tmp_path):
    config, _ = _config(tmp_path)
    _png(tmp_path / "sce_sys" / "icon0.png", 128, 128, colour=2)  # truecolour
    with pytest.raises(vp.PackageError, match="colour type"):
        _check(config, tmp_path)


def test_transparency_rejected_on_icon_but_allowed_on_startup(tmp_path):
    config, _ = _config(tmp_path)
    sce = tmp_path / "sce_sys" / "livearea" / "contents"

    _png(sce / "startup.png", 280, 158, trns=True)
    _check(config, tmp_path)          # the one layer that may be transparent

    _png(tmp_path / "sce_sys" / "icon0.png", 128, 128, trns=True)
    with pytest.raises(vp.PackageError, match="transparency"):
        _check(config, tmp_path)


def test_missing_image_rejected(tmp_path):
    config, _ = _config(tmp_path)
    (tmp_path / "sce_sys" / "icon0.png").unlink()
    with pytest.raises(vp.PackageError, match="missing file"):
        _check(config, tmp_path)


def test_required_layer_cannot_be_omitted(tmp_path):
    config, _ = _config(tmp_path)
    del config["livearea"]["background"]
    with pytest.raises(vp.PackageError, match="livearea.background is required"):
        _check(config, tmp_path)


def test_optional_picture_may_be_absent(tmp_path):
    config, _ = _config(tmp_path)
    _check(config, tmp_path)          # no 'picture' key at all


def _trophies(**over):
    base = {
        "enabled": True,
        "np_communication_id": "NPWR00001_00",
        "list": [{"id": 0, "grade": "bronze", "name": "First"}],
    }
    base.update(over)
    return base


def test_sparse_trophy_ids_rejected(tmp_path):
    config, _ = _config(tmp_path, trophies=_trophies(list=[
        {"id": 0, "grade": "bronze", "name": "A"},
        {"id": 2, "grade": "bronze", "name": "B"},
    ]))
    with pytest.raises(vp.PackageError, match="dense"):
        _check(config, tmp_path)


def test_duplicate_trophy_ids_rejected(tmp_path):
    config, _ = _config(tmp_path, trophies=_trophies(list=[
        {"id": 0, "grade": "bronze", "name": "A"},
        {"id": 0, "grade": "gold", "name": "B"},
    ]))
    with pytest.raises(vp.PackageError, match="duplicate"):
        _check(config, tmp_path)


def test_two_platinums_rejected(tmp_path):
    config, _ = _config(tmp_path, trophies=_trophies(list=[
        {"id": 0, "grade": "platinum", "name": "A"},
        {"id": 1, "grade": "platinum", "name": "B"},
    ]))
    with pytest.raises(vp.PackageError, match="platinum"):
        _check(config, tmp_path)


def test_too_many_trophies_rejected(tmp_path):
    entries = [{"id": i, "grade": "bronze", "name": f"T{i}"} for i in range(vp.ACHV_MAX_ENTRIES + 1)]
    config, _ = _config(tmp_path, trophies=_trophies(list=entries))
    with pytest.raises(vp.PackageError, match="maximum"):
        _check(config, tmp_path)


@pytest.mark.parametrize("bad", ["NPWR0001_00", "npwr00001_00", "NPWR00001_01", "NPWR00001"])
def test_bad_communication_id_rejected(tmp_path, bad):
    config, _ = _config(tmp_path, trophies=_trophies(np_communication_id=bad))
    with pytest.raises(vp.PackageError, match="np_communication_id"):
        _check(config, tmp_path)


def test_trp_and_list_are_mutually_exclusive(tmp_path):
    (tmp_path / "pack.trp").write_bytes(b"x")
    config, _ = _config(tmp_path, trophies=_trophies(trp="pack.trp"))
    with pytest.raises(vp.PackageError, match="mutually exclusive"):
        _check(config, tmp_path)


def test_enabled_without_any_source_rejected(tmp_path):
    config, _ = _config(tmp_path, trophies={"enabled": True, "np_communication_id": "NPWR00001_00"})
    with pytest.raises(vp.PackageError, match="neither"):
        _check(config, tmp_path)


def test_disabled_list_is_still_validated(tmp_path):
    """A disabled definition must not rot: enabling it later should not be the
    moment it first fails."""
    config, _ = _config(tmp_path, trophies=_trophies(enabled=False, list=[
        {"id": 5, "grade": "bronze", "name": "A"},
    ]))
    with pytest.raises(vp.PackageError, match="dense"):
        _check(config, tmp_path)


def test_variant_override_is_deep_and_leaves_base_intact(tmp_path):
    config, _ = _config(tmp_path)
    config["variants"] = {"vitatv": {"title": {"name": "TV Title"}}}

    tv = vp.resolve_variant(config, "vitatv")
    assert tv["title"]["name"] == "TV Title"
    assert tv["title"]["id"] == "PSEN00001"      # siblings survive the merge
    assert "variants" not in tv

    base = vp.resolve_variant(config, "vita")     # no block for this variant
    assert base["title"]["name"] == "Test Title"


def test_variant_list_replaces_rather_than_appends():
    base = {"trophies": {"list": [{"id": 0}, {"id": 1}]}}
    merged = vp.deep_merge(base, {"trophies": {"list": [{"id": 0}]}})
    assert merged["trophies"]["list"] == [{"id": 0}]


def test_emit_ids_generates_dense_enum(tmp_path):
    config, _ = _config(tmp_path, trophies=_trophies(list=[
        {"id": 0, "grade": "bronze", "name": "First Frame"},
        {"id": 1, "grade": "gold", "name": "World Traveller"},
    ]))
    out = tmp_path / "TrophyIds.h"
    vp.emit_ids(config, str(out))
    text = out.read_text()
    assert "First_Frame = 0," in text
    assert "World_Traveller = 1," in text
    assert "Count = 2" in text


def test_emit_cmake_carries_identity_and_file_list(tmp_path):
    config, _ = _config(tmp_path)
    out = tmp_path / "vita.cmake"
    vp.emit_cmake(config, str(tmp_path), str(tmp_path), str(out))
    text = out.read_text()
    assert 'set(VITA_TITLE_ID "PSEN00001")' in text
    assert "-s TITLE_ID=PSEN00001" in text
    assert "sce_sys/livearea/contents/template.xml" in text


def test_extended_memory_declares_the_attribute(tmp_path):
    config, _ = _config(tmp_path, self={"extended_memory": True})
    assert "-d ATTRIBUTE2=12" in vp._sfo_args(config)
    config, _ = _config(tmp_path, self={"extended_memory": False})
    assert "-d ATTRIBUTE2=12" not in vp._sfo_args(config)


def test_emit_template_uses_the_declared_style(tmp_path):
    config, _ = _config(tmp_path)
    config["livearea"]["style"] = "psmobile"
    out = tmp_path / "template.xml"
    vp.emit_template(config, str(out))
    assert 'style="psmobile"' in out.read_text()


def test_trp_index_names_every_payload(tmp_path):
    """The index is what TRPWork repacks from: a header it accepts and an entry
    per payload, with the payloads written beside it. Offsets, sizes and the
    digest are deliberately absent -- TRPWork computes those."""
    _config(tmp_path, trophies=_trophies())
    config = vp.load_config(str(tmp_path / "package.json"))
    vp.emit_trophy_conf(config, str(tmp_path))
    index = vp.emit_trp_index(config, str(tmp_path), str(tmp_path), str(tmp_path))

    blob = pathlib.Path(index).read_bytes()
    magic, version, size, count, info_off = struct.unpack(">IIQII", blob[:0x18])
    assert (magic, version, info_off) == (0xDCA24D00, 2, vp.TRP_HEADER_SIZE)
    assert size == 0
    assert len(blob) == vp.TRP_HEADER_SIZE + count * vp.TRP_ENTRY_SIZE

    names = [blob[vp.TRP_HEADER_SIZE + i * vp.TRP_ENTRY_SIZE:][:0x20].rstrip(b"\0").decode()
             for i in range(count)]
    assert names[:2] == ["TROPCONF.SFM", "TROP.SFM"]
    for name in names:
        assert (tmp_path / "TROPHY" / name).exists()


def test_trp_index_version_is_the_vita_one(tmp_path):
    """2 is PS3/Vita; 3 is PS4, and the Vita reader refuses it."""
    _config(tmp_path, trophies=_trophies())
    config = vp.load_config(str(tmp_path / "package.json"))
    vp.emit_trophy_conf(config, str(tmp_path))
    index = vp.emit_trp_index(config, str(tmp_path), str(tmp_path), str(tmp_path))
    assert struct.unpack(">I", pathlib.Path(index).read_bytes()[4:8])[0] == 2

