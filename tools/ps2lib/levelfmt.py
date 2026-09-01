"""Binary (de)serialization for the compiled level format (.ps2l v2) + PSEC/FARF.

Mirrors engine/include/EngineLevelFormat.h field-for-field. Used by
tools/compile_level.py (writer) and tools/dump_level.py (reader). Keeping the
struct formats in one place stops the C and Python views from drifting.
"""

import struct

LEVEL_FILE_MAGIC = 0x4C325350  # "PS2L"
LEVEL_FILE_VERSION = 2

CHUNK_INFO = 0x4F464E49       # "INFO"
CHUNK_MATERIALS = 0x4C54414D  # "MATL"
CHUNK_GRID = 0x44524753       # "SGRD"
CHUNK_ENTITIES = 0x53544E45   # "ENTS"
CHUNK_FARFIELD = 0x46524146   # "FARF"
CHUNK_BSP = 0x54505342        # "BSPT" (reserved)

CHUNK_NAMES = {
    CHUNK_INFO: "INFO", CHUNK_MATERIALS: "MATL", CHUNK_GRID: "SGRD",
    CHUNK_ENTITIES: "ENTS", CHUNK_FARFIELD: "FARF", CHUNK_BSP: "BSPT",
}

SECTOR_MAGIC = 0x43455350  # "PSEC"
SECTOR_VERSION = 1

# Mirrors LEVEL_CHUNK_ALIGN / LEVEL_MAX_MATERIALS / LEVEL_MAX_MESHES_PER_SECTOR
# / LEVEL_FARFIELD_MAX_ATLASES in engine/include/level/EngineLevelFormat.h. The
# runtime refuses a core or a sector that breaks any of them, so the cook must
# not produce one.
CHUNK_ALIGN = 16
MAX_MATERIALS = 64
MAX_MESHES_PER_SECTOR = 32
FARFIELD_MAX_ATLASES = 4

# Struct formats (little-endian). Sizes asserted against EngineLevelFormat.h.
_HDR = "<IIII"                    # LevelFileHeaderV2 (16)
_CHUNK = "<IIII"                  # LevelChunkEntry (16)
_INFO = "<64sfffHHHHII"           # LevelInfoChunk (92)
_MATERIAL = "<64s"               # LevelMaterialEntry (64)
_GRIDCELL = "<IffffffHH"          # LevelGridCell (32)
_ENTREC = "<IfffHH"               # LevelEntityRecord (20)
_ENTPROP = "<II"                 # LevelEntityProp (8)
_SECHDR = "<IIIIffffffII"         # SectorHeader (48)
_MESHENTRY = "<IIIIIIffffII"      # BakedMeshEntry (48)
_FARFHDR = "<IIIIIIII"            # FarfieldHeader (32)
_FARFCLUSTER = "<fffffHH"         # FarfieldCluster (24)
_FARFFRAME = "<ffff"             # FarfieldFrame (16)

assert struct.calcsize(_INFO) == 92
assert struct.calcsize(_GRIDCELL) == 32
assert struct.calcsize(_SECHDR) == 48
assert struct.calcsize(_MESHENTRY) == 48
assert struct.calcsize(_FARFHDR) == 32
assert struct.calcsize(_FARFCLUSTER) == 24


def _align16(n):
    return (n + CHUNK_ALIGN - 1) & ~(CHUNK_ALIGN - 1)


def _cstr(s, size):
    b = s.encode("utf-8")[:size - 1]
    return b + b"\x00" * (size - len(b))


# --- writers ----------------------------------------------------------------

def pack_info(name, origin_x, origin_z, cell_size, cells_x, cells_z, material_count, entity_count):
    return struct.pack(_INFO, _cstr(name, 64), origin_x, origin_z, cell_size,
                       cells_x, cells_z, material_count, entity_count, 0, 0)


def pack_materials(keys):
    if len(keys) > MAX_MATERIALS:
        raise ValueError(f"level declares {len(keys)} materials, the engine table holds {MAX_MATERIALS}")
    return b"".join(struct.pack(_MATERIAL, _cstr(k, 64)) for k in keys)


def pack_grid(cells):
    """cells: list of dicts {sector_bytes, aabb_min(3), aabb_max(3), ent_first, ent_count}."""
    out = bytearray()
    for c in cells:
        mn = c["aabb_min"]
        mx = c["aabb_max"]
        out += struct.pack(_GRIDCELL, c["sector_bytes"], mn[0], mn[1], mn[2],
                           mx[0], mx[1], mx[2], c["ent_first"], c["ent_count"])
    return bytes(out)


def pack_entities(records):
    """records: list of {classname, origin(3), props:[(key,value)]}. Builds the
    ENTS chunk: count/stringsOffset/stringsSize header, records, props, strings."""
    strings = bytearray()
    string_pos = {}

    def intern(s):
        if s not in string_pos:
            string_pos[s] = len(strings)
            strings.extend(s.encode("utf-8") + b"\x00")
        return string_pos[s]

    rec_bytes = bytearray()
    prop_bytes = bytearray()
    prop_index = 0
    packed_recs = []
    for r in records:
        cn_off = intern(r["classname"])
        first = prop_index
        for (k, v) in r["props"]:
            prop_bytes += struct.pack(_ENTPROP, intern(k), intern(v))
            prop_index += 1
        packed_recs.append((cn_off, r["origin"], first, prop_index - first))

    count = len(records)
    header_size = 12
    recs_size = count * struct.calcsize(_ENTREC)
    props_size = len(prop_bytes)
    strings_offset = header_size + recs_size + props_size
    for (cn_off, origin, first, pcount) in packed_recs:
        rec_bytes += struct.pack(_ENTREC, cn_off, origin[0], origin[1], origin[2], first, pcount)

    return (struct.pack("<III", count, strings_offset, len(strings))
            + bytes(rec_bytes) + bytes(prop_bytes) + bytes(strings))


def build_ps2l(chunks):
    """chunks: list of (chunk_type, payload_bytes) in emit order. Returns the
    full .ps2l blob (header + chunk table + 16-aligned payloads)."""
    header_size = 16
    table_size = len(chunks) * 16
    cursor = _align16(header_size + table_size)

    entries = []
    placed = []
    for ctype, payload in chunks:
        cursor = _align16(cursor)
        entries.append((ctype, cursor, len(payload)))
        placed.append((cursor, payload))
        cursor += len(payload)

    total = _align16(cursor)
    buf = bytearray(total)
    struct.pack_into(_HDR, buf, 0, LEVEL_FILE_MAGIC, LEVEL_FILE_VERSION, len(chunks), total)
    pos = header_size
    for ctype, off, size in entries:
        struct.pack_into(_CHUNK, buf, pos, ctype, off, size, 0)
        pos += 16
    for off, payload in placed:
        buf[off:off + len(payload)] = payload
    return bytes(buf)


def pack_sector(meshes):
    """meshes: list of {material_index, topology, vert_count, vbytes, nbytes,
    tbytes, center(3), radius}. Returns the PSEC blob. Sector AABB is derived
    from the union of mesh bounding spheres."""
    if len(meshes) > MAX_MESHES_PER_SECTOR:
        raise ValueError(f"sector holds {len(meshes)} meshes, the resident ring holds {MAX_MESHES_PER_SECTOR}")
    header_size = struct.calcsize(_SECHDR)
    entry_size = struct.calcsize(_MESHENTRY)
    geom_start = _align16(header_size + entry_size * len(meshes))

    geom = bytearray()
    entries = []
    for m in meshes:
        base = geom_start + len(geom)
        verts_off = base
        geom += m["vbytes"]
        geom += b"\x00" * ((-len(geom)) % 16)
        norms_off = geom_start + len(geom) if m["nbytes"] else 0
        geom += m["nbytes"]
        geom += b"\x00" * ((-len(geom)) % 16)
        uvs_off = geom_start + len(geom) if m["tbytes"] else 0
        geom += m["tbytes"]
        geom += b"\x00" * ((-len(geom)) % 16)
        entries.append((m, verts_off, norms_off, uvs_off))

    # Sector AABB from mesh bounding spheres.
    mn = [1e30, 1e30, 1e30]
    mx = [-1e30, -1e30, -1e30]
    for m in meshes:
        c = m["center"]
        r = m["radius"]
        for i in range(3):
            mn[i] = min(mn[i], c[i] - r)
            mx[i] = max(mx[i], c[i] + r)
    if not meshes:
        mn = [0.0, 0.0, 0.0]
        mx = [0.0, 0.0, 0.0]

    total = geom_start + len(geom)
    buf = bytearray(total)
    struct.pack_into(_SECHDR, buf, 0, SECTOR_MAGIC, SECTOR_VERSION, len(meshes), 0,
                     mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], 0, 0)
    pos = header_size
    for (m, verts_off, norms_off, uvs_off) in entries:
        c = m["center"]
        struct.pack_into(_MESHENTRY, buf, pos,
                         m["vert_count"], m["material_index"], verts_off, norms_off, uvs_off,
                         m["topology"], c[0], c[1], c[2], m["radius"], 0, 0)
        pos += entry_size
    buf[geom_start:geom_start + len(geom)] = geom
    return bytes(buf), (tuple(mn), tuple(mx))


def pack_farfield(atlas_materials, azimuth_count, clusters, frames):
    """clusters: list of {center(3), half_w, half_h, atlas_index, first_frame}.
    frames: flat list of (u0, v0, u1, v1)."""
    if len(atlas_materials) > FARFIELD_MAX_ATLASES:
        raise ValueError(f"far field names {len(atlas_materials)} atlases, the header holds {FARFIELD_MAX_ATLASES}")
    for c in clusters:
        if c["first_frame"] + azimuth_count > len(frames):
            raise ValueError(f"far-field cluster at frame {c['first_frame']} needs {azimuth_count} frames, {len(frames)} exist")
    atlas = list(atlas_materials) + [0] * (FARFIELD_MAX_ATLASES - len(atlas_materials))
    out = bytearray()
    out += struct.pack(_FARFHDR, len(clusters), len(atlas_materials),
                       atlas[0], atlas[1], atlas[2], atlas[3], azimuth_count, 0)
    for c in clusters:
        ct = c["center"]
        out += struct.pack(_FARFCLUSTER, ct[0], ct[1], ct[2], c["half_w"], c["half_h"],
                           c["atlas_index"], c["first_frame"])
    for f in frames:
        out += struct.pack(_FARFFRAME, *f)
    return bytes(out)


# --- readers (for dump_level.py / tests) ------------------------------------

def parse_ps2l(blob):
    magic, version, chunk_count, total = struct.unpack_from(_HDR, blob, 0)
    if magic != LEVEL_FILE_MAGIC:
        raise ValueError(f"bad .ps2l magic 0x{magic:08X}")
    chunks = []
    pos = 16
    for _ in range(chunk_count):
        ctype, off, size, _res = struct.unpack_from(_CHUNK, blob, pos)
        pos += 16
        chunks.append({"type": ctype, "name": CHUNK_NAMES.get(ctype, "????"),
                       "offset": off, "size": size})
    return {"magic": magic, "version": version, "chunk_count": chunk_count,
            "total_size": total, "chunks": chunks}


def parse_info(blob, chunk):
    name, ox, oz, cs, cx, cz, mc, ec, _r0, _r1 = struct.unpack_from(_INFO, blob, chunk["offset"])
    return {"name": name.split(b"\x00")[0].decode("utf-8", "ignore"),
            "origin_x": ox, "origin_z": oz, "cell_size": cs,
            "cells_x": cx, "cells_z": cz, "material_count": mc, "entity_count": ec}


def parse_materials(blob, chunk):
    keys = []
    pos = chunk["offset"]
    end = chunk["offset"] + chunk["size"]
    while pos < end:
        (raw,) = struct.unpack_from(_MATERIAL, blob, pos)
        keys.append(raw.split(b"\x00")[0].decode("utf-8", "ignore"))
        pos += 64
    return keys


def parse_grid(blob, chunk, cells_x, cells_z):
    cells = []
    pos = chunk["offset"]
    for _ in range(cells_x * cells_z):
        sb, mnx, mny, mnz, mxx, mxy, mxz, ef, ec = struct.unpack_from(_GRIDCELL, blob, pos)
        pos += 32
        cells.append({"sector_bytes": sb, "aabb_min": (mnx, mny, mnz),
                      "aabb_max": (mxx, mxy, mxz), "ent_first": ef, "ent_count": ec})
    return cells


def parse_sector(blob):
    magic, version, mesh_count, bvh, mnx, mny, mnz, mxx, mxy, mxz, _r0, _r1 = struct.unpack_from(_SECHDR, blob, 0)
    if magic != SECTOR_MAGIC:
        raise ValueError(f"bad PSEC magic 0x{magic:08X}")
    meshes = []
    pos = struct.calcsize(_SECHDR)
    for _ in range(mesh_count):
        vc, mi, vo, no, uo, topo, cxx, cyy, czz, rad, _a, _b = struct.unpack_from(_MESHENTRY, blob, pos)
        pos += 48
        meshes.append({"vert_count": vc, "material_index": mi, "verts_offset": vo,
                       "norms_offset": no, "uvs_offset": uo, "topology": topo,
                       "center": (cxx, cyy, czz), "radius": rad})
    return {"magic": magic, "version": version, "mesh_count": mesh_count,
            "aabb_min": (mnx, mny, mnz), "aabb_max": (mxx, mxy, mxz), "meshes": meshes}


def parse_farfield(blob, chunk):
    """Header, clusters and frames of a FARF chunk. The frame count is whatever
    the chunk has room for after the clusters, exactly as the runtime derives it."""
    base = chunk["offset"]
    cluster_count, atlas_count, a0, a1, a2, a3, azimuth_count, ground = struct.unpack_from(_FARFHDR, blob, base)
    header_size = struct.calcsize(_FARFHDR)
    cluster_size = struct.calcsize(_FARFCLUSTER)
    frame_size = struct.calcsize(_FARFFRAME)
    clusters = []
    pos = base + header_size
    for _ in range(cluster_count):
        cx, cy, cz, hw, hh, ai, ff = struct.unpack_from(_FARFCLUSTER, blob, pos)
        pos += cluster_size
        clusters.append({"center": (cx, cy, cz), "half_w": hw, "half_h": hh,
                         "atlas_index": ai, "first_frame": ff})
    frame_bytes = chunk["size"] - header_size - cluster_size * cluster_count
    frames = []
    for _ in range(frame_bytes // frame_size):
        frames.append(struct.unpack_from(_FARFFRAME, blob, pos))
        pos += frame_size
    return {"cluster_count": cluster_count, "atlas_count": atlas_count,
            "atlas_materials": (a0, a1, a2, a3)[:atlas_count],
            "azimuth_count": azimuth_count, "ground_offset": ground,
            "clusters": clusters, "frames": frames, "frame_bytes": frame_bytes}
