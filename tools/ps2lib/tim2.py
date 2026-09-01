"""TIM2 texture encoding (PS2-native). rgba32 / pal8, optional box-filtered mips.

Byte-compatible with the historical cooker encoders (golden-tested via
BOX.PS2A). `encode_pal8_cutout` is a level-pipeline addition: it reserves CLUT
index 0 as fully transparent for billboard-impostor cutouts.
"""

import struct

TIM2_IMGTYPE_RGBA16 = 0x01
TIM2_IMGTYPE_RGBA32 = 0x03
TIM2_IMGTYPE_IDTEX8 = 0x05

# GsTex1 carries the sampling filter. Zero has always meant "let the backend
# choose", and every texture cooked before this field was used contains zero, so
# an explicit choice is marked rather than encoded as a value. Bit 63 is unused
# by the hardware register, which is what makes it available to say "the filter
# fields below are authoritative".
TEX1_EXPLICIT = 1 << 63
TEX1_MMAG_SHIFT = 8
TEX1_MMIN_SHIFT = 9
FILTER_NEAREST = 0
FILTER_LINEAR = 1


def tex1_for_filter(name):
    """Encode a filter name into a GsTex1 image, or 0 for the backend default."""
    if not name:
        return 0
    key = str(name).lower()
    if key == "nearest":
        return TEX1_EXPLICIT | (FILTER_NEAREST << TEX1_MMAG_SHIFT) | (FILTER_NEAREST << TEX1_MMIN_SHIFT)
    if key == "linear":
        return TEX1_EXPLICIT | (FILTER_LINEAR << TEX1_MMAG_SHIFT) | (FILTER_LINEAR << TEX1_MMIN_SHIFT)
    raise ValueError("unknown texture filter '%s'" % name)


def filter_from_tex1(tex1):
    """Decode a GsTex1 image; returns 'nearest', 'linear', or None for default."""
    if not (tex1 & TEX1_EXPLICIT):
        return None
    return "linear" if ((tex1 >> TEX1_MMAG_SHIFT) & 1) else "nearest"


def mip_level_count(w, h, requested):
    """Total mip levels (>=1) for `requested` extra levels, clamped so the
    smallest level stays >= 8x8."""
    levels = 1
    while levels <= requested:
        if (w >> levels) < 8 or (h >> levels) < 8:
            break
        levels += 1
    return levels


def assemble_tim2(width, height, level_payloads, image_type, clut_bytes, gs_tex1=0):
    """Assemble a single-picture TIM2 from per-level pixel payloads (largest
    first, each padded to 16 bytes) and an optional linear CLUT."""
    image = bytearray()
    for lp in level_payloads:
        image += lp
        image += b"\x00" * ((-len(image)) % 16)  # pad each level to 16 bytes
    image_size = len(image)
    clut_size = len(clut_bytes) if clut_bytes else 0
    clut_colors = (clut_size // 4) if clut_bytes else 0
    mip_count = len(level_payloads)
    clut_type = 0x03 if clut_bytes else 0  # A8B8G8R8 CLUT, CSM1, stored linear

    total_size = 0x30 + image_size + clut_size
    pic = struct.pack("<III", total_size, clut_size, image_size)  # totalSize, clutSize, imageSize
    pic += struct.pack("<HH", 0x30, clut_colors)                  # headerSize, clutColors
    pic += struct.pack("<BBBB", 0, mip_count, clut_type, image_type)  # pictFormat, mipmapCount, clutType, imageType
    pic += struct.pack("<HH", width, height)                      # imageWidth, imageHeight
    pic += struct.pack("<QQ", 0, gs_tex1)                         # GsTex0, GsTex1
    pic += struct.pack("<II", 0, 0)                               # GsRegs, GsTexClut
    assert len(pic) == 0x30, len(pic)

    # File header (16 bytes): magic, formatVersion=4, formatId=0, pictureCount=1, pad[8]
    fh = b"TIM2" + struct.pack("<BBH", 0x04, 0x00, 1) + (b"\x00" * 8)
    assert len(fh) == 16, len(fh)

    return fh + pic + bytes(image) + (clut_bytes if clut_bytes else b"")


def encode_rgba32(img, mip_levels, gs_tex1=0):
    """RGBA32 (A8B8G8R8) TIM2, box-filtered mip chain."""
    from PIL import Image
    w, h = img.size
    levels = mip_level_count(w, h, mip_levels)
    payloads = []
    for l in range(levels):
        lw, lh = max(1, w >> l), max(1, h >> l)
        lvl = img if l == 0 else img.resize((lw, lh), Image.BOX)
        payloads.append(lvl.tobytes())
    return assemble_tim2(w, h, payloads, TIM2_IMGTYPE_RGBA32, None, gs_tex1)


def encode_pal8(img, mip_levels, gs_tex1=0):
    """8-bit indexed (IDTEX8) TIM2 with a linear 256-entry A8B8G8R8 CLUT. Level 0
    is quantized; lower mips are box-filtered then remapped to the same palette
    so one CLUT serves the whole chain. Alpha is not preserved (set opaque)."""
    from PIL import Image
    w, h = img.size
    rgb = img.convert("RGB")
    pal_img = rgb.quantize(colors=256, method=Image.Quantize.FASTOCTREE)

    levels = mip_level_count(w, h, mip_levels)
    payloads = []
    for l in range(levels):
        lw, lh = max(1, w >> l), max(1, h >> l)
        idx_img = pal_img if l == 0 else rgb.resize((lw, lh), Image.BOX).quantize(palette=pal_img, dither=Image.Dither.NONE)
        payloads.append(idx_img.tobytes())

    # CLUT: palette RGB triples -> linear A8B8G8R8 (alpha 0x80 == GS 1.0), 256 entries.
    palette = pal_img.getpalette() or []
    clut = bytearray()
    for i in range(256):
        r = palette[i * 3 + 0] if i * 3 + 2 < len(palette) else 0
        g = palette[i * 3 + 1] if i * 3 + 2 < len(palette) else 0
        b = palette[i * 3 + 2] if i * 3 + 2 < len(palette) else 0
        clut += struct.pack("<BBBB", r, g, b, 0x80)
    return assemble_tim2(w, h, payloads, TIM2_IMGTYPE_IDTEX8, bytes(clut), gs_tex1)


def encode_coverage8(img, gs_tex1=0):
    """IDTEX8 TIM2 holding coverage only: the index is the source alpha and the
    CLUT is a ramp from transparent to opaque white.

    One byte per texel with antialiased edges intact, and it round-trips exactly
    through the runtime's console-alpha rescale, so the same payload is correct
    whether a backend samples it natively or expands it. This is what a glyph
    atlas wants: colour comes from the drawing call, never from the font."""
    rgba = img.convert("RGBA")
    w, h = rgba.size
    payload = rgba.getchannel("A").tobytes()

    clut = bytearray()
    for i in range(256):
        clut += struct.pack("<BBBB", 255, 255, 255, (i * 0x80) // 255)
    return assemble_tim2(w, h, [bytes(payload)], TIM2_IMGTYPE_IDTEX8, bytes(clut), gs_tex1)


def encode_pal8_cutout(img):
    """IDTEX8 TIM2 whose CLUT index 0 is fully transparent (alpha 0). Used for
    billboard-impostor atlases: fully-transparent source pixels map to index 0,
    opaque pixels are quantized into indices 1..255. No mips (impostors are drawn
    at roughly one screen size). CLUT alpha is 0x80 (GS 1.0) for opaque entries."""
    from PIL import Image
    w, h = img.size
    rgba = img.convert("RGBA")
    px = rgba.load()

    opaque_rgb = rgba.convert("RGB")
    # Quantize the RGB into 255 colours (indices become 0..254); shift to 1..255
    # so index 0 is free for transparency.
    pal_img = opaque_rgb.quantize(colors=255, method=Image.Quantize.FASTOCTREE)
    pal_idx = pal_img.load()

    indices = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            if px[x, y][3] < 128:
                indices[y * w + x] = 0  # transparent
            else:
                indices[y * w + x] = pal_idx[x, y] + 1

    palette = pal_img.getpalette() or []
    clut = bytearray()
    clut += struct.pack("<BBBB", 0, 0, 0, 0)  # index 0 = transparent
    for i in range(255):
        r = palette[i * 3 + 0] if i * 3 + 2 < len(palette) else 0
        g = palette[i * 3 + 1] if i * 3 + 2 < len(palette) else 0
        b = palette[i * 3 + 2] if i * 3 + 2 < len(palette) else 0
        clut += struct.pack("<BBBB", r, g, b, 0x80)
    return assemble_tim2(w, h, [bytes(indices)], TIM2_IMGTYPE_IDTEX8, bytes(clut))


def convert_texture_to_tim2(source_path, fmt="rgba32", mip_levels=0, tex_filter=None):
    """Decode any Pillow-supported image and re-encode as TIM2. `fmt` is
    'rgba32' (default), 'pal8' or 'coverage8'; `mip_levels` is the number of
    extra mip levels; `tex_filter` is 'nearest', 'linear' or None for the
    backend default.
    Returns (tim2_bytes, ".tm2") or raises ImportError if Pillow is absent."""
    from PIL import Image
    img = Image.open(source_path).convert("RGBA")
    gs_tex1 = tex1_for_filter(tex_filter)
    if fmt == "coverage8":
        return encode_coverage8(img, gs_tex1), ".tm2"
    if fmt == "pal8":
        return encode_pal8(img, mip_levels, gs_tex1), ".tm2"
    return encode_rgba32(img, mip_levels, gs_tex1), ".tm2"


# Names for the imageType values the encoders above write. Derived from the
# constants rather than restated, so the two cannot drift.
IMAGE_TYPE_NAMES = {
    TIM2_IMGTYPE_RGBA16: "rgba16",
    TIM2_IMGTYPE_RGBA32: "rgba32",
    TIM2_IMGTYPE_IDTEX8: "pal8",
}


def describe(blob):
    """Summarise a TIM2 blob for the inspection and validation tools.

    Returns a dict, or raises ValueError. Reads only the file and picture
    headers - it never decodes pixels, so it stays cheap on a whole tree.
    """
    if len(blob) < 16 + 0x30:
        raise ValueError("shorter than a TIM2 header")
    if blob[:4] != b"TIM2":
        raise ValueError("bad TIM2 magic")

    total_size, clut_size, image_size = struct.unpack_from("<III", blob, 16)
    header_size, clut_colors = struct.unpack_from("<HH", blob, 16 + 12)
    _pict_format, mip_count, clut_type, image_type = struct.unpack_from("<BBBB", blob, 16 + 16)
    width, height = struct.unpack_from("<HH", blob, 16 + 20)
    (gs_tex1,) = struct.unpack_from("<Q", blob, 16 + 32)

    return {
        "width": width,
        "height": height,
        "format": IMAGE_TYPE_NAMES.get(image_type, "type%d" % image_type),
        "image_type": image_type,
        "mip_count": mip_count,
        "clut_colors": clut_colors,
        "clut_size": clut_size,
        "clut_type": clut_type,
        "image_size": image_size,
        "total_size": total_size,
        "header_size": header_size,
        "filter": filter_from_tex1(gs_tex1),
    }


def describe_text(blob):
    d = describe(blob)
    clut = ", clut %d colours" % d["clut_colors"] if d["clut_colors"] else ""
    filt = ", %s" % d["filter"] if d["filter"] else ""
    return "%dx%d %s, %d mip(s), %d image bytes%s%s" % (
        d["width"], d["height"], d["format"], d["mip_count"], d["image_size"], clut, filt)
