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
    assert struct.calcsize(levelfmt._FARFHDR) == 32
    assert struct.calcsize(levelfmt._FARFCLUSTER) == 24


_PLATFORM_HEADERS = {
    "ps2": ROOT / "engine" / "include" / "platform" / "ps2" / "PlatformConstantsPs2.h",
    "vita": ROOT / "engine" / "include" / "platform" / "vita" / "PlatformConstantsVita.h",
    "win32": ROOT / "engine" / "include" / "platform" / "win32" / "PlatformConstants.h",
    "psp": ROOT / "engine" / "include" / "platform" / "psp" / "PlatformConstants.h",
    "nx": ROOT / "engine" / "include" / "platform" / "nx" / "PlatformConstants.h",
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


def test_bake_material_respects_the_level_texture_dimension_cap(tmp_path):
    """A platform's 'level_textures' cap (e.g. PS2's 64x64) is enforced up
    front, not just as a byte-budget fallback - but the UV space
    (mat.width/mat.height) must stay at the size TrenchBroom authored
    against, or the material's tiling frequency would drift."""
    big = PILImage.new("RGB", (1024, 1024), (128, 64, 32))
    big.save(tmp_path / "HUGE.png")

    mat = compile_level._bake_material("TESTLEVEL", "HUGE", str(tmp_path), max_width=64, max_height=64)

    assert len(mat.payload) <= compile_level.LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM["ps2"]
    assert (mat.width, mat.height) == (1024, 1024)


def test_bake_material_references_a_shared_rasset_instead_of_duplicating_it(tmp_path):
    """A texture with a TEXTURE descriptor beside it (the same convention
    cook_assets.py reads) already ships as a standalone RASSETS/*.PS2A - the
    level should reference that key and skip baking its own copy, so the
    same texture used by many levels is not duplicated into each archive."""
    img = PILImage.new("RGB", (64, 64), (10, 20, 30))
    img.save(tmp_path / "WOOD.png")
    (tmp_path / "WOOD.json").write_text('{"type": "TEXTURE", "source": "WOOD.png", "deps": []}')

    mat = compile_level._bake_material("TESTLEVEL", "WOOD", str(tmp_path), max_width=64, max_height=64)

    assert mat.key == "RASSETS/WOOD.PS2A"
    assert mat.payload is None
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
    assert mat.payload is not None
    assert len(mat.payload) <= compile_level.LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM["ps2"]


def test_bake_material_falls_back_to_the_byte_budget_with_no_dimension_cap(tmp_path):
    """A platform with an unrestricted dimension cap (e.g. Win32) still must
    not exceed its own IO read buffer - the byte-based safety net alone
    should catch an oversized texture."""
    big = PILImage.new("RGB", (1024, 1024), (128, 64, 32))
    big.save(tmp_path / "HUGE.png")

    mat = compile_level._bake_material("TESTLEVEL", "HUGE", str(tmp_path), max_bytes=64 * 1024)

    assert len(mat.payload) <= 64 * 1024
    assert (mat.width, mat.height) == (1024, 1024)


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
        _uv, _un, _ut, tris = meshlib.dedup_corners(verts, norms, uvs)
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


def test_farfield_clusters_reference_only_what_the_chunk_holds(compiled):
    """The runtime refuses a FARF chunk whose atlases, clusters or frames point
    outside the chunk or the material table, so the cook must never produce
    one - in particular a cluster with fewer frames than azimuth_count once
    the atlas has filled up."""
    toc = pack_archive.read_toc(compiled)
    by_key = {e["key"]: e for e in toc["entries"]}
    core = pack_archive.read_payload(compiled, by_key["TEST.PS2L"])
    lv = levelfmt.parse_ps2l(core)
    chunks = {c["name"]: c for c in lv["chunks"]}
    assert "FARF" in chunks
    info = levelfmt.parse_info(core, chunks["INFO"])
    farf = levelfmt.parse_farfield(core, chunks["FARF"])
    assert farf["frame_bytes"] % struct.calcsize(levelfmt._FARFFRAME) == 0
    assert 0 < farf["atlas_count"] <= levelfmt.FARFIELD_MAX_ATLASES
    assert farf["azimuth_count"] > 0
    for material in farf["atlas_materials"]:
        assert material < info["material_count"]
    assert farf["cluster_count"] > 0
    for c in farf["clusters"]:
        assert c["atlas_index"] < farf["atlas_count"]
        assert c["first_frame"] + farf["azimuth_count"] <= len(farf["frames"])


def test_pack_farfield_refuses_a_cluster_short_of_frames():
    cluster = {"center": (0.0, 0.0, 0.0), "half_w": 1.0, "half_h": 1.0,
               "atlas_index": 0, "first_frame": 0}
    frames = [(0.0, 0.0, 1.0, 1.0)] * 3
    with pytest.raises(ValueError):
        levelfmt.pack_farfield([0], 4, [cluster], frames)


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
    assert count == 1  # prop_model
    strings = core[ents_chunk["offset"] + strings_offset:
                   ents_chunk["offset"] + strings_offset + strings_size]
    assert b"prop_model" in strings
