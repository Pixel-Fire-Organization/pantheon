#!/usr/bin/env python3
"""
compile_level.py — TrenchBroom .map -> compiled PS2 level (.ps2l + .PS2R archive).

Pipeline (see docs/formats/LEVEL_FORMAT.md):
  1. Parse the Valve-220 .map (ps2lib.mapparse): entities + brush face polygons.
  2. Convert Quake Z-up map units to engine Y-up world units (scale _map_scale).
  3. Partition world geometry into a fixed square grid of sectors (_sector_size).
  4. Per cell, group faces by material and bake one strip/list mesh each
     (ps2lib.mesh.bake_mesh) into a PSEC sector blob.
  5. Bake each material to a TIM2 .ps2a; bake point-entity models (.obj) to BKM2.
  6. Bake far-field billboard impostors (flat-colour orthographic views) per cell.
  7. Emit the .ps2l core (INFO/MATL/SGRD/ENTS/FARF) + all payloads into one
     locality-ordered archive <NAME>.PS2R. This is a standalone, inspectable
     build artefact (tools/dump_level.py) - tools/pack_master_archive.py later
     folds its entries, byte-identical, into the one master archive that
     actually ships (see docs/subsystems/ARCHIVE.md).

Coordinate convention: Quake (x east, y north, z up) -> engine (x, z, -y), i.e.
the map's horizontal X/Y plane becomes the engine's X/Z ground plane. UVs are
computed from the untransformed Quake vertices (the U/V axes live in map space).

Usage:
  python3 tools/compile_level.py assets/maps/test.map --out build/levels \
      --textures assets/textures --models assets/models --platform ps2 \
      [--report] [--debug-render out.png]

--platform selects the cook list (engine/config/<name>/cooklist.json) that
caps a baked brush material's dimensions ("level_textures") - omit it to
compile unrestricted, e.g. for local inspection.
"""

import argparse
import hashlib
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ps2lib import levelfmt, mapparse, mesh as meshlib, ps2a, tim2
import cook_assets
import pack_archive

DEFAULT_MAP_SCALE = 1.0 / 32.0   # 32 map units = 1 world unit (~1 metre)
DEFAULT_SECTOR_SIZE = 64.0        # world units per sector cell
FARFIELD_FRAME_SIZE = 48          # px per azimuth view in the impostor atlas
FARFIELD_ATLAS_SIZE = 256
LEVEL_SECTOR_MAX_BYTES = 512 * 1024

# Max triangle edge length in world units (worldspawn `_max_edge` overrides).
# The PS2 requires small world triangles: ps2gl's VU1 renderers never truly clip
# — a triangle with ANY vertex outside the ±2048 guard band or behind the near
# plane has its ADC bit set and is dropped WHOLE (see external/ps2gl/vu1/
# clip_cull.i). Giant brush faces (a floor as two half-map triangles) therefore
# vanish piecewise as the camera moves. Subdividing to a few metres per edge
# keeps every triangle comfortably inside the guard band.
DEFAULT_MAX_EDGE = 4.0

# Levels are cooked per platform, same as game/engine assets (see
# docs/PIPELINE.md): a brush material's pixel dimensions are a policy choice
# declared per platform in its cooklist.json ("level_textures"), same idea as
# the "assets.TEXTURE" ceiling but sized for many simultaneously-pinned
# materials instead of one deliberately-loaded resource. This byte ceiling is
# a second, independent safety net regardless of that dimension cap - a baked
# texture must still fit the target platform's own IO read buffer. Kept in
# sync with the engine headers by tools/tests/test_compile_level.py.
LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM = {
    "ps2": 512 * 1024,
    "vita": 4 * 1024 * 1024,
    "win32": 4 * 1024 * 1024,
    "psp": 512 * 1024,
    "nx": 4 * 1024 * 1024,
}
DEFAULT_LEVEL_TEXTURE_MAX_BYTES = 512 * 1024  # conservative fallback with no --platform

# Texture names that never produce render geometry.
_SKIP_TEXTURES = ("skip", "nodraw", "clip", "trigger", "origin", "hint", "areaportal")


def _is_skip_texture(name):
    low = name.lower().rsplit("/", 1)[-1]
    return any(low.startswith(s) for s in _SKIP_TEXTURES)


def q2e(p, scale):
    """Quake (x,y,z up) -> engine (x, y up, z) world units."""
    return (p[0] * scale, p[2] * scale, -p[1] * scale)

def e2q(p, scale):
    """Engine (x, y up, z) -> Quake (x,y,z up) world units."""
    return (p[0] / scale, -p[2] / scale, p[1] / scale)



def q2e_dir(n):
    return (n[0], n[2], -n[1])


def _tessellate_tri(tri, max_edge):
    """Subdivide one triangle until no edge exceeds max_edge (world units).

    tri is [(vert3, uv2)] * 3. Splits the longest edge at its midpoint each
    step; brush-face UVs are a planar (affine) mapping, so midpoint-interpolated
    UVs are exact. Winding is preserved. Returns a list of triangles.
    """
    max_e2 = max_edge * max_edge
    out = []
    stack = [tri]
    while stack:
        t = stack.pop()
        longest = -1
        longest_l2 = 0.0
        for i in range(3):
            a = t[i][0]
            b = t[(i + 1) % 3][0]
            l2 = (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2
            if l2 > longest_l2:
                longest_l2 = l2
                longest = i
        if longest_l2 <= max_e2:
            out.append(t)
            continue
        i = longest
        j = (i + 1) % 3
        k = (j + 1) % 3
        a, b, c = t[i], t[j], t[k]
        mid = (
            tuple((a[0][q] + b[0][q]) * 0.5 for q in range(3)),
            tuple((a[1][q] + b[1][q]) * 0.5 for q in range(2)),
        )
        stack.append([a, mid, c])
        stack.append([mid, b, c])
    return out


class Material:
    def __init__(self, tex_name, key):
        self.tex_name = tex_name
        self.key = key
        self.width = 64
        self.height = 64
        self.color = (160, 160, 160)  # mean colour, for far-field flat shading
        self.payload = None           # baked TIM2 .ps2a bytes
        self.img = None
        self.shared = False


def _resolve_texture(tex_dir, tex_name):
    for ext in (".png", ".tga", ".jpg", ".jpeg", ".bmp"):
        p = os.path.join(tex_dir, tex_name.replace("/", os.sep) + ext)
        if os.path.isfile(p):
            return p
    return None


def _find_shared_descriptor(src_path):
    """A brush material that also ships as a standalone rasset - a .json
    descriptor next to it naming it as a TEXTURE source, same convention
    cook_assets.py reads - can be referenced from the boot archive instead of
    duplicated into every level that paints with it. Returns the descriptor's
    base name (its cooked RASSETS/<name>.PS2A key stem), or None."""
    tex_dir = os.path.dirname(src_path)
    src_base = os.path.basename(src_path).lower()
    try:
        names = sorted(os.listdir(tex_dir))
    except OSError:
        return None
    for name in names:
        if not name.lower().endswith(".json"):
            continue
        try:
            with open(os.path.join(tex_dir, name), "r", encoding="utf-8-sig") as fh:
                meta = json.load(fh)
        except (OSError, ValueError):
            continue
        if meta.get("type", "").upper() != "TEXTURE":
            continue
        if str(meta.get("source", "")).lower() == src_base:
            return os.path.splitext(name)[0]
    return None


def _bake_material(level_name, tex_name, tex_dir, max_width=None, max_height=None,
                   max_bytes=DEFAULT_LEVEL_TEXTURE_MAX_BYTES):
    key = f"{level_name}/{tex_name.upper().replace('/', '_')}.PS2A"
    mat = Material(tex_name, key)
    src = _resolve_texture(tex_dir, tex_name)
    try:
        from PIL import Image
        if src:
            img = Image.open(src).convert("RGBA")
        else:
            img = Image.new("RGBA", (64, 64), (200, 0, 200, 255))  # missing-texture magenta
        # UV baking below always divides by mat.width/mat.height, which must
        # stay the size TrenchBroom saw when the material was aligned in the
        # editor - the actual encoded pixels may end up smaller (see below),
        # but the normalized UV space they're sampled with must not shrink
        # with them, or the material's tiling frequency would drift from what
        # was authored.
        mat.width, mat.height = img.size
        thumb = img.convert("RGB").resize((1, 1), Image.BOX)
        mat.color = thumb.getpixel((0, 0))

        # A material already cooked as a standalone rasset can be referenced
        # from the boot archive instead of baked a second time into this
        # level's own - but only if it already satisfies this platform's
        # level_textures cap. That cap is deliberately stricter than the
        # standalone TEXTURE ceiling (a level pins every material for its
        # whole lifetime), so an oversized shared texture is excluded rather
        # than reconciled: this level still bakes and pins its own capped
        # copy, same as before this existed.
        if src:
            shared_base = _find_shared_descriptor(src)
            if shared_base:
                fits = ((not max_width or mat.width <= max_width) and
                        (not max_height or mat.height <= max_height))
                shared_key = f"RASSETS/{shared_base.upper()}.PS2A"
                if fits:
                    mat.key = shared_key
                    mat.payload = None
                    mat.shared = True
                    print(f"  INFO: '{tex_name}' shared with {shared_key}; "
                          f"not duplicated into this level's archive")
                    return mat
                print(f"  INFO: '{tex_name}' ships as {shared_key} but exceeds this platform's "
                      f"level texture cap ({max_width}x{max_height}); baking a level-local copy")

        baked = img
        # The platform's own "level_textures" policy is enforced first (a
        # deliberate quality/budget choice, not just a fallback) - a level
        # pins every one of its materials for its whole lifetime, so this
        # cap is far stricter than a standalone TEXTURE resource's.
        if (max_width and baked.width > max_width) or (max_height and baked.height > max_height):
            target_w = min(baked.width, max_width) if max_width else baked.width
            target_h = min(baked.height, max_height) if max_height else baked.height
            scale = min(target_w / baked.width, target_h / baked.height)
            new_size = (max(1, round(baked.width * scale)), max(1, round(baked.height * scale)))
            print(f"  INFO: '{tex_name}' downscaled from {baked.width}x{baked.height} to "
                  f"{new_size[0]}x{new_size[1]} for this platform's level texture cap "
                  f"({max_width}x{max_height})")
            baked = baked.resize(new_size, Image.LANCZOS)

        payload, ext = tim2.encode_pal8(baked, 0), ".tm2"
        blob = ps2a.write_ps2a(ps2a.TYPE_MAP["TEXTURE"], payload, [], ext)
        # Independent safety net: even a dimension-capped (or, on a platform
        # with no cap, an arbitrarily large) texture must still fit the
        # target's own IO read buffer.
        orig_bytes = len(blob)
        orig_w, orig_h = baked.width, baked.height
        while len(blob) > max_bytes and (baked.width > 1 or baked.height > 1):
            baked = baked.resize((max(1, baked.width // 2), max(1, baked.height // 2)), Image.LANCZOS)
            payload, ext = tim2.encode_pal8(baked, 0), ".tm2"
            blob = ps2a.write_ps2a(ps2a.TYPE_MAP["TEXTURE"], payload, [], ext)
        if len(blob) != orig_bytes:
            print(f"  WARN: '{tex_name}' baked at {orig_w}x{orig_h} ({orig_bytes} bytes) exceeds the "
                  f"IO read buffer ({max_bytes} bytes); downscaled further to "
                  f"{baked.width}x{baked.height} ({len(blob)} bytes)")
        mat.payload = blob
        mat.img = baked
    except ImportError:
        # No Pillow: keep defaults and a 1x1 placeholder so the archive is valid.
        mat.payload = ps2a.write_ps2a(ps2a.TYPE_MAP["TEXTURE"], b"", [], ".tm2")
    return mat


def _cell_index(x, z, origin_x, origin_z, cell_size, cells_x):
    cx = int((x - origin_x) / cell_size)
    cz = int((z - origin_z) / cell_size)
    return cx, cz


def compile_level(map_path, out_dir, tex_dir, model_dir, platform=None, report=False, debug_png=None):
    level_name = os.path.splitext(os.path.basename(map_path))[0].upper()
    entities = mapparse.parse_map(map_path)

    # Levels are cooked per platform (see docs/PIPELINE.md), so a brush
    # material's dimension cap comes from the same cooklist.json every other
    # asset class reads its policy from - no --platform means unrestricted,
    # which is right for ad hoc/local inspection but not for a real build.
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cooklist = (cook_assets.load_cooklist(cook_assets.cooklist_for_platform(project_root, platform))
                if platform else cook_assets.DEFAULT_COOKLIST)
    level_tex_policy = cooklist.get("level_textures", {})
    tex_max_width = level_tex_policy.get("max_width")
    tex_max_height = level_tex_policy.get("max_height")
    tex_max_bytes = LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM.get(
        cooklist.get("platform", "").lower(), DEFAULT_LEVEL_TEXTURE_MAX_BYTES)

    world = next((e for e in entities if e.classname == "worldspawn"), None)
    if world is None:
        raise ValueError("map has no worldspawn entity")

    scale = float(world.props.get("_map_scale", DEFAULT_MAP_SCALE))
    sector_size = float(world.props.get("_sector_size", DEFAULT_SECTOR_SIZE))
    max_edge = float(world.props.get("_max_edge", DEFAULT_MAX_EDGE))

    # --- collect world faces (worldspawn + any solid entities' brushes) -------
    # A face -> (texture, engine polygon verts, engine normal, quake verts for UV).
    faces = []
    faces_lod1 = []
    lod1_brushes = []
    for ent in entities:
        in_lod1 = True
        if ent.classname == "func_detail":
            inc_prop = str(ent.props.get("include_in_lod1", "0")).lower()
            in_lod1 = (inc_prop in ("1", "true"))

        for brush in ent.brushes:
            if in_lod1:
                lod1_brushes.append(brush)
            for face, poly in mapparse.brush_polygons(brush):
                if _is_skip_texture(face.texture):
                    continue
                everts = [q2e(v, scale) for v in poly]
                faces.append((face, poly, everts))
                if in_lod1:
                    faces_lod1.append((brush, face, poly, everts))

    if not faces:
        raise ValueError("map produced no render geometry")

    # --- world AABB + grid ----------------------------------------------------
    all_e = [v for (_, _, everts) in faces for v in everts]
    min_x = min(v[0] for v in all_e)
    max_x = max(v[0] for v in all_e)
    min_z = min(v[2] for v in all_e)
    max_z = max(v[2] for v in all_e)
    origin_x = math.floor(min_x / sector_size) * sector_size
    origin_z = math.floor(min_z / sector_size) * sector_size
    cells_x = max(1, int(math.ceil((max_x - origin_x) / sector_size)))
    cells_z = max(1, int(math.ceil((max_z - origin_z) / sector_size)))

    # --- materials ------------------------------------------------------------
    materials = {}  # tex_name -> Material
    material_order = []

    def material_index(tex_name):
        if tex_name not in materials:
            mat = _bake_material(level_name, tex_name, tex_dir, tex_max_width, tex_max_height, tex_max_bytes)
            materials[tex_name] = mat
            material_order.append(tex_name)
        return material_order.index(tex_name)

    # --- assign faces to cells, group by material -----------------------------
    # cell_groups[(cx,cz)][mat_idx] = (out_v, out_n, out_t)
    # Tessellate first, then bin each resulting triangle by its own centroid.
    # A face's footprint can span many sector cells (a large floor or skybox
    # wall); binning by the whole face's centroid would dump every one of its
    # tessellated triangles into a single cell no matter how far apart they
    # end up, which can blow LEVEL_SECTOR_MAX_BYTES regardless of _sector_size.
    def _build_cell_groups(face_list, is_lod1=False):
        cgroups = {}
        for item in face_list:
            if is_lod1:
                brush, face, poly, everts = item
            else:
                face, poly, everts = item
            midx = material_index(face.texture)
            mat = materials[face.texture]
            nrm = mapparse._normalize(q2e_dir(face.normal))
            uvs = [face.uv(qv, mat.width, mat.height) for qv in poly]
            for k in range(1, len(poly) - 1):
                fan_tri = [(everts[idx], uvs[idx]) for idx in (0, k, k + 1)]
                for tri in _tessellate_tri(fan_tri, max_edge):
                    centroid = tuple(sum(v[i] for (v, _uv) in tri) / 3 for i in range(3))
                    
                    if is_lod1:
                        cent_q = e2q(centroid, scale)
                        # Push inwards slightly
                        cent_q = (cent_q[0] - face.normal[0]*0.1, cent_q[1] - face.normal[1]*0.1, cent_q[2] - face.normal[2]*0.1)
                        hidden = False
                        for other_b in lod1_brushes:
                            if other_b is brush: continue
                            inside = True
                            for f in other_b.faces:
                                if f.normal[0]*cent_q[0] + f.normal[1]*cent_q[1] + f.normal[2]*cent_q[2] - f.dist > 0.01:
                                    inside = False
                                    break
                            if inside:
                                hidden = True
                                break
                        if hidden:
                            continue

                    cx, cz = _cell_index(centroid[0], centroid[2], origin_x, origin_z, sector_size, cells_x)
                    cx = max(0, min(cells_x - 1, cx))
                    cz = max(0, min(cells_z - 1, cz))
                    groups = cgroups.setdefault((cx, cz), {})
                    ov, on, ot = groups.setdefault(midx, ([], [], []))
                    for (vert, uv) in tri:
                        ov.append(vert)
                        on.append(nrm)
                        ot.append(uv)
        return cgroups

    cell_groups = _build_cell_groups(faces, False)
    cell_groups_lod1 = _build_cell_groups(faces_lod1, True)

    # --- ATLASING -------------------------------------------------------------
    UV_ATLAS_THRESHOLD = 3.0

    try:
        from PIL import Image
    except ImportError:
        pass

    def _pack_cell_groups(cgroups, is_lod1=False):
        new_cgroups = {}
        atlas_cache = {}
        for (cx, cz), groups in cgroups.items():
            atlasable_midxs = []
            plain_midxs = []
            for m in groups.keys():
                mat = materials[material_order[m]]
                if mat.shared or not mat.img:
                    plain_midxs.append(m)
                    continue
                ov, on, ot = groups[m]
                if not ot:
                    plain_midxs.append(m)
                    continue
                
                if is_lod1:
                    atlasable_midxs.append(m)
                else:
                    u_min = min(u for u, v in ot)
                    u_max = max(u for u, v in ot)
                    v_min = min(v for u, v in ot)
                    v_max = max(v for u, v in ot)
                    if u_min >= -2.0 and u_max <= UV_ATLAS_THRESHOLD and v_min >= -2.0 and v_max <= UV_ATLAS_THRESHOLD:
                        atlasable_midxs.append(m)
                    else:
                        plain_midxs.append(m)

            new_groups = []
            for m in plain_midxs:
                new_groups.append((m, groups[m], 0, 0, 0, 0))

            if atlasable_midxs:
                atlasable_midxs.sort()
                mset_hash = hashlib.md5(str(atlasable_midxs).encode() + (b"LOD1" if is_lod1 else b"LOD0")).hexdigest()
                if mset_hash not in atlas_cache:
                    atlas_size = 256
                    atlas_img = Image.new("RGBA", (atlas_size, atlas_size), (255, 0, 255, 255))
                    
                    if is_lod1:
                        n = len(atlasable_midxs)
                        if n <= 1: cell_size = 256
                        elif n <= 4: cell_size = 128
                        elif n <= 16: cell_size = 64
                        elif n <= 64: cell_size = 32
                        else: cell_size = 16
                        grid_size = 256 // cell_size
                    else:
                        grid_size = int(math.ceil(math.sqrt(len(atlasable_midxs))))
                        if grid_size == 0: grid_size = 1
                        cell_size = atlas_size // grid_size

                    remaps = {}
                    for idx, midx in enumerate(atlasable_midxs):
                        row = idx // grid_size
                        col = idx % grid_size
                        # Use Image.LANCZOS to fix the AttributeError
                        img = materials[material_order[midx]].img.resize((cell_size, cell_size), Image.LANCZOS)
                        atlas_img.paste(img, (col * cell_size, row * cell_size))

                        off_u = col * cell_size / float(atlas_size)
                        off_v = row * cell_size / float(atlas_size)
                        scale_u = cell_size / float(atlas_size)
                        scale_v = cell_size / float(atlas_size)
                        remaps[midx] = (off_u, off_v, scale_u, scale_v)

                    prefix = "LOD1" if is_lod1 else "ATLAS"
                    atlas_tex_name = f"__{prefix}_{mset_hash}__"
                    atlas_mat = Material(atlas_tex_name, f"{level_name}/{prefix}_{mset_hash.upper()[:8]}.PS2A")
                    payload, ext = tim2.encode_pal8(atlas_img, 0), ".tm2"
                    atlas_mat.payload = ps2a.write_ps2a(ps2a.TYPE_MAP["TEXTURE"], payload, [], ext)
                    atlas_mat.img = atlas_img
                    materials[atlas_tex_name] = atlas_mat
                    material_order.append(atlas_tex_name)
                    atlas_matidx = len(material_order) - 1

                    atlas_cache[mset_hash] = (atlas_matidx, remaps)

                atlas_matidx, remaps = atlas_cache[mset_hash]

                for m in atlasable_midxs:
                    off_u, off_v, scale_u, scale_v = remaps[m]
                    ov, on, ot = groups[m]
                    atlas_ot = [(u * scale_u + off_u, v * scale_v + off_v) for u, v in ot]

                    min_u = int(off_u * 256)
                    max_u = int(scale_u * 256) - 1
                    min_v = int(off_v * 256)
                    max_v = int(scale_v * 256) - 1

                    new_groups.append((atlas_matidx, (ov, on, atlas_ot), min_u, max_u, min_v, max_v))

            new_cgroups[(cx, cz)] = new_groups
        return new_cgroups

    new_cell_groups = _pack_cell_groups(cell_groups, False)
    new_cell_groups_lod1 = _pack_cell_groups(cell_groups_lod1, True)
    # --- bake sectors (PSEC) --------------------------------------------------
    def _bake_sectors(cgroups, is_lod1=False):
        out_sectors = {}
        out_aabb = {}
        for (cx, cz), groups in cgroups.items():
            meshes = []
            for midx, (ov, on, ot), min_u, max_u, min_v, max_v in groups:
                if len(meshes) >= levelfmt.MAX_MESHES_PER_SECTOR:
                    print(f"  WARN: cell {cx},{cz} exceeds {levelfmt.MAX_MESHES_PER_SECTOR} meshes; extra material dropped")
                    break
                
                # Grid decimation for LOD1 sectors (2.0 engine meters = 64 map units)
                decimate_grid = 2.0 if is_lod1 else None
                baked = meshlib.bake_mesh(ov, on, ot, decimate_grid=decimate_grid)
                if not baked:
                    continue
                baked["material_index"] = midx
                baked["min_u"] = min_u
                baked["max_u"] = max_u
                baked["min_v"] = min_v
                baked["max_v"] = max_v
                meshes.append(baked)
            if not meshes:
                continue
            blob, aabb = levelfmt.pack_sector(meshes)
            if len(blob) > LEVEL_SECTOR_MAX_BYTES:
                print(f"  WARN: sector {cx},{cz} is {len(blob)} bytes, exceeds {LEVEL_SECTOR_MAX_BYTES} bytes")
            out_sectors[(cx, cz)] = blob
            out_aabb[(cx, cz)] = aabb
        return out_sectors, out_aabb

    sectors, cell_aabb = _bake_sectors(new_cell_groups, False)
    sectors_lod1, _ = _bake_sectors(new_cell_groups_lod1, True)

    # --- entities (ENTS) + point-entity models --------------------------------
    entity_records = []
    model_payloads = {}  # key -> ps2a bytes
    for ent in entities:
        if ent.classname in ("", "worldspawn", "func_detail"):
            continue
        origin = (0.0, 0.0, 0.0)
        if "origin" in ent.props:
            try:
                ox, oy, oz = (float(t) for t in ent.props["origin"].split())
                origin = q2e((ox, oy, oz), scale)
            except ValueError:
                pass
        props = []
        for k, v in ent.props.items():
            if k in ("origin",):
                continue
            if k == "model" and v.lower().endswith(".obj"):
                model_key = _bake_entity_model(level_name, v, model_dir, model_payloads)
                v = model_key if model_key else v
            props.append((k, v))
        entity_records.append({"classname": ent.classname, "origin": origin, "props": props})


    # --- visibility (VISI) ----------------------------------------------------
    # For now, simplistic radius-based PVS if visportals aren't implemented
    pvs = []
    for cz in range(cells_z):
        for cx in range(cells_x):
            visible = []
            for tz in range(cells_z):
                for tx in range(cells_x):
                    # For now, hardcode a radius of 12 cells so smaller sector sizes still work
                    if abs(cx - tx) <= 12 and abs(cz - tz) <= 12:
                        # Only add to visibility list if the sector actually has LOD1 geometry,
                        # otherwise we exhaust the 32 engine slots with empty void cells!
                        if (tx, tz) in sectors_lod1:
                            visible.append((tx, tz))
            
            # Sort by distance so closer cells take priority when hitting the engine limit!
            visible.sort(key=lambda c: (c[0] - cx)**2 + (c[1] - cz)**2)
            
            pvs.append(visible)
    visi_chunk = levelfmt.pack_visi(cells_x, cells_z, pvs)

    # --- assemble .ps2l core --------------------------------------------------
    grid_cells = []
    for cz in range(cells_z):
        for cx in range(cells_x):
            blob = sectors.get((cx, cz))
            aabb = cell_aabb.get((cx, cz), ((0, 0, 0), (0, 0, 0)))
            grid_cells.append({
                "sector_bytes": len(blob) if blob else 0,
                "aabb_min": aabb[0], "aabb_max": aabb[1],
                "ent_first": 0, "ent_count": 0,  # v1: all entities spawn at load
            })

    mat_keys = [materials[t].key for t in material_order]
    chunks = [
        (levelfmt.CHUNK_INFO, levelfmt.pack_info(level_name, origin_x, origin_z, sector_size,
                                                 cells_x, cells_z, len(mat_keys), len(entity_records))),
        (levelfmt.CHUNK_MATERIALS, levelfmt.pack_materials(mat_keys)),
        (levelfmt.CHUNK_GRID, levelfmt.pack_grid(grid_cells)),
        (levelfmt.CHUNK_ENTITIES, levelfmt.pack_entities(entity_records)),
        (levelfmt.CHUNK_VISI, visi_chunk),
    ]
    ps2l_blob = levelfmt.build_ps2l(chunks)

    # --- archive assembly (locality order) ------------------------------------
    archive_entries = [(f"{level_name}.PS2L", ps2l_blob)]
    for cz in range(cells_z):  # row-major so a crossing reads contiguous entries
        for cx in range(cells_x):
            blob = sectors.get((cx, cz))
            if blob:
                archive_entries.append((f"{level_name}/S{cx:03d}_{cz:03d}.SEC", blob))
            blob_lod1 = sectors_lod1.get((cx, cz))
            if blob_lod1:
                archive_entries.append((f"{level_name}/L{cx:03d}_{cz:03d}.SEC", blob_lod1))
    for tex_name in material_order:  # includes the far-field atlas material
        mat = materials[tex_name]
        if mat.payload is not None:  # None = shared rasset, referenced not duplicated
            archive_entries.append((mat.key, mat.payload))
    for key, payload in model_payloads.items():
        archive_entries.append((key, payload))

    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{level_name}.PS2R")
    stats = pack_archive.write_archive(archive_entries, out_path)

    if report:
        _print_report(level_name, cells_x, cells_z, sectors, materials, entity_records, stats)
    if debug_png:
        _debug_render(debug_png, cells_x, cells_z, sectors)

    print(f"compile_level: wrote {out_path} ({stats['entry_count']} entries, {stats['total_size']} bytes)")
    return out_path


def _bake_entity_model(level_name, model_ref, model_dir, model_payloads):
    base = os.path.splitext(os.path.basename(model_ref))[0]
    key = f"{level_name}/{base.upper()}.PS2A"
    if key in model_payloads:
        return key
    src = os.path.join(model_dir, os.path.basename(model_ref))
    if not os.path.isfile(src):
        print(f"  WARN: entity model '{model_ref}' not found at {src}; keeping raw reference")
        return None
    try:
        payload, ext = meshlib.bake_obj_model(src, has_texture=False)
        model_payloads[key] = ps2a.write_ps2a(ps2a.TYPE_MAP["MODEL"], payload, [], ext)
        return key
    except Exception as e:  # noqa: BLE001
        print(f"  WARN: failed to bake model '{model_ref}': {e}")
        return None


        return
    inv = 1.0 / area
    for y in range(min_y, max_y + 1):
        for x in range(min_x, max_x + 1):
            w0 = ((b[0] - x) * (c[1] - y) - (c[0] - x) * (b[1] - y)) * inv
            w1 = ((c[0] - x) * (a[1] - y) - (a[0] - x) * (c[1] - y)) * inv
            w2 = 1.0 - w0 - w1
            if w0 < 0 or w1 < 0 or w2 < 0:
                continue
            depth = w0 * a[2] + w1 * b[2] + w2 * c[2]
            zi = y * size + x
            if depth < zbuf[zi]:
                zbuf[zi] = depth
                px[x, y] = (color[0], color[1], color[2], 255)


def _print_report(name, cells_x, cells_z, sectors, materials, entities, stats):
    print(f"--- level {name} ---")
    print(f"  grid: {cells_x} x {cells_z} cells, {len(sectors)} non-empty sectors")
    print(f"  materials: {len(materials)}  entities: {len(entities)}")
    for (cx, cz), blob in sorted(sectors.items()):
        print(f"    sector {cx},{cz}: {len(blob)} bytes")
    print(f"  archive: {stats['entry_count']} entries, {stats['total_size']} bytes")


def _debug_render(path, cells_x, cells_z, sectors):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    scale = 24
    img = Image.new("RGB", (cells_x * scale + 1, cells_z * scale + 1), (30, 30, 30))
    d = ImageDraw.Draw(img)
    for cz in range(cells_z):
        for cx in range(cells_x):
            x0, y0 = cx * scale, cz * scale
            fill = (70, 120, 70) if (cx, cz) in sectors else (50, 50, 50)
            d.rectangle([x0, y0, x0 + scale, y0 + scale], fill=fill, outline=(90, 90, 90))
    img.save(path)
    print(f"  debug render -> {path}")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Compile a .map into a .ps2l level archive")
    ap.add_argument("map", help="input .map path")
    ap.add_argument("--out", required=True, help="output directory for <NAME>.PS2R")
    ap.add_argument("--textures", default="assets/textures", help="texture source root")
    ap.add_argument("--models", default="assets/models", help="model source root")
    ap.add_argument("--platform", help="target platform (selects its cooklist.json level_textures cap)")
    ap.add_argument("--report", action="store_true", help="print a per-sector report")
    ap.add_argument("--debug-render", help="write a top-down sector-occupancy PNG")
    args = ap.parse_args(argv)

    compile_level(args.map, args.out, args.textures, args.models, platform=args.platform,
                  report=args.report, debug_png=args.debug_render)
    return 0


if __name__ == "__main__":
    sys.exit(main())
