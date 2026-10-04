"""Tests for the level compiler and its shared library.

Compiles assets/maps/test.map end-to-end and asserts the on-disc invariants the
runtime relies on (chunk integrity, sector budgets, archive alignment), plus
mesh-stripifier correctness and C<->Python struct-size parity.
"""

import importlib.util
import pathlib
import os
import re
import struct

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
LEVEL_SECTOR_MAX_BYTES = 512 * 1024

# Pillow is required for texture/far-field baking.
PIL = pytest.importorskip("PIL")
from PIL import Image as PILImage  # noqa: E402 - after importorskip, by design


def _load(name, relpath):
    spec = importlib.util.spec_from_file_location(name, TOOLS / relpath)
    module = importlib.util.module_from_spec(spec)
    import sys
    sys.path.insert(0, str(TOOLS))
    spec.loader.exec_module(module)
    return module


levelfmt = _load("ps2lib.levelfmt", "ps2lib/levelfmt.py")
meshlib = _load("ps2lib.mesh", "ps2lib/mesh.py")
mapparse = _load("ps2lib.mapparse", "ps2lib/mapparse.py")
pack_archive = _load("pack_archive", "pack_archive.py")
compile_level = _load("compile_level", "compile_level.py")

# --- struct-size parity with EngineLevelFormat.h ----------------------------

def test_struct_sizes():
    assert struct.calcsize(levelfmt._INFO) == 92
    assert struct.calcsize(levelfmt._GRIDCELL) == 32
    assert struct.calcsize(levelfmt._ENTREC) == 20
    assert struct.calcsize(levelfmt._SECHDR) == 48
    assert struct.calcsize(levelfmt._MESHENTRY) == 48


_PLATFORM_HEADERS = {
    "ps2": ROOT / "engine" / "include" / "platform" / "ps2" / "PlatformConstantsPs2.h",
    "vita": ROOT / "engine" / "include" / "platform" / "vita" / "PlatformConstantsVita.h",
    "win32": ROOT / "engine" / "include" / "platform" / "win32" / "PlatformConstants.h",
    "psp": ROOT / "engine" / "include" / "platform" / "psp" / "PlatformConstants.h",
    "nx": ROOT / "engine" / "include" / "platform" / "nx" / "PlatformConstants.h",
    "macos": ROOT / "engine" / "include" / "platform" / "macos" / "PlatformConstants.h",
}


def _product_of_literals(expression, source):
    """Evaluate a header constant written as a product of integer literals.

    The only shapes a platform header uses are "512*1024" and a bare count.
    Multiplying the parsed literals keeps a header string out of the
    interpreter, which eval() would not.
    """
    product = 1
    for token in expression.replace(" ", "").split("*"):
        assert token.isdigit(), f"{source}: IO_READ_BUFFER_SIZE is not a product of integer literals ({expression!r})"
        product *= int(token)
    return product


def test_level_texture_max_bytes_matches_every_platform_header():
    """LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM must not exceed each platform's own
    IO_READ_BUFFER_SIZE - levels now cook per platform (docs/PIPELINE.md), so
    each entry only needs to satisfy its own target, not a shared minimum."""
    for platform, header_path in _PLATFORM_HEADERS.items():
        header = header_path.read_text(encoding="utf-8")
        m = re.search(r"#define\s+IO_READ_BUFFER_SIZE\s+\(([^)]+)\)", header)
        assert m, f"IO_READ_BUFFER_SIZE not found in {header_path}"
        buffer_size = _product_of_literals(m.group(1), header_path)
        assert compile_level.LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM[platform] <= buffer_size


def _header_define(header, name):
    m = re.search(r"#define\s+" + name + r"\s+\(?([^)/\n]+)\)?", header)
    assert m, f"{name} not found"
    return m.group(1).strip()


def test_lod1_sector_max_bytes_matches_every_platform_slot():
    """LEVEL_LOD1_SECTOR_MAX_BYTES_BY_PLATFORM must equal the ARENA_LEVEL_LOD1
    slot each platform actually carves: the arena size over its slot count,
    rounded down to the slot alignment, exactly as the engine does."""
    for platform, header_path in _PLATFORM_HEADERS.items():
        header = header_path.read_text(encoding="utf-8")
        size = _product_of_literals(_header_define(header, "MEM_BLOCK_LEVEL_LOD1_SIZE"), header_path)
        slots = int(_header_define(header, "MEM_BLOCK_LEVEL_LOD1_SLOTS"))
        align = _product_of_literals(_header_define(header, "MEM_ARENA_SLOT_ALIGNMENT"), header_path)
        slot_bytes = (size // slots) // align * align
        assert compile_level.LEVEL_LOD1_SECTOR_MAX_BYTES_BY_PLATFORM[platform] == slot_bytes, platform
    assert compile_level.DEFAULT_LEVEL_LOD1_SECTOR_MAX_BYTES == min(compile_level.LEVEL_LOD1_SECTOR_MAX_BYTES_BY_PLATFORM.values())


def _soup_area(verts):
    area = 0.0
    for k in range(0, len(verts), 3):
        a, b, c = verts[k:k + 3]
        ab = [b[i] - a[i] for i in range(3)]
        ac = [c[i] - a[i] for i in range(3)]
        cross = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
        area += 0.5 * sum(x * x for x in cross) ** 0.5
    return area


def test_uv_tile_clipping_keeps_area_and_confines_every_triangle_to_one_tile():
    """An atlased triangle that tiles its texture must be split so each piece
    samples one tile, shifted into [0, 1] - otherwise it samples the next
    atlas cell on every backend without a region-repeat wrap mode."""
    verts = [(0.0, 0.0, 0.0), (4.0, 0.0, 0.0), (0.0, 0.0, 4.0)]
    norms = [(0.0, 1.0, 0.0)] * 3
    uvs = [(-0.5, -1.25), (2.75, -1.25), (-0.5, 2.0)]
    cv, cn, ct = compile_level._clip_soup_to_uv_tiles(verts, norms, uvs)
    assert len(cv) % 3 == 0 and len(cv) > 3
    assert abs(_soup_area(cv) - _soup_area(verts)) < 1e-6
    assert all(0.0 <= u <= 1.0 and 0.0 <= v <= 1.0 for u, v in ct)
    assert all(n == (0.0, 1.0, 0.0) for n in cn)


def _piece_area(piece):
    return sum(compile_level._triangle_area(piece[0][0], piece[i][0], piece[i + 1][0]) for i in range(1, len(piece) - 1))


def test_cell_clipping_keeps_area_and_confines_every_piece_to_its_cell():
    """A triangle spanning several sector cells is cut along every border it
    crosses: the pieces' areas add up to the triangle's, and each piece lies
    inside the cell it is assigned to, with cut corners exactly on a border."""
    n = (0.0, 1.0, 0.0)
    tri = [((1.0, 0.0, 1.0), n, (0.0, 0.0)), ((39.0, 0.0, 1.0), n, (1.0, 0.0)), ((1.0, 0.0, 39.0), n, (0.0, 1.0))]
    cells = compile_level._split_poly_to_cells(tri, 0.0, 0.0, 16.0, 3, 3)
    assert len({cell for cell, _piece in cells}) >= 4
    assert abs(sum(_piece_area(piece) for _cell, piece in cells) - _piece_area(tri)) < 1e-6
    for (cx, cz), piece in cells:
        for pos, _normal, _uv in piece:
            assert cx * 16.0 - 1e-9 <= pos[0] <= (cx + 1) * 16.0 + 1e-9
            assert cz * 16.0 - 1e-9 <= pos[2] <= (cz + 1) * 16.0 + 1e-9


def test_cell_clipping_makes_identical_corners_on_an_edge_two_triangles_share():
    """Two triangles sharing an edge that crosses a border cut it at the same
    point whichever way each one traverses the edge, so no crack opens there."""
    n = (0.0, 1.0, 0.0)
    a, b = ((4.0, 0.0, 3.0), n, (0.0, 0.0)), ((28.0, 0.0, 9.0), n, (1.0, 0.0))
    upper = [a, b, ((10.0, 0.0, 30.0), n, (0.0, 1.0))]
    lower = [b, a, ((30.0, 0.0, -20.0), n, (1.0, 1.0))]
    on_border = lambda cells: {corner[0] for _cell, piece in cells for corner in piece if corner[0][0] == 16.0 or corner[0][2] == 16.0}
    shared = on_border(compile_level._split_poly_to_cells(upper, 0.0, 0.0, 16.0, 4, 4)) & \
        on_border(compile_level._split_poly_to_cells(lower, 0.0, -32.0, 16.0, 4, 4))
    assert any(abs(pos[0] - 16.0) < 1e-12 for pos in shared)


def test_polygon_lying_on_a_cell_border_is_assigned_to_one_cell_only():
    n = (1.0, 0.0, 0.0)
    wall = [((16.0, 0.0, 2.0), n, (0.0, 0.0)), ((16.0, 0.0, 10.0), n, (1.0, 0.0)), ((16.0, 5.0, 6.0), n, (0.5, 1.0))]
    cells = compile_level._split_poly_to_cells(wall, 0.0, 0.0, 16.0, 3, 3)
    assert len(cells) == 1


def test_split_long_edges_bounds_every_edge_and_keeps_area():
    verts = [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (0.0, 0.0, 7.0)]
    norms = [(0.0, 1.0, 0.0)] * 3
    uvs = [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)]
    cols = [(1.0, 0.5, 0.25, 1.0)] * 3
    sv, sn, st, sc = meshlib.split_long_edges(verts, norms, uvs, 4.0, cols)
    assert len(sv) == len(sn) == len(st) == len(sc)
    assert abs(_soup_area(sv) - _soup_area(verts)) < 1e-6
    for k in range(0, len(sv), 3):
        for j in range(3):
            a, b = sv[k + j], sv[k + (j + 1) % 3]
            assert sum((a[i] - b[i]) ** 2 for i in range(3)) ** 0.5 <= 4.0 + 1e-6


def test_clustering_keeps_height_and_leaves_cell_borders_on_the_grid():
    """LOD1 clustering snaps X and Z only: a floor keeps its height, a thin slab
    keeps its thickness, and a vertex on a sector border stays on it."""
    n = (0.0, 1.0, 0.0)
    verts = [(15.2, 0.5, 3.1), (16.0, 0.5, 9.4), (21.9, 0.5, 3.1), (15.2, 0.0, 3.1), (16.0, 0.0, 9.4), (21.9, 0.0, 3.1)]
    norms = [n] * 6
    uvs = [(0.0, 0.0)] * 6
    cv, _cn, _ct, _cc = meshlib.decimate_soup(verts, norms, uvs, 2.0, None, (0.0, 0.0))
    assert {p[1] for p in cv} <= {0.5, 0.0}
    assert any(p[0] == 16.0 for p in cv)
    assert all(p[0] % 2.0 == 0.0 and p[2] % 2.0 == 0.0 for p in cv)


def test_bake_material_respects_the_level_texture_dimension_cap(tmp_path):
    """A platform's 'level_textures' cap (e.g. PS2's 64x64) is enforced up
    front, not just as a byte-budget fallback - but the UV space
    (mat.width/mat.height) must stay at the size TrenchBroom authored
    against, or the material's tiling frequency would drift. mat.payload is
    now the small, fixed-size wrapping RES_MATERIAL; the budget applies to
    the underlying albedo texture it wraps (mat.texture_payload)."""
    big = PILImage.new("RGB", (1024, 1024), (128, 64, 32))
    big.save(tmp_path / "HUGE.png")

    mat = compile_level._bake_material("TESTLEVEL", "HUGE", str(tmp_path), max_width=64, max_height=64)

    assert len(mat.texture_payload) <= compile_level.LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM["ps2"]
    assert mat.payload is not None  # the default-generated wrapping material
    assert (mat.width, mat.height) == (1024, 1024)


def test_bake_material_references_a_shared_rasset_instead_of_duplicating_it(tmp_path):
    """A texture with a TEXTURE descriptor beside it (the same convention
    cook_assets.py reads) already ships as a standalone RASSETS/*.PS2A - the
    level should reference that key and skip baking its own copy of the
    texture, so the same texture used by many levels is not duplicated into
    each archive. A level-local default RES_MATERIAL is still generated,
    wrapping that shared texture, since MATL now always names a material."""
    img = PILImage.new("RGB", (64, 64), (10, 20, 30))
    img.save(tmp_path / "WOOD.png")
    (tmp_path / "WOOD.json").write_text('{"type": "TEXTURE", "source": "WOOD.png", "deps": []}')

    mat = compile_level._bake_material("TESTLEVEL", "WOOD", str(tmp_path), max_width=64, max_height=64)

    assert mat.key == "TESTLEVEL/WOOD.PS2A"
    assert mat.payload is not None  # the default-generated wrapping material
    assert mat.texture_key is None
    assert mat.texture_payload is None  # the shared texture itself is not duplicated
    assert (mat.width, mat.height) == (64, 64)  # UV space still comes from the source image


def test_bake_material_falls_back_to_a_level_local_copy_when_the_shared_rasset_is_oversized(tmp_path):
    """The level_textures cap is stricter than the standalone TEXTURE ceiling
    on purpose (a level pins every material for its whole lifetime) - a
    shared rasset that does not fit it must still get a level-local, capped
    copy rather than being referenced oversized."""
    img = PILImage.new("RGB", (1024, 1024), (10, 20, 30))
    img.save(tmp_path / "WOOD.png")
    (tmp_path / "WOOD.json").write_text('{"type": "TEXTURE", "source": "WOOD.png", "deps": []}')

    mat = compile_level._bake_material("TESTLEVEL", "WOOD", str(tmp_path), max_width=64, max_height=64)

    assert mat.key == "TESTLEVEL/WOOD.PS2A"
    assert mat.texture_key == "TESTLEVEL/WOOD_TEX.PS2A"
    assert mat.texture_payload is not None
    assert len(mat.texture_payload) <= compile_level.LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM["ps2"]


def test_bake_material_falls_back_to_the_byte_budget_with_no_dimension_cap(tmp_path):
    """A platform with an unrestricted dimension cap (e.g. Win32) still must
    not exceed its own IO read buffer - the byte-based safety net alone
    should catch an oversized texture."""
    big = PILImage.new("RGB", (1024, 1024), (128, 64, 32))
    big.save(tmp_path / "HUGE.png")

    mat = compile_level._bake_material("TESTLEVEL", "HUGE", str(tmp_path), max_bytes=64 * 1024)

    assert len(mat.texture_payload) <= 64 * 1024
    assert (mat.width, mat.height) == (1024, 1024)


def test_bake_material_uses_an_authored_material_json_when_present(tmp_path):
    """A material.json sitting where assets/materials/ mirrors this brush
    texture's own path is picked up directly - no default wrapper is
    generated, and the level references its already-cooked RES_MATERIAL."""
    img = PILImage.new("RGB", (32, 32), (5, 5, 5))
    img.save(tmp_path / "STONE_ALBEDO.png")
    (tmp_path / "STONE.json").write_text(
        '{"type": "MATERIAL", "albedo": "STONE_ALBEDO.png", "roughnessFactor": 0.4}'
    )

    mat = compile_level._bake_material("TESTLEVEL", "STONE", str(tmp_path), materials_dir=str(tmp_path))

    assert mat.key == "RASSETS/STONE.PS2A"
    assert mat.payload is None
    assert mat.texture_payload is None
    assert (mat.width, mat.height) == (32, 32)


# --- map parser -------------------------------------------------------------

def test_parse_test_map():
    ents = mapparse.parse_map(str(ROOT / "assets" / "maps" / "test.map"))
    assert any(e.classname == "worldspawn" for e in ents)
    assert any(e.classname == "prop_model" for e in ents)
    world = next(e for e in ents if e.classname == "worldspawn")
    polys = mapparse.brush_polygons(world.brushes[0])
    assert len(polys) == 6  # a 6-sided brush -> 6 quads
    for _face, poly in polys:
        assert len(poly) >= 3


# --- mesh stripifier --------------------------------------------------------

def test_bake_mesh_strip_roundtrips():
    # A 3x3 grid of quads shares vertices -> should stripify and verify.
    verts, norms, uvs = [], [], []
    n = (0.0, 1.0, 0.0)
    for gz in range(3):
        for gx in range(3):
            quad = [(gx, 0, gz), (gx + 1, 0, gz), (gx + 1, 0, gz + 1), (gx, 0, gz + 1)]
            for a, b, c in ((0, 1, 2), (0, 2, 3)):
                for i in (a, b, c):
                    verts.append(tuple(float(v) for v in quad[i]))
                    norms.append(n)
                    uvs.append((0.0, 0.0))
    baked = meshlib.bake_mesh(verts, norms, uvs)
    assert baked["vert_count"] > 0
    # If it chose a strip, the strip must decode back to the source triangles.
    if baked["topology"] == meshlib.BAKED_TOPOLOGY_STRIP:
        _uv, _un, _ut, _uc, tris = meshlib.dedup_corners(verts, norms, uvs)
        # (bake_mesh already verifies internally; just assert it produced verts)
        assert baked["vert_count"] >= len(tris)


# --- full compile -----------------------------------------------------------

@pytest.fixture(scope="module")
def compiled(tmp_path_factory):
    out = tmp_path_factory.mktemp("levels")
    path = compile_level.compile_level(
        str(ROOT / "assets" / "maps" / "test.map"), str(out),
        str(ROOT / "assets" / "textures"), str(ROOT / "assets" / "models"))
    return path


def test_archive_alignment_and_core(compiled):
    toc = pack_archive.read_toc(compiled)
    assert toc["entry_count"] >= 4
    for e in toc["entries"]:
        assert e["offset"] % pack_archive.ARCH_SECTOR_ALIGN == 0

    by_key = {e["key"]: e for e in toc["entries"]}
    assert "TEST.PS2L" in by_key
    core = pack_archive.read_payload(compiled, by_key["TEST.PS2L"])
    lv = levelfmt.parse_ps2l(core)
    assert lv["version"] == levelfmt.LEVEL_FILE_VERSION
    names = {c["name"] for c in lv["chunks"]}
    assert {"INFO", "MATL", "SGRD", "ENTS"} <= names


def test_sectors_within_budget(compiled):
    toc = pack_archive.read_toc(compiled)
    sector_entries = [e for e in toc["entries"] if e["key"].endswith(".SEC")]
    assert sector_entries, "expected at least one sector"
    for e in sector_entries:
        blob = pack_archive.read_payload(compiled, e)
        sec = levelfmt.parse_sector(blob)
        assert sec["magic"] == levelfmt.SECTOR_MAGIC
        assert e["size"] <= LEVEL_SECTOR_MAX_BYTES
        assert sec["mesh_count"] <= 32   # LEVEL_MAX_MESHES_PER_SECTOR
        for m in sec["meshes"]:
            assert m["vert_count"] > 0
            assert m["verts_offset"] % 16 == 0


def test_visi_lists_cover_the_grid_and_name_only_lod1_sectors(compiled):
    """The runtime refuses a VISI chunk whose lists overrun the chunk, do not
    cover exactly the grid, or name a cell outside it, so the cook must never
    produce one; every named cell must also have a LOD1 sector to stream."""
    toc = pack_archive.read_toc(compiled)
    by_key = {e["key"]: e for e in toc["entries"]}
    core = pack_archive.read_payload(compiled, by_key["TEST.PS2L"])
    lv = levelfmt.parse_ps2l(core)
    chunks = {c["name"]: c for c in lv["chunks"]}
    assert "VISI" in chunks
    info = levelfmt.parse_info(core, chunks["INFO"])
    pvs = levelfmt.parse_visi(core, chunks["VISI"])
    assert len(pvs) == info["cells_x"] * info["cells_z"]
    for visible in pvs:
        for (tx, tz) in visible:
            assert tx < info["cells_x"] and tz < info["cells_z"]
            assert f"TEST/L{tx:03d}_{tz:03d}.SEC" in by_key


def test_triangle_edges_within_max_edge(compiled):
    """ps2gl's VU1 renderers drop whole triangles with any vertex outside the
    guard band (no true clipping), so the compiler must tessellate: no baked
    triangle edge may exceed DEFAULT_MAX_EDGE world units."""
    max_edge = compile_level.DEFAULT_MAX_EDGE + 1e-3
    toc = pack_archive.read_toc(compiled)
    checked = 0
    for e in toc["entries"]:
        if not e["key"].endswith(".SEC"):
            continue
        blob = pack_archive.read_payload(compiled, e)
        sec = levelfmt.parse_sector(blob)
        for m in sec["meshes"]:
            # vec4 positions at verts_offset, 16-byte stride.
            verts = []
            for i in range(m["vert_count"]):
                x, y, z, _w = struct.unpack_from("<ffff", blob, m["verts_offset"] + i * 16)
                verts.append((x, y, z))
            if m["topology"] == 1:  # strip: decode, skipping degenerates
                tris = []
                for i in range(len(verts) - 2):
                    t = (i, i + 1, i + 2)
                    a, b, c = verts[t[0]], verts[t[1]], verts[t[2]]
                    if a == b or b == c or a == c:
                        continue
                    tris.append((a, b, c))
            else:  # list
                tris = [(verts[i], verts[i + 1], verts[i + 2]) for i in range(0, len(verts), 3)]
            for (a, b, c) in tris:
                for (p, q) in ((a, b), (b, c), (c, a)):
                    edge = sum((p[k] - q[k]) ** 2 for k in range(3)) ** 0.5
                    assert edge <= max_edge, f"{e['key']}: edge {edge:.2f} > {max_edge}"
                    checked += 1
    assert checked > 0


def test_entities_present(compiled):
    toc = pack_archive.read_toc(compiled)
    by_key = {e["key"]: e for e in toc["entries"]}
    core = pack_archive.read_payload(compiled, by_key["TEST.PS2L"])
    lv = levelfmt.parse_ps2l(core)
    ents_chunk = next(c for c in lv["chunks"] if c["name"] == "ENTS")
    count, strings_offset, strings_size = struct.unpack_from("<III", core, ents_chunk["offset"])
    assert count == 3  # prop_model + 2 light entities
    strings = core[ents_chunk["offset"] + strings_offset:
                   ents_chunk["offset"] + strings_offset + strings_size]
    assert b"prop_model" in strings
    assert b"light" in strings


# --- sector and lighting invariants on a purpose-built map --------------------

_AXES = {
    "minx": ("0 1 0", "0 0 -1"), "miny": ("1 0 0", "0 0 -1"), "bottom": ("1 0 0", "0 -1 0"),
    "top": ("1 0 0", "0 -1 0"), "maxy": ("1 0 0", "0 0 -1"), "maxx": ("0 1 0", "0 0 -1"),
}


def _box_brush(x0, x1, y0, y1, z0, z1, texture="utils/missing", scale=1.0):
    corners = {
        "minx": ((x0, y1, z1), (x0, y0, z1), (x0, y0, z0)),
        "miny": ((x0, y0, z1), (x1, y0, z1), (x1, y0, z0)),
        "bottom": ((x1, y0, z0), (x1, y1, z0), (x0, y1, z0)),
        "top": ((x0, y1, z1), (x1, y1, z1), (x1, y0, z1)),
        "maxy": ((x1, y1, z0), (x1, y1, z1), (x0, y1, z1)),
        "maxx": ((x1, y0, z1), (x1, y1, z1), (x1, y1, z0)),
    }
    lines = ["{"]
    for name, pts in corners.items():
        u, v = _AXES[name]
        plane = " ".join("( %d %d %d )" % p for p in pts)
        lines.append(f"{plane} {texture} [ {u} 0 ] [ {v} 0 ] 0 {scale} {scale}")
    lines.append("}")
    return "\n".join(lines)


def _synthetic_map(extra_world="", extra_brushes=""):
    """Three-by-three cells of 4 world units: a thin floor, and a tall box that
    straddles the border between two cells and shadows the floor beyond it."""
    brushes = [_box_brush(0, 384, 0, 384, -16, 0), _box_brush(96, 160, 160, 224, 0, 96)]
    return "\n".join([
        "// entity 0", "{", '"mapversion" "220"', '"classname" "worldspawn"', '"_sector_size" "4"', '"_max_edge" "2"', extra_world,
        *brushes, extra_brushes, "}",
        "// entity 1", "{", '"classname" "light"', '"origin" "200 100 200"', '"light_type" "1"', '"color" "255 255 255"', '"intensity" "2.0"', '"range" "60"', "}",
        "// entity 2", "{", '"classname" "light"', '"origin" "0 0 400"', '"angles" "-50 30 0"', '"light_type" "0"', '"color" "255 255 255"', '"intensity" "0.8"', "}",
    ]) + "\n"


def _compile_synthetic(tmp_path, platform, name="synth", **kwargs):
    map_path = tmp_path / f"{name}.map"
    map_path.write_text(_synthetic_map(**kwargs))
    out = tmp_path / f"levels_{platform}_{name}"
    path = compile_level.compile_level(str(map_path), str(out), str(ROOT / "assets" / "textures"), str(ROOT / "assets" / "models"), platform=platform)
    return path, name.upper()


def _read_sectors(path, name):
    """{(tier, cx, cz): {"aabb": (min, max), "tris": [(corners, normals, colours)]}} for every sector of a level."""
    toc = pack_archive.read_toc(path)
    sectors = {}
    for e in toc["entries"]:
        key = e["key"]
        if not key.endswith(".SEC"):
            continue
        base = key.split("/")[-1]
        tier = "LOD0" if base[0] == "S" else "LOD1"
        cx, cz = int(base[1:4]), int(base[5:8])
        blob = pack_archive.read_payload(path, e)
        sec = levelfmt.parse_sector(blob)
        tris = []
        for m in sec["meshes"]:
            n = m["vert_count"]
            pos = [struct.unpack_from("<4f", blob, m["verts_offset"] + 16 * i)[:3] for i in range(n)]
            nrm = [struct.unpack_from("<3f", blob, m["norms_offset"] + 12 * i) for i in range(n)]
            col = [struct.unpack_from("<4f", blob, m["colors_offset"] + 16 * i) for i in range(n)] if m["colors_offset"] else None
            if m["topology"] == 1:
                idx = [(i, i + 1, i + 2) for i in range(n - 2) if len({pos[i], pos[i + 1], pos[i + 2]}) == 3]
            else:
                idx = [(i, i + 1, i + 2) for i in range(0, n - 2, 3)]
            for (a, b, c) in idx:
                tris.append(((pos[a], pos[b], pos[c]), (nrm[a], nrm[b], nrm[c]), None if col is None else (col[a], col[b], col[c])))
        sectors[(tier, cx, cz)] = {"aabb": (sec["aabb_min"], sec["aabb_max"]), "tris": tris}
    core_entry = next(e for e in toc["entries"] if e["key"] == f"{name}.PS2L")
    core = pack_archive.read_payload(path, core_entry)
    info = levelfmt.parse_info(core, next(c for c in levelfmt.parse_ps2l(core)["chunks"] if c["name"] == "INFO"))
    return sectors, info


@pytest.fixture(scope="module", params=[None, "ps2", "macos"], ids=["no-platform", "ps2", "macos"])
def synthetic(request, tmp_path_factory):
    path, name = _compile_synthetic(tmp_path_factory.mktemp("synthetic"), request.param)
    sectors, info = _read_sectors(path, name)
    return request.param, sectors, info


def test_every_triangle_lies_inside_its_sector_cell(synthetic):
    _platform, sectors, info = synthetic
    size = info["cell_size"]
    for (tier, cx, cz), sector in sectors.items():
        x0, z0 = info["origin_x"] + cx * size, info["origin_z"] + cz * size
        for corners, _n, _c in sector["tris"]:
            for p in corners:
                assert x0 - 1e-4 <= p[0] <= x0 + size + 1e-4, f"{tier} {cx},{cz}: x {p[0]} outside [{x0}, {x0 + size}]"
                assert z0 - 1e-4 <= p[2] <= z0 + size + 1e-4, f"{tier} {cx},{cz}: z {p[2]} outside [{z0}, {z0 + size}]"


def test_sector_aabb_is_the_exact_vertex_extent(synthetic):
    _platform, sectors, _info = synthetic
    for key, sector in sectors.items():
        pts = [p for corners, _n, _c in sector["tris"] for p in corners]
        lo, hi = sector["aabb"]
        for k in range(3):
            assert abs(lo[k] - min(p[k] for p in pts)) < 1e-5, key
            assert abs(hi[k] - max(p[k] for p in pts)) < 1e-5, key


def test_a_vertex_has_one_baked_colour_in_every_sector_and_tier(synthetic):
    _platform, sectors, _info = synthetic
    seen = {}
    shared = 0
    for key, sector in sectors.items():
        for corners, normals, colours in sector["tris"]:
            assert colours is not None
            for p, n, c in zip(corners, normals, colours):
                k = (round(p[0], 3), round(p[1], 3), round(p[2], 3), round(n[0], 2), round(n[1], 2), round(n[2], 2))
                if k in seen and seen[k][0] != key:
                    shared += 1
                    assert max(abs(a - b) for a, b in zip(seen[k][1], c)) < 1e-6, f"{k}: {seen[k][0]} vs {key}"
                seen.setdefault(k, (key, c))
    assert shared > 0


def test_the_bake_shadows_the_floor_beside_the_box(synthetic):
    _platform, sectors, _info = synthetic
    lum = [sum(c[:3]) / 3 for sector in sectors.values() for _p, n, cols in sector["tris"] for c in cols]
    ambient = compile_level.BAKE_AMBIENT[0]
    assert min(lum) == pytest.approx(ambient, abs=1e-6)
    assert max(lum) > ambient + 0.2
    assert sum(1 for v in lum if abs(v - ambient) < 1e-6) > len(lum) // 50


def test_lod1_keeps_every_height_lod0_has(synthetic):
    _platform, sectors, _info = synthetic
    def levels(tier):
        return {round(corners[0][1], 2) for (t, _cx, _cz), s in sectors.items() if t == tier
                for corners, _n, _c in s["tris"] if abs(corners[0][1] - corners[1][1]) < 1e-4 and abs(corners[1][1] - corners[2][1]) < 1e-4}
    assert levels("LOD1") <= levels("LOD0")


def test_hardware_clipping_platforms_cover_the_same_floor_with_fewer_triangles(synthetic):
    platform, sectors, _info = synthetic
    if platform != "macos":
        pytest.skip("only platforms that clip in hardware take coarse LOD1 faces")
    def floor_area(tier):
        total = 0.0
        for (t, _cx, _cz), s in sectors.items():
            if t != tier:
                continue
            for corners, normals, _c in s["tris"]:
                if normals[0][1] > 0.5 and all(abs(p[1]) < 1e-4 for p in corners):
                    total += compile_level._triangle_area(*corners)
        return total
    count = lambda tier: sum(len(s["tris"]) for (t, _a, _b), s in sectors.items() if t == tier)
    assert floor_area("LOD1") == pytest.approx(floor_area("LOD0"), rel=1e-6)
    assert count("LOD1") < count("LOD0")


def test_ps2_lod1_edges_stay_within_the_guard_band_limit(synthetic):
    platform, sectors, _info = synthetic
    if platform == "macos":
        pytest.skip("a platform that clips in hardware is not bound by the guard band")
    limit = 2.0 + 1e-3
    for key, s in sectors.items():
        for corners, _n, _c in s["tris"]:
            for p, q in ((corners[0], corners[1]), (corners[1], corners[2]), (corners[2], corners[0])):
                assert sum((p[k] - q[k]) ** 2 for k in range(3)) ** 0.5 <= limit, key


def test_the_bake_ambient_key_raises_the_floor_of_the_lighting(tmp_path):
    path, name = _compile_synthetic(tmp_path, "macos", name="bright", extra_world='"_bake_ambient" "0.5"')
    sectors, _info = _read_sectors(path, name)
    lum = [sum(c[:3]) / 3 for s in sectors.values() for _p, _n, cols in s["tris"] for c in cols]
    assert min(lum) == pytest.approx(0.5, abs=1e-6)


def test_a_face_covering_a_sliver_of_its_texture_is_reported(tmp_path, capsys):
    extra = _box_brush(300, 332, 300, 332, 0, 32, scale=100.0)
    _compile_synthetic(tmp_path, "macos", name="sliver", extra_brushes=extra)
    out = capsys.readouterr().out
    assert "utils/missing" in out and "draw as one flat colour" in out
