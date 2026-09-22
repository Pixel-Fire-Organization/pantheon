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
CHUNK_VISI = 0x49534956       # "VISI"

CHUNK_NAMES = {
    CHUNK_INFO: "INFO", CHUNK_MATERIALS: "MATL", CHUNK_GRID: "SGRD",
    CHUNK_ENTITIES: "ENTS", CHUNK_FARFIELD: "FARF", CHUNK_BSP: "BSPT", CHUNK_VISI: "VISI",
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
_MESHENTRY = "<IIIIIIffffHHHH"      # BakedMeshEntry (48)
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
                         m["topology"], c[0], c[1], c[2], m["radius"],
                         m.get("min_u", 0), m.get("max_u", 0), m.get("min_v", 0), m.get("max_v", 0))
        pos += entry_size
    buf[geom_start:geom_start + len(geom)] = geom
    return bytes(buf), (tuple(mn), tuple(mx))



def pack_visi(cells_x, cells_z, pvs):
    """Packs visibility graph. pvs is a list of lists of (tx, tz)."""
    # format: uint32 total_cells
    # for each cell: uint32 num_visible, [uint16 tx, uint16 tz] * num_visible
    out = bytearray(struct.pack('<I', cells_x * cells_z))
    for visible in pvs:
        out.extend(struct.pack('<I', len(visible)))
        for (tx, tz) in visible:
            out.extend(struct.pack('<HH', tx, tz))
    
    # padding
    pad = (16 - (len(out) % 16)) % 16
    out.extend(b'\x00' * pad)
    return bytes(out)
