"""The PSTH cooked-theme payload: a header, a style memory image, font keys.

The style block is copied straight into live engine state rather than parsed,
which is what makes loading a theme cheap for a game. Three consequences follow
and are enforced here: the layout is fixed and its size is asserted against the
engine's own struct, there are no pointers in the image, and the payload carries
a checksum because a copy performs none of the validation a parse would.

See docs/formats/THEME_FORMAT.md.
"""

import struct

MAGIC = b"PSTH"
VERSION = 2

HEADER_SIZE = 16
STYLE_BYTES = 108
KEY_MAX = 48
FONT_REF_SIZE = 1 + KEY_MAX
MAX_FONT_REFS = 8

# Colour roles, in the order UiColor declares them. The generator checks this
# list against the engine header, so the two cannot drift.
COLOR_ROLES = [
    "WindowBackground", "PanelBackground", "Border", "Header", "Text", "TextDim",
    "TextAccent", "TextWarn", "TextDisabled", "ItemBackground", "ItemHovered",
    "ItemActive", "Focus", "BarTrack", "BarFill", "BarFillWarn", "Cursor", "CursorOutline",
]

# int16 metrics, in declaration order after the two floats. Version 2 spent the
# format's last two reserved slots on menuBarHeight and caretWidth, plus one
# more byte pair of colour growth (TextDisabled, above) covering iconSpacing.
INT_METRICS = [
    "panelPadding", "itemSpacing", "borderWidth", "textScale", "rowPadding",
    "barHeight", "cursorSize", "screenMargin", "panelGap", "scrollBarWidth",
    "menuBarHeight", "caretWidth", "iconSpacing",
]

FONT_ROLES = ["Body", "Header", "Value", "Default"]

# A checksum proves a file arrived as written; it says nothing about whether it
# was written sensibly, so the values are range-checked separately.
METRIC_RANGES = {
    "panelPadding": (0, 256), "itemSpacing": (0, 256), "borderWidth": (0, 64),
    "textScale": (1, 16), "rowPadding": (0, 256), "barHeight": (1, 256),
    "cursorSize": (1, 256), "screenMargin": (0, 512), "panelGap": (0, 512),
    "scrollBarWidth": (1, 128), "menuBarHeight": (1, 256), "caretWidth": (1, 32),
    "iconSpacing": (0, 128),
}
REPEAT_RANGE = (0.01, 5.0)

FNV_OFFSET_BASIS = 2166136261
FNV_PRIME = 16777619


class ThemeError(ValueError):
    """A theme that cannot be cooked or trusted, with the reason a human needs."""


def checksum(data):
    """FNV-1a over the payload after the header. Not cryptographic: its job is
    to catch a truncated or corrupted file, not a hostile one."""
    h = FNV_OFFSET_BASIS
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & 0xFFFFFFFF
    return h


def _check_metrics(metrics):
    for name in INT_METRICS:
        if name not in metrics:
            raise ThemeError(f"metric '{name}' is missing")
        lo, hi = METRIC_RANGES[name]
        value = metrics[name]
        if not isinstance(value, int) or not (lo <= value <= hi):
            raise ThemeError(f"metric '{name}' is {value}, outside {lo}..{hi}")
    for name in ("repeatDelaySeconds", "repeatIntervalSeconds"):
        if name not in metrics:
            raise ThemeError(f"metric '{name}' is missing")
        lo, hi = REPEAT_RANGE
        value = float(metrics[name])
        if not (lo <= value <= hi):
            raise ThemeError(f"metric '{name}' is {value}, outside {lo}..{hi}")


def pack_style(colors, metrics):
    """Build the style memory image: colours in role order, then the metrics."""
    _check_metrics(metrics)

    blob = b""
    for role in COLOR_ROLES:
        if role not in colors:
            raise ThemeError(f"colour role '{role}' is missing")
        rgba = colors[role]
        if len(rgba) != 4 or any((not isinstance(c, int)) or c < 0 or c > 255 for c in rgba):
            raise ThemeError(f"colour role '{role}' is not four bytes in 0..255")
        blob += struct.pack("<BBBB", *rgba)

    blob += struct.pack("<ff", float(metrics["repeatDelaySeconds"]), float(metrics["repeatIntervalSeconds"]))
    for name in INT_METRICS:
        blob += struct.pack("<h", metrics[name])
    blob += struct.pack("<h", 0)  # reserved

    if len(blob) != STYLE_BYTES:
        raise ThemeError(f"style image is {len(blob)} bytes, the format declares {STYLE_BYTES}")
    return blob


def write_theme(colors, metrics, fonts=None):
    """Build a PSTH payload."""
    style = pack_style(colors, metrics)

    refs = b""
    count = 0
    for role, key in sorted((fonts or {}).items()):
        if role not in FONT_ROLES:
            raise ThemeError(f"unknown font role '{role}'")
        encoded = str(key).encode("utf-8")
        if len(encoded) >= KEY_MAX:
            raise ThemeError(f"font key for '{role}' is longer than {KEY_MAX - 1} bytes")
        refs += struct.pack("<B", FONT_ROLES.index(role))
        refs += encoded + b"\x00" * (KEY_MAX - len(encoded))
        count += 1
    if count > MAX_FONT_REFS:
        raise ThemeError(f"{count} font references, at most {MAX_FONT_REFS} fit")

    body = style + refs
    header = MAGIC + struct.pack("<HHIB3x", VERSION, STYLE_BYTES, checksum(body), count)
    if len(header) != HEADER_SIZE:
        raise ThemeError(f"header is {len(header)} bytes, expected {HEADER_SIZE}")
    return header + body, ".thm"


def describe(blob):
    """Validate and summarise a PSTH payload, in the order a loader must."""
    if len(blob) < HEADER_SIZE:
        raise ThemeError("shorter than a theme header")
    if blob[:4] != MAGIC:
        raise ThemeError("bad PSTH magic")

    version, style_bytes, want, font_count = struct.unpack_from("<HHIB", blob, 4)
    if version != VERSION:
        raise ThemeError(f"theme layout version {version}, this build reads {VERSION}")
    if style_bytes != STYLE_BYTES:
        raise ThemeError(f"style block is {style_bytes} bytes, this build expects {STYLE_BYTES}")

    expected = HEADER_SIZE + STYLE_BYTES + font_count * FONT_REF_SIZE
    if len(blob) != expected:
        raise ThemeError(f"payload is {len(blob)} bytes, header describes {expected}")

    body = blob[HEADER_SIZE:]
    got = checksum(body)
    if got != want:
        raise ThemeError(f"checksum {got:#010x} does not match the recorded {want:#010x}")

    colors = {}
    for i, role in enumerate(COLOR_ROLES):
        colors[role] = list(struct.unpack_from("<BBBB", body, i * 4))

    off = len(COLOR_ROLES) * 4
    delay, interval = struct.unpack_from("<ff", body, off)
    off += 8
    metrics = {"repeatDelaySeconds": delay, "repeatIntervalSeconds": interval}
    for name in INT_METRICS:
        (metrics[name],) = struct.unpack_from("<h", body, off)
        off += 2

    _check_metrics({**metrics, **{k: int(v) for k, v in metrics.items() if isinstance(v, int)}})

    fonts = {}
    off = STYLE_BYTES
    for _ in range(font_count):
        role = body[off]
        key = body[off + 1:off + FONT_REF_SIZE].split(b"\x00", 1)[0].decode("utf-8", "replace")
        if role >= len(FONT_ROLES):
            raise ThemeError(f"font reference names role {role}, which does not exist")
        fonts[FONT_ROLES[role]] = key
        off += FONT_REF_SIZE

    return {"version": version, "colors": colors, "metrics": metrics, "fonts": fonts}


def describe_text(blob):
    d = describe(blob)
    return "%d colour roles, text scale %d, %d font reference(s)" % (
        len(d["colors"]), d["metrics"]["textScale"], len(d["fonts"]))
