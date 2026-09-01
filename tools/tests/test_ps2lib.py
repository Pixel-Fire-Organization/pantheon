"""Golden tests for the ps2lib refactor.

Locks the extracted mesh/tim2/ps2a code to byte-identical output for the existing
assets, so moving it out of the cooker can never silently change what ships.
"""

import importlib.util
import json
import pathlib
import shutil
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
GOLDEN = pathlib.Path(__file__).resolve().parent / "golden"
FIXTURES = pathlib.Path(__file__).resolve().parent / "fixtures"

pytest.importorskip("PIL")


def _load(name, relpath):
    import sys
    sys.path.insert(0, str(TOOLS))
    spec = importlib.util.spec_from_file_location(name, TOOLS / relpath)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


mesh = _load("ps2lib.mesh", "ps2lib/mesh.py")
tim2 = _load("ps2lib.tim2", "ps2lib/tim2.py")
ps2a = _load("ps2lib.ps2a", "ps2lib/ps2a.py")
cook_assets = _load("cook_assets", "cook_assets.py")


def test_box_ps2a_byte_identical(tmp_path):
    src = ROOT / "assets" / "textures" / "props"
    cook_assets.pack_asset(str(src / "MAINMENU.JSON"), str(src), str(tmp_path))
    produced = (tmp_path / "BOX.PS2A").read_bytes()
    assert produced == (GOLDEN / "BOX.PS2A").read_bytes()


def test_cook_assets_finds_nested_descriptors(tmp_path, monkeypatch):
    src = tmp_path / "src" / "sub"
    src.mkdir(parents=True)
    shutil.copy(ROOT / "assets" / "textures" / "props" / "MAINMENU.JPG", src / "MAINMENU.JPG")
    (src / "MAINMENU.JSON").write_text(json.dumps({"type": "TEXTURE", "source": "MAINMENU.JPG", "deps": []}))
    dst = tmp_path / "dst"

    monkeypatch.setattr(sys, "argv", ["cook_assets.py", "--src", str(tmp_path / "src"), "--dst", str(dst)])
    assert cook_assets.main() == 0
    assert (dst / "BOX.PS2A").is_file()


def test_cube_bkm_byte_identical():
    data, ext = mesh.bake_obj_model(str(FIXTURES / "cube.obj"), has_texture=True)
    assert ext == ".bkm"
    assert data == (GOLDEN / "cube.bkm").read_bytes()


def test_pal8_cutout_reserves_transparent_index0():
    from PIL import Image
    img = Image.new("RGBA", (16, 16), (255, 0, 0, 255))
    # Punch a transparent hole; it must map to CLUT index 0 (alpha 0).
    for y in range(8):
        for x in range(8):
            img.putpixel((x, y), (0, 0, 0, 0))
    blob = tim2.encode_pal8_cutout(img)
    assert blob[:4] == b"TIM2"
    # CLUT is the tail (256 * 4 bytes); entry 0 must be fully transparent.
    clut = blob[-256 * 4:]
    assert clut[3] == 0  # index 0 alpha == 0
