"""The PSFN cooked-font payload: a fixed header plus a dense glyph table.

The atlas is NOT in this payload. It is an ordinary cooked texture named as the
font asset's dependency, so the existing texture budget, upload and validation
paths serve it unchanged. See docs/formats/FONT_FORMAT.md.

Pure struct packing on purpose: the cook stage needs Pillow to read source art,
but nothing here does, so the format stays testable without it.
"""

import struct

MAGIC = b"PSFN"
VERSION = 2

HEADER_SIZE = 28
GLYPH_SIZE = 12

FLAG_MONOSPACED = 0x0001

# The interface's own contract limits coverage to printable ASCII, so the table
# is dense and a lookup is one subtraction rather than a search.
FIRST_CODE = 32
LAST_CODE = 126
GLYPH_COUNT = LAST_CODE - FIRST_CODE + 1


class FontError(ValueError):
    """A font that cannot be cooked, with the reason a human needs."""


def _check_cell(label, g, atlas_w, atlas_h, allow_zero_advance):
    u, v, w, h = g["u"], g["v"], g["w"], g["h"]
    if w < 0 or h < 0 or w > 255 or h > 255:
        raise FontError(f"{label} has size {w}x{h}, outside 0..255")
    if u < 0 or v < 0 or u + w > atlas_w or v + h > atlas_h:
        raise FontError(f"{label} at ({u},{v}) size {w}x{h} falls outside the {atlas_w}x{atlas_h} atlas")
    if not (-128 <= g["bearingX"] <= 127) or not (-128 <= g["bearingY"] <= 127):
        raise FontError(f"{label} bearing ({g['bearingX']},{g['bearingY']}) does not fit a signed byte")
    if g["advance"] < 0 or g["advance"] > 255:
        raise FontError(f"{label} advance {g['advance']} is outside 0..255")
    # A zero advance stacks every following glyph on this one. It is almost
    # always an authoring slip, so it is refused rather than drawn. Icon cells
    # are never stacked in a run of their own the way glyphs are, so the first
    # one is not a special case the way FIRST_CODE is for glyphs.
    if g["advance"] == 0 and not allow_zero_advance:
        raise FontError(f"{label} has a zero advance")


def _check_glyph(index, g, atlas_w, atlas_h):
    code = FIRST_CODE + index
    _check_cell(f"glyph {code}", g, atlas_w, atlas_h, allow_zero_advance=(code == FIRST_CODE))


MAX_CELLS = 255


def _pack_cell(g):
    return struct.pack("<HHBBbbB3x", g["u"], g["v"], g["w"], g["h"], g["bearingX"], g["bearingY"], g["advance"])


def write_font(glyphs, atlas_w, atlas_h, line_height, baseline, space_advance, missing_index=0, cells=None):
    """Build a PSFN payload.

    `glyphs` is a list of GLYPH_COUNT dicts with keys u, v, w, h, bearingX,
    bearingY, advance, ordered from FIRST_CODE. `cells` is an optional list of
    the same shape, addressed by an engine-side enumerator rather than a
    codepoint -- icons and controller glyphs -- and stored right after the
    glyph table; a font with none is exactly as valid as one with some, since a
    caller with no cells falls back to text.
    """
    cells = cells or []
    if len(glyphs) != GLYPH_COUNT:
        raise FontError(f"expected {GLYPH_COUNT} glyphs, got {len(glyphs)}")
    if atlas_w <= 0 or atlas_h <= 0:
        raise FontError(f"atlas {atlas_w}x{atlas_h} is empty")
    if atlas_w & (atlas_w - 1) or atlas_h & (atlas_h - 1):
        raise FontError(f"atlas {atlas_w}x{atlas_h} is not power-of-two on both axes")
    if not (0 <= missing_index < GLYPH_COUNT):
        raise FontError(f"missing glyph index {missing_index} is outside 0..{GLYPH_COUNT - 1}")
    if line_height <= 0:
        raise FontError(f"line height {line_height} must be positive")
    if len(cells) > MAX_CELLS:
        raise FontError(f"{len(cells)} cells, at most {MAX_CELLS} fit")

    for i, g in enumerate(glyphs):
        _check_glyph(i, g, atlas_w, atlas_h)
    for i, g in enumerate(cells):
        _check_cell(f"cell {i} ({g.get('name', '?')})", g, atlas_w, atlas_h, allow_zero_advance=True)

    advances = {g["advance"] for g in glyphs}
    flags = FLAG_MONOSPACED if len(advances) == 1 else 0

    cell_count = len(cells)
    cell_offset = HEADER_SIZE + GLYPH_COUNT * GLYPH_SIZE

    blob = MAGIC
    blob += struct.pack(
        "<HHHHHHHHHHHH",
        VERSION,
        flags,
        atlas_w,
        atlas_h,
        line_height,
        baseline,
        space_advance,
        FIRST_CODE,
        GLYPH_COUNT,
        missing_index,
        cell_count,
        cell_offset,
    )
    assert len(blob) == HEADER_SIZE, len(blob)

    for g in glyphs:
        blob += _pack_cell(g)
    assert len(blob) == cell_offset, len(blob)

    for g in cells:
        blob += _pack_cell(g)

    assert len(blob) == cell_offset + cell_count * GLYPH_SIZE, len(blob)
    return blob, ".fnt"


def describe(blob):
    """Summarise a PSFN payload for the inspection and validation tools.

    Returns a dict, or raises FontError. Reads the header and the glyph table;
    it never touches the atlas, which is a separate asset.
    """
    if len(blob) < HEADER_SIZE:
        raise FontError("shorter than a font header")
    if blob[:4] != MAGIC:
        raise FontError("bad PSFN magic")

    (version, flags, atlas_w, atlas_h, line_height, baseline,
     space_advance, first_code, glyph_count, missing_index,
     cell_count, cell_offset) = struct.unpack_from("<HHHHHHHHHHHH", blob, 4)

    if version != VERSION:
        raise FontError(f"font layout version {version}, this build reads {VERSION}")

    glyph_table_end = HEADER_SIZE + glyph_count * GLYPH_SIZE
    expected = glyph_table_end + cell_count * GLYPH_SIZE
    if cell_offset != glyph_table_end:
        raise FontError(f"cell table declared at {cell_offset}, the glyph table ends at {glyph_table_end}")
    if len(blob) != expected:
        raise FontError(f"payload is {len(blob)} bytes, header describes {expected}")
    if missing_index >= glyph_count:
        raise FontError(f"missing glyph index {missing_index} is outside 0..{glyph_count - 1}")
    if atlas_w & (atlas_w - 1) or atlas_h & (atlas_h - 1):
        raise FontError(f"atlas {atlas_w}x{atlas_h} is not power-of-two on both axes")

    glyphs = []
    for i in range(glyph_count):
        u, v, w, h, bx, by, adv = struct.unpack_from("<HHBBbbB", blob, HEADER_SIZE + i * GLYPH_SIZE)
        code = first_code + i
        if u + w > atlas_w or v + h > atlas_h:
            raise FontError(f"glyph {code} falls outside the atlas")
        if adv == 0 and code != first_code:
            raise FontError(f"glyph {code} has a zero advance")
        glyphs.append({"u": u, "v": v, "w": w, "h": h, "bearingX": bx, "bearingY": by, "advance": adv})

    cells = []
    for i in range(cell_count):
        u, v, w, h, bx, by, adv = struct.unpack_from("<HHBBbbB", blob, cell_offset + i * GLYPH_SIZE)
        if u + w > atlas_w or v + h > atlas_h:
            raise FontError(f"cell {i} falls outside the atlas")
        cells.append({"u": u, "v": v, "w": w, "h": h, "bearingX": bx, "bearingY": by, "advance": adv})

    return {
        "version": version,
        "monospaced": bool(flags & FLAG_MONOSPACED),
        "atlas_width": atlas_w,
        "atlas_height": atlas_h,
        "line_height": line_height,
        "baseline": baseline,
        "space_advance": space_advance,
        "first_code": first_code,
        "glyph_count": glyph_count,
        "missing_index": missing_index,
        "cell_count": cell_count,
        "glyphs": glyphs,
        "cells": cells,
    }


def describe_text(blob):
    d = describe(blob)
    kind = "monospaced" if d["monospaced"] else "proportional"
    return "%d glyphs from U+%04X, %s, %d cell(s), line %dpx baseline %dpx, atlas %dx%d" % (
        d["glyph_count"], d["first_code"], kind, d["cell_count"], d["line_height"], d["baseline"],
        d["atlas_width"], d["atlas_height"])
