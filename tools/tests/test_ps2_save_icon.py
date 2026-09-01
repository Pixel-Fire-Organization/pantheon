"""The PS2 save icon: the descriptor and model a memory card browser needs.

Structure only. A browser rejects a save whose descriptor is the wrong size or
whose model is missing, and reports it to the player as corrupted data rather
than as a content error, so every check here is about being *accepted* rather
than about looking a particular way.
"""

import json
import os
import re
import struct
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "ps2"))

import save_icon  # noqa: E402

DECLARATION = os.path.join(ROOT, "game", "config", "title.json")


def test_descriptor_is_exactly_the_size_the_browser_expects():
    blob = save_icon.build_icon_sys("PS2 Engine", 10)
    assert len(blob) == save_icon.ICON_SYS_SIZE == 964
    assert blob[:4] == b"PS2D"


def test_descriptor_names_a_model_for_all_three_slots():
    # Normal, copy and delete. A slot naming nothing shows an empty icon.
    blob = save_icon.build_icon_sys("PS2 Engine", 10)
    names = []
    for offset in (260, 324, 388):
        names.append(blob[offset:offset + save_icon.NAME_MAX].split(b"\x00", 1)[0].decode("ascii"))
    assert names == [save_icon.ICON_FILE] * 3


def test_the_declared_title_reaches_the_descriptor():
    blob = save_icon.build_icon_sys("PS2 Engine", 10)
    title = blob[192:192 + save_icon.TITLE_MAX].split(b"\x00", 1)[0].decode("shift_jis")
    assert title == "PS2 Engine"


def test_an_overlong_title_is_refused_rather_than_truncated():
    with pytest.raises(ValueError, match="at most"):
        save_icon.build_icon_sys("X" * 100, 0)


def test_model_header_declares_what_follows():
    model = save_icon.build_icon()
    file_id, shapes, tex_type, reserved, verts = struct.unpack_from("<IIIII", model, 0)
    assert file_id == 0x010000
    assert shapes == 1
    assert tex_type == save_icon.TEX_TYPE_UNCOMPRESSED
    assert reserved == 0
    # The console draws triangles; a count that is not a multiple of three
    # leaves it reading past the end of the vertex block.
    assert verts % 3 == 0 and verts > 0


def test_model_is_exactly_as_long_as_its_header_describes():
    model = save_icon.build_icon()
    verts = struct.unpack_from("<I", model, 16)[0]
    header = 20
    per_vertex = 8 + 8 + 4 + 4          # position, normal, texcoord, colour
    animation = 20 + 8 + 8              # header, one frame, one key
    assert len(model) == header + verts * per_vertex + animation


def test_header_emits_both_blobs_and_the_texture_constant(tmp_path):
    out = tmp_path / "Ps2SaveIcon.h"
    save_icon.emit_header(DECLARATION, str(out))
    text = out.read_text(encoding="utf-8")

    assert "PS2_SAVE_ICON_SYS[964]" in text
    assert "PS2_SAVE_ICON_MODEL[" in text
    assert "PS2_SAVE_ICON_NAME" in text
    assert "PS2_SAVE_ICON_TEXEL" in text
    dim = re.search(r"#define PS2_SAVE_ICON_TEX_DIM (\d+)", text)
    assert dim and int(dim.group(1)) == save_icon.TEX_SIZE


def test_the_icon_carries_the_title_the_declaration_names(tmp_path):
    with open(DECLARATION, "r", encoding="utf-8-sig") as fh:
        declared = json.load(fh)["name"]

    out = tmp_path / "Ps2SaveIcon.h"
    save_icon.emit_header(DECLARATION, str(out))
    text = out.read_text(encoding="utf-8")

    # Read bytes out of the descriptor array only: constants declared above it
    # are also hex literals and would shift every offset.
    body = text.split("PS2_SAVE_ICON_SYS[964] = {", 1)[1].split("};", 1)[0]
    blob = bytes(int(b, 16) for b in re.findall(r"0x([0-9A-F]{2})", body))
    assert len(blob) == save_icon.ICON_SYS_SIZE
    title = blob[192:192 + save_icon.TITLE_MAX].split(b"\x00", 1)[0].decode("shift_jis")
    assert title == declared
