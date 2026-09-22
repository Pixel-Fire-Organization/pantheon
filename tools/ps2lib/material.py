"""Baked material payload (MaterialAssetHeader), cooked as a RES_MATERIAL
.ps2a payload by tools/cook_assets.py. Mirrors
engine/include/graphics/MaterialFormat.h and engine/include/graphics/Types.h
field-for-field -- see docs/formats/MATERIAL_FORMAT.md.

A material names a shader type plus a fixed, generic set of parameter slots
whose MEANING depends on that shader type. This is what lets a future shader
type (e.g. water) add its own parameters without a format change: it defines
its own mapping over the same slot arrays.
"""

import struct

MAGIC = 0x324C544D  # "MTL2" little-endian
VERSION = 1

FLOAT_PARAMS = 8
COLOR_PARAMS = 4
TEX_SLOTS = 4
TEXREF_NONE = 0xFFFFFFFF

SHADER_PBR_STANDARD = 0

# PbrStandard's slot mapping -- the single source of truth mirrored by
# engine/include/graphics/Types.h and every backend's shading code.
PBR_FLOAT_METALLIC = 0
PBR_FLOAT_ROUGHNESS = 1
PBR_FLOAT_NORMAL_SCALE = 2
PBR_FLOAT_ALPHA_CUTOFF = 3

PBR_COLOR_BASE = 0      # baseColorFactor, RGBA
PBR_COLOR_EMISSIVE = 1  # emissiveFactor, RGB (alpha unused)

PBR_TEX_ALBEDO = 0
PBR_TEX_NORMAL = 1
PBR_TEX_ORM = 2  # packed occlusion/roughness/metallic

FLAG_ALPHA_MASK = 1 << 0
FLAG_ALPHA_BLEND = 1 << 1
FLAG_DOUBLE_SIDED = 1 << 2

_HEADER = "<IIBB2x8f16f4I"
assert struct.calcsize(_HEADER) == 124


def pack_material(shader_type, flags, float_params, color_params, texture_refs):
    """Pack a MaterialAssetHeader.

    `float_params`: up to FLOAT_PARAMS floats, padded with 0.0.
    `color_params`: up to COLOR_PARAMS (r, g, b, a) tuples, padded with zeros.
    `texture_refs`: up to TEX_SLOTS dependency indices, padded with TEXREF_NONE.
    """
    if len(float_params) > FLOAT_PARAMS:
        raise ValueError(f"material: {len(float_params)} float params, max {FLOAT_PARAMS}")
    if len(color_params) > COLOR_PARAMS:
        raise ValueError(f"material: {len(color_params)} colour params, max {COLOR_PARAMS}")
    if len(texture_refs) > TEX_SLOTS:
        raise ValueError(f"material: {len(texture_refs)} texture refs, max {TEX_SLOTS}")

    fp = list(float_params) + [0.0] * (FLOAT_PARAMS - len(float_params))
    cp = []
    for i in range(COLOR_PARAMS):
        c = color_params[i] if i < len(color_params) else (0.0, 0.0, 0.0, 0.0)
        cp.extend(c)
    tr = list(texture_refs) + [TEXREF_NONE] * (TEX_SLOTS - len(texture_refs))

    return struct.pack(_HEADER, MAGIC, VERSION, shader_type, flags, *fp, *cp, *tr)


def describe(blob):
    """Summarise a MaterialAssetHeader payload for the inspection and
    validation tools. Returns a dict, or raises ValueError."""
    if len(blob) < struct.calcsize(_HEADER):
        raise ValueError(f"material payload is {len(blob)} bytes, need at least {struct.calcsize(_HEADER)}")

    fields = struct.unpack_from(_HEADER, blob, 0)
    magic, version, shader_type, flags = fields[0:4]
    float_params = fields[4:4 + FLOAT_PARAMS]
    color_flat = fields[4 + FLOAT_PARAMS:4 + FLOAT_PARAMS + COLOR_PARAMS * 4]
    color_params = [tuple(color_flat[i * 4:i * 4 + 4]) for i in range(COLOR_PARAMS)]
    texture_refs = fields[4 + FLOAT_PARAMS + COLOR_PARAMS * 4:]

    if magic != MAGIC:
        raise ValueError(f"bad material magic 0x{magic:08X}, expected 0x{MAGIC:08X}")
    if version != VERSION:
        raise ValueError(f"unsupported material version {version}")

    return {
        "shader_type": shader_type,
        "flags": flags,
        "float_params": list(float_params),
        "color_params": color_params,
        "texture_refs": list(texture_refs),
    }
