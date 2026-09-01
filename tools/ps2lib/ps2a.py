"""The .ps2a asset container: a fixed 2080-byte AssetFileHeader + payload.

The runtime resource manager (engine/src/EngineResource.cpp) reads this header to
find the asset type and its dependency keys. Byte-compatible with the historical
cook_assets output.
"""

import struct

MAGIC = 0x50533241  # "PS2A" little-endian
MAX_DEPS = 8
MAX_PATH_LEN = 256
EXT_LEN = 16  # matches AssetFileHeader.ext[16]

TYPE_MAP = {"TEXTURE": 0, "MODEL": 1, "SOUND": 2, "FONT": 3, "THEME": 4}

# 4 + 4 + 1 + 3 + 16 + (8 * 256) + 4 = 2080 bytes
HEADER_SIZE = 4 + 4 + 1 + 3 + EXT_LEN + (MAX_DEPS * MAX_PATH_LEN) + 4


def pack_dep_string(dep_str):
    encoded = dep_str.encode("utf-8")[:MAX_PATH_LEN - 1]
    return encoded + b"\x00" * (MAX_PATH_LEN - len(encoded))


def write_ps2a(type_id, payload, dep_keys, ext_str):
    """Build a .ps2a blob: header + payload. `dep_keys` are the full canonical
    dependency keys (e.g. "RASSETS/BOX.PS2A") in dependency order (max 8)."""
    dep_count = min(len(dep_keys), MAX_DEPS)

    ext_bytes = ext_str.encode("utf-8")[:EXT_LEN - 1]
    ext_field = ext_bytes + b"\x00" * (EXT_LEN - len(ext_bytes))

    header = struct.pack("<II", MAGIC, type_id)
    header += struct.pack("B", dep_count)
    header += b"\x00" * 3
    header += ext_field
    for i in range(MAX_DEPS):
        if i < dep_count:
            header += pack_dep_string(dep_keys[i])
        else:
            header += b"\x00" * MAX_PATH_LEN
    header += struct.pack("<I", len(payload))
    assert len(header) == HEADER_SIZE, f"Header size mismatch: {len(header)} != {HEADER_SIZE}"

    return header + payload


TYPE_NAMES = {v: k for k, v in TYPE_MAP.items()}


def read_ps2a(path):
    """Parse a .ps2a file. Returns a dict describing it, or raises ValueError.

    The inverse of write_ps2a, used by the inspection and validation tools so
    they read the format exactly the way the runtime does rather than
    re-deriving offsets.
    """
    with open(path, "rb") as fh:
        blob = fh.read()

    if len(blob) < HEADER_SIZE:
        raise ValueError(f"{path}: shorter than a header ({len(blob)} < {HEADER_SIZE})")

    magic, type_id = struct.unpack_from("<II", blob, 0)
    if magic != MAGIC:
        raise ValueError(f"{path}: bad magic 0x{magic:08X}, expected 0x{MAGIC:08X}")

    dep_count = blob[8]
    if dep_count > MAX_DEPS:
        raise ValueError(f"{path}: dep count {dep_count} exceeds {MAX_DEPS}")

    ext_off = 4 + 4 + 1 + 3
    ext = blob[ext_off:ext_off + EXT_LEN].split(b"\x00", 1)[0].decode("utf-8", "replace")

    deps = []
    dep_off = ext_off + EXT_LEN
    for i in range(dep_count):
        raw = blob[dep_off + i * MAX_PATH_LEN: dep_off + (i + 1) * MAX_PATH_LEN]
        deps.append(raw.split(b"\x00", 1)[0].decode("utf-8", "replace"))

    (data_size,) = struct.unpack_from("<I", blob, HEADER_SIZE - 4)
    actual = len(blob) - HEADER_SIZE
    if data_size != actual:
        raise ValueError(f"{path}: header says {data_size} payload bytes, file has {actual}")

    return {
        "path": path,
        "type_id": type_id,
        "type": TYPE_NAMES.get(type_id, f"<unknown {type_id}>"),
        "ext": ext,
        "deps": deps,
        "data_size": data_size,
        "payload": blob[HEADER_SIZE:],
        "total_size": len(blob),
    }
