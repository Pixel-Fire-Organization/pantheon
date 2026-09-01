"""Mesh baking: separated, UNINDEXED vertex arrays as ps2gl / GIFTAG want them.

The core is bake_mesh(), which turns per-corner (pos, normal, uv) triangle arrays
into a degenerate-stitched GL_TRIANGLE_STRIP (when that is a win) or an unindexed
list, emitting vec4 positions (16-byte stride) + vec3 normals + vec2 uvs and an
object-space bounding sphere. It is shared by the OBJ->BKM2 baker (cook_assets)
and the level compiler's per-sector meshes (compile_level), so both go through
exactly one stripifier.
"""

import struct

BAKED_MODEL_MAGIC = 0x324D4B42  # "BKM2"
BAKED_MODEL_VERSION = 2

BAKED_TOPOLOGY_LIST = 0
BAKED_TOPOLOGY_STRIP = 1

# Above this verts/triangle ratio the strip is worse than just paying for the
# extra draw-call overhead, so bake an (unstripped) triangle list instead.
STRIP_MAX_VERTS_PER_TRI = 2.5


def align16(n):
    return (n + 15) & ~15


def bounding_sphere(positions):
    """AABB-midpoint center + exact max-distance radius over (x,y,z) tuples."""
    if not positions:
        return (0.0, 0.0, 0.0), 0.0
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    cx = (min(xs) + max(xs)) * 0.5
    cy = (min(ys) + max(ys)) * 0.5
    cz = (min(zs) + max(zs)) * 0.5
    r2 = 0.0
    for (x, y, z) in positions:
        d2 = (x - cx) ** 2 + (y - cy) ** 2 + (z - cz) ** 2
        if d2 > r2:
            r2 = d2
    return (cx, cy, cz), r2 ** 0.5


def aabb(positions):
    """Axis-aligned bounds (min, max) over (x,y,z) tuples."""
    if not positions:
        return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    zs = [p[2] for p in positions]
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def dedup_corners(out_v, out_n, out_t):
    """Collapse identical (pos, normal, uv) triangle corners to unique vertices.
    Returns (unique_v, unique_n, unique_t, triangles) with triangles as index
    triples into the unique arrays."""
    unique = {}
    uv, un, ut = [], [], []
    indices = []
    for i in range(len(out_v)):
        key = (out_v[i], out_n[i], out_t[i])
        idx = unique.get(key)
        if idx is None:
            idx = len(uv)
            unique[key] = idx
            uv.append(out_v[i])
            un.append(out_n[i])
            ut.append(out_t[i])
        indices.append(idx)
    triangles = [(indices[k], indices[k + 1], indices[k + 2]) for k in range(0, len(indices), 3)]
    return uv, un, ut, triangles


def stripify(triangles):
    """Greedy triangle-strip builder. Returns a list of strips (lists of vertex
    indices) whose GL_TRIANGLE_STRIP decode reproduces `triangles` with winding."""
    from collections import defaultdict
    edge_tris = defaultdict(list)
    for idx, (a, b, c) in enumerate(triangles):
        edge_tris[frozenset((a, b))].append(idx)
        edge_tris[frozenset((b, c))].append(idx)
        edge_tris[frozenset((c, a))].append(idx)

    used = [False] * len(triangles)

    def third(tri, p, q):
        for v in triangles[tri]:
            if v != p and v != q:
                return v
        return None

    def cyclic_match(tri, target):
        a, b, c = triangles[tri]
        return target in ((a, b, c), (b, c, a), (c, a, b))

    def neighbor(p, q, exclude):
        for ti in edge_tris[frozenset((p, q))]:
            if not used[ti] and ti != exclude:
                return ti
        return None

    strips = []
    for start in range(len(triangles)):
        if used[start]:
            continue
        a, b, c = triangles[start]
        strip = [a, b, c]
        used[start] = True
        prev = start
        while True:
            i = len(strip) - 2  # strip index of the triangle about to be added
            p, q = strip[-2], strip[-1]
            nb = neighbor(p, q, prev)
            if nb is None:
                break
            r = third(nb, p, q)
            if r is None:
                break
            required = (p, q, r) if (i % 2 == 0) else (q, p, r)
            if not cyclic_match(nb, required):
                break
            strip.append(r)
            used[nb] = True
            prev = nb
        strips.append(strip)
    return strips


def stitch_strips(strips):
    """Concatenate strips into one using degenerate triangles, keeping each
    sub-strip's first triangle on an even index (GL winding rule)."""
    combined = list(strips[0])
    for s in strips[1:]:
        if not s:
            continue
        combined.append(combined[-1])  # duplicate current tail (degenerate)
        combined.append(s[0])          # duplicate next head (degenerate)
        if len(combined) % 2 != 0:
            combined.append(s[0])      # extra degenerate fixes winding parity
        combined.extend(s)
    return combined


def canon_tri(t):
    """Rotate a triangle so its smallest index is first (winding preserved)."""
    a, b, c = t
    if a <= b and a <= c:
        return (a, b, c)
    if b <= a and b <= c:
        return (b, c, a)
    return (c, a, b)


def decode_strip(strip):
    """Decode a GL_TRIANGLE_STRIP index list to triangles, dropping degenerates."""
    tris = []
    for i in range(len(strip) - 2):
        t = (strip[i], strip[i + 1], strip[i + 2]) if (i % 2 == 0) else (strip[i + 1], strip[i], strip[i + 2])
        if t[0] == t[1] or t[1] == t[2] or t[0] == t[2]:
            continue
        tris.append(t)
    return tris


def verify_strip(strip, triangles):
    """True if the stitched strip decodes back to exactly the source triangles."""
    from collections import Counter
    want = Counter(canon_tri(t) for t in triangles)
    got = Counter(canon_tri(t) for t in decode_strip(strip))
    return want == got


def parse_obj(source_path):
    """Parse a .obj into per-corner (pos, normal, uv) arrays, fan-triangulated."""
    positions, normals, uvs = [], [], []
    out_v, out_n, out_t = [], [], []

    def resolve(tok, count):
        i = int(tok)
        return i - 1 if i > 0 else count + i  # 1-based, or negative from end

    with open(source_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            tag = p[0]
            if tag == "v":
                positions.append((float(p[1]), float(p[2]), float(p[3])))
            elif tag == "vn":
                normals.append((float(p[1]), float(p[2]), float(p[3])))
            elif tag == "vt":
                uvs.append((float(p[1]), float(p[2]) if len(p) > 2 else 0.0))
            elif tag == "f":
                face = []
                for v in p[1:]:
                    s = v.split("/")
                    vi = resolve(s[0], len(positions))
                    ti = resolve(s[1], len(uvs)) if len(s) > 1 and s[1] else -1
                    ni = resolve(s[2], len(normals)) if len(s) > 2 and s[2] else -1
                    face.append((vi, ti, ni))
                # Fan-triangulate.
                for k in range(1, len(face) - 1):
                    for (vi, ti, ni) in (face[0], face[k], face[k + 1]):
                        out_v.append(positions[vi])
                        out_n.append(normals[ni] if 0 <= ni < len(normals) else (0.0, 0.0, 0.0))
                        uv = uvs[ti] if 0 <= ti < len(uvs) else (0.0, 0.0)
                        out_t.append((uv[0], 1.0 - uv[1]))  # flip V for GS texel origin
    return out_v, out_n, out_t


def bake_mesh(out_v, out_n, out_t):
    """Core mesh baker. Dedups corners, stripifies (verified) when it is a win,
    else emits an unindexed list. Returns a dict with the emitted vec4/vec3/vec2
    byte blobs, topology, vertex count and object-space bounding sphere. Shared by
    bake_obj_model and the level compiler's sector meshing."""
    count = len(out_v)
    if count == 0 or count % 3 != 0:
        raise ValueError(f"bake_mesh: not a triangle soup ({count} corners)")
    tri_count = count // 3

    uv, un, ut, triangles = dedup_corners(out_v, out_n, out_t)
    topology = BAKED_TOPOLOGY_LIST
    emit_v, emit_n, emit_t = out_v, out_n, out_t  # list fallback (per-corner)
    strip_runs = 0

    try:
        strips = stripify(triangles)
        combined = stitch_strips(strips)
        ratio = len(combined) / float(tri_count)
        if ratio <= STRIP_MAX_VERTS_PER_TRI and verify_strip(combined, triangles):
            topology = BAKED_TOPOLOGY_STRIP
            emit_v = [uv[i] for i in combined]
            emit_n = [un[i] for i in combined]
            emit_t = [ut[i] for i in combined]
            strip_runs = len(strips)
    except Exception:  # noqa: BLE001 — any strip failure falls back to a list
        pass

    (cx, cy, cz), radius = bounding_sphere(emit_v)
    vbytes = b"".join(struct.pack("<ffff", p[0], p[1], p[2], 1.0) for p in emit_v)
    nbytes = b"".join(struct.pack("<fff", *n) for n in emit_n)
    tbytes = b"".join(struct.pack("<ff", *t) for t in emit_t)

    return {
        "topology": topology,
        "vert_count": len(emit_v),
        "tri_count": tri_count,
        "strip_runs": strip_runs,
        "vbytes": vbytes,
        "nbytes": nbytes,
        "tbytes": tbytes,
        "center": (cx, cy, cz),
        "radius": radius,
    }


def bake_obj_model(source_path, has_texture):
    """Parse a .obj and bake it as BKM2 v2. Positions are vec4 (x,y,z,1). One
    mesh, one material (materialIndex 0) when has_texture. Byte-compatible with
    the historical cooker output (golden-tested)."""
    out_v, out_n, out_t = parse_obj(source_path)
    m = bake_mesh(out_v, out_n, out_t)

    if m["topology"] == BAKED_TOPOLOGY_STRIP:
        print(f"    strip: {m['tri_count']} tris -> {m['vert_count']} strip verts "
              f"({m['vert_count'] / float(m['tri_count']):.2f} v/tri, {m['strip_runs']} runs)")
    else:
        print(f"    list:  {m['tri_count']} tris")

    vbytes, nbytes, tbytes = m["vbytes"], m["nbytes"], m["tbytes"]
    cx, cy, cz = m["center"]
    mat_count = 1 if has_texture else 0
    header_size, mesh_size, mat_size = 16, 48, 8 * mat_count
    verts_off = align16(header_size + mesh_size + mat_size)
    norms_off = align16(verts_off + len(vbytes))
    uvs_off = align16(norms_off + len(nbytes))
    total = uvs_off + len(tbytes)

    buf = bytearray(total)
    struct.pack_into("<IIII", buf, 0, BAKED_MODEL_MAGIC, BAKED_MODEL_VERSION, 1, mat_count)
    struct.pack_into("<IIIII", buf, 16, m["vert_count"], 0, verts_off, norms_off, uvs_off)
    struct.pack_into("<I", buf, 36, m["topology"])
    struct.pack_into("<ffff", buf, 40, cx, cy, cz, m["radius"])
    struct.pack_into("<II", buf, 56, 0, 0)  # reserved[2]
    if mat_count:
        struct.pack_into("<II", buf, 64, 0, 0)  # diffuseTexRef = dependency 0
    buf[verts_off:verts_off + len(vbytes)] = vbytes
    buf[norms_off:norms_off + len(nbytes)] = nbytes
    buf[uvs_off:uvs_off + len(tbytes)] = tbytes
    return bytes(buf), ".bkm"
