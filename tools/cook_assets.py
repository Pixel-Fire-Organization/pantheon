#!/usr/bin/env python3
"""
cook_assets.py - stage 3 of the build pipeline: cook.

Reads JSON + source file pairs from the asset source directory and bakes each
into an engine-native .ps2a, using the target platform's cook list to decide how.
Output is per platform, because the right encoding is a hardware question - see
docs/PIPELINE.md.

Usage:
    python3 tools/cook_assets.py --platform ps2pal
    python3 tools/cook_assets.py --cooklist <FILE> --src <DIR> --dst <DIR>

JSON schema (e.g. player_tex.json):
    { "type": "TEXTURE", "source": "player_tex.png", "deps": [] }
    { "type": "MODEL",   "source": "prop.obj",       "deps": ["prop_tex"] }

Binary .ps2a layout:
    [AssetFileHeader]  (fixed-size, 2080 bytes)
    [payload]          (TIM2 for textures, baked BKM2 blob for models)

The TIM2 / BKM2 encoders and the .ps2a header writer live in tools/ps2lib so the
level compiler (tools/compile_level.py) bakes geometry and textures through the
exact same code path.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ps2lib import font, material as materiallib, mesh, ps2a, theme as themelib, tim2


DEFAULT_COOKLIST = {
    "platform": "<none>",
    "assets": {
        "TEXTURE": {"enabled": True, "format": "source"},
        "MODEL": {"enabled": True},
        "SOUND": {"enabled": False},
        "FONT": {"enabled": False},
        "THEME": {"enabled": True},
        "MATERIAL": {"enabled": True, "bake_normal": True, "bake_orm": True},
    },
}

# A cooked TEXTURE (standalone or a material's albedo/normal/ORM map) is read
# back at runtime through EngineIO's single fixed-size shared read buffer
# (IO_READ_BUFFER_SIZE, each platform's own PlatformConstants.h) -- an asset
# bigger than that is rejected at load time, silently as far as the cook and
# package stages are concerned, since neither checks a *single* asset's byte
# size against anything narrower than the whole assets.TEXTURE budget. This
# mirrors compile_level.py's own LEVEL_TEXTURE_MAX_BYTES_BY_PLATFORM safety
# net (same values, same "kept in sync with the engine headers by
# tools/tests/test_compile_level.py" discipline -- see
# test_cook_assets_io_read_buffer_sizes_match_compile_level there), applied
# here to the material-map bake path (_bake_material_map) the same way.
IO_READ_BUFFER_SIZE_BY_PLATFORM = {
    "ps2": 512 * 1024,
    "vita": 4 * 1024 * 1024,
    "win32": 4 * 1024 * 1024,
    "psp": 512 * 1024,
    "nx": 4 * 1024 * 1024,
}
DEFAULT_IO_READ_BUFFER_SIZE = 512 * 1024  # conservative fallback with no --platform


def load_cooklist(path):
    """Read a platform cook list. Missing file means cook everything as authored,
    which is what a platform that has not declared a policy should get."""
    if not path or not os.path.isfile(path):
        return DEFAULT_COOKLIST
    with open(path, "r", encoding="utf-8-sig") as fh:
        return json.load(fh)


def cooklist_for_platform(project_root, platform):
    """Map a CMake platform name onto the directory holding its cook list.
    Regional variants share their base platform's list."""
    name = (platform or "").lower()
    if name.startswith("ps2"):
        return os.path.join(project_root, "engine", "config", "ps2", "cooklist.json")
    if name.startswith("vita"):
        return os.path.join(project_root, "engine", "config", "vita", "cooklist.json")
    if name:
        return os.path.join(project_root, "engine", "config", name, "cooklist.json")
    return None


def _bake_material_map(image_path, dep_base_name, texture_policy, dst_dir, max_dimensions=None, max_bytes=None):
    """Bake one material texture map (already-resolved image path, or a
    packed-in-memory PIL Image) into its own standalone TEXTURE .ps2a,
    written as f"{dep_base_name}.PS2A" in dst_dir. Returns dep_base_name.

    A material's maps are authored as plain image files, not separate JSON
    descriptors -- this is what lets a material.json name real files
    directly (see material.schema.json) while every map still flows through
    the ordinary standalone-TEXTURE dependency mechanism at runtime.

    max_dimensions is the material policy's own per-map override for normal
    and ORM maps (normal_max_dimensions / orm_max_dimensions -- there is no
    such override for albedo, which always uses the platform's general
    ceiling); absent one (including always, for albedo), this falls back to
    the platform's blanket TEXTURE policy (max_width/max_height) rather than
    baking the source art at whatever resolution it happens to ship in -- a
    material map with no explicit cap must still answer honestly to this
    platform's texture budget, the same "override only where it differs"
    shape GetTextureBudgetBytes/SupportsPbrShading already use elsewhere in
    this engine.

    max_bytes is a second, independent safety net regardless of that
    dimension cap: the encoded .ps2a must still fit this platform's own
    IO_READ_BUFFER_SIZE (IO_READ_BUFFER_SIZE_BY_PLATFORM above), the same
    "cap dimensions, then keep halving until the encoded blob actually fits"
    two-step compile_level.py's own _bake_material already performs for a
    level's baked-in materials."""
    from PIL import Image

    tex_fmt = str(texture_policy.get("format", "rgba32")).lower()
    if tex_fmt == "source":
        tex_fmt = "rgba32"
    if tex_fmt not in ("rgba32", "pal8", "coverage8"):
        tex_fmt = "rgba32"

    if not max_dimensions:
        max_dimensions = {"max_width": texture_policy.get("max_width"), "max_height": texture_policy.get("max_height")}

    img = image_path if hasattr(image_path, "size") else Image.open(image_path).convert("RGBA")
    if max_dimensions:
        max_w = max_dimensions.get("max_width")
        max_h = max_dimensions.get("max_height")
        if max_w and max_h and (img.size[0] > max_w or img.size[1] > max_h):
            ratio = min(max_w / img.size[0], max_h / img.size[1])
            new_size = (max(1, int(img.size[0] * ratio)), max(1, int(img.size[1] * ratio)))
            print(f"    downscaled material map '{dep_base_name}' from {img.size[0]}x{img.size[1]} to {new_size[0]}x{new_size[1]}")
            img = img.resize(new_size, Image.BOX)

    def encode(image):
        if tex_fmt == "pal8":
            return tim2.encode_pal8(image.convert("RGB"), 0)
        if tex_fmt == "coverage8":
            return tim2.encode_coverage8(image)
        return tim2.encode_rgba32(image, 0)

    payload = encode(img)
    blob = ps2a.write_ps2a(ps2a.TYPE_MAP["TEXTURE"], payload, [], ".tm2")

    orig_size = img.size
    orig_bytes = len(blob)
    while max_bytes and len(blob) > max_bytes and (img.size[0] > 1 or img.size[1] > 1):
        img = img.resize((max(1, img.size[0] // 2), max(1, img.size[1] // 2)), Image.BOX)
        payload = encode(img)
        blob = ps2a.write_ps2a(ps2a.TYPE_MAP["TEXTURE"], payload, [], ".tm2")
    if len(blob) != orig_bytes:
        print(f"  WARN: material map '{dep_base_name}' baked at {orig_size[0]}x{orig_size[1]} ({orig_bytes} bytes) exceeds the "
              f"IO read buffer ({max_bytes} bytes); downscaled further to {img.size[0]}x{img.size[1]} ({len(blob)} bytes)")

    with open(os.path.join(dst_dir, f"{dep_base_name}.PS2A"), "wb") as fh:
        fh.write(blob)
    print(f"  BAKE:  {dep_base_name}.PS2A (material map, {len(payload)} bytes payload)")
    return dep_base_name


def _pack_orm_image(occlusion_path, roughness_path, metallic_path):
    """Combine up to three separate greyscale maps into one RGB image
    (R=occlusion, G=roughness, B=metallic) -- the common case art ships in.
    A channel with no source image gets a safe default (occlusion/roughness
    default to fully-off, i.e. 255; metallic defaults to fully dielectric,
    i.e. 0). All three are resized to the first present map's size."""
    from PIL import Image

    sources = [p for p in (occlusion_path, roughness_path, metallic_path) if p]
    size = Image.open(sources[0]).size

    def band(path, default):
        if not path:
            return Image.new("L", size, default)
        img = Image.open(path).convert("L")
        return img.resize(size, Image.BOX) if img.size != size else img

    r = band(occlusion_path, 255)
    g = band(roughness_path, 255)
    b = band(metallic_path, 0)
    return Image.merge("RGB", (r, g, b)).convert("RGBA")


def _bake_material_payload(meta, json_path, src_dir, dst_dir, cooklist):
    """Build a RES_MATERIAL payload plus the list of texture dependency base
    names it names, in slot order (see materiallib.PBR_TEX_*). Returns
    (payload_bytes, ext_str, dep_base_names), or None on error (logged)."""
    shader_type_str = str(meta.get("shaderType", "pbr_standard")).lower()
    if shader_type_str != "pbr_standard":
        print(f"  ERROR: unknown material shaderType '{shader_type_str}' in {json_path}")
        return None

    base_name = os.path.splitext(os.path.basename(json_path))[0].upper()
    material_policy = (cooklist or DEFAULT_COOKLIST).get("assets", {}).get("MATERIAL", {})
    texture_policy = (cooklist or DEFAULT_COOKLIST).get("assets", {}).get("TEXTURE", {})
    max_bytes = IO_READ_BUFFER_SIZE_BY_PLATFORM.get(
        str((cooklist or DEFAULT_COOKLIST).get("platform", "")).lower(), DEFAULT_IO_READ_BUFFER_SIZE)

    def resolve(field):
        name = meta.get(field)
        return os.path.join(src_dir, name) if name else None

    albedo_path = resolve("albedo")
    normal_path = resolve("normal") if material_policy.get("bake_normal", True) else None
    orm_path = resolve("orm")
    occlusion_path = resolve("occlusion")
    roughness_path = resolve("roughness")
    metallic_path = resolve("metallic")

    dep_names = []
    texture_refs = [materiallib.TEXREF_NONE] * materiallib.TEX_SLOTS

    if albedo_path:
        if not os.path.isfile(albedo_path):
            print(f"  ERROR: material albedo source not found: {albedo_path}")
            return None
        # No per-map override for albedo (unlike normal/orm below) -- the
        # schema deliberately offers none, so it always falls back to the
        # platform's own general TEXTURE ceiling inside _bake_material_map.
        key = _bake_material_map(albedo_path, f"{base_name}_ALBEDO", texture_policy, dst_dir, max_bytes=max_bytes)
        texture_refs[materiallib.PBR_TEX_ALBEDO] = len(dep_names)
        dep_names.append(key)

    if material_policy.get("bake_normal", True) and normal_path:
        if not os.path.isfile(normal_path):
            print(f"  ERROR: material normal source not found: {normal_path}")
            return None
        key = _bake_material_map(normal_path, f"{base_name}_NORMAL", texture_policy, dst_dir, material_policy.get("normal_max_dimensions"), max_bytes)
        texture_refs[materiallib.PBR_TEX_NORMAL] = len(dep_names)
        dep_names.append(key)

    if material_policy.get("bake_orm", True) and (orm_path or occlusion_path or roughness_path or metallic_path):
        if orm_path and (occlusion_path or roughness_path or metallic_path):
            print(f"  ERROR: material names both 'orm' and separate occlusion/roughness/metallic sources in {json_path}")
            return None
        try:
            if orm_path:
                if not os.path.isfile(orm_path):
                    print(f"  ERROR: material orm source not found: {orm_path}")
                    return None
                orm_image = orm_path
            else:
                for p in (occlusion_path, roughness_path, metallic_path):
                    if p and not os.path.isfile(p):
                        print(f"  ERROR: material ORM source not found: {p}")
                        return None
                orm_image = _pack_orm_image(occlusion_path, roughness_path, metallic_path)
            key = _bake_material_map(orm_image, f"{base_name}_ORM", texture_policy, dst_dir, material_policy.get("orm_max_dimensions"), max_bytes)
        except ImportError:
            print(f"  ERROR: Pillow required to bake material maps; cannot pack {json_path}")
            return None
        texture_refs[materiallib.PBR_TEX_ORM] = len(dep_names)
        dep_names.append(key)

    base_color = meta.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
    emissive = meta.get("emissiveFactor", [0.0, 0.0, 0.0])
    float_params = [0.0] * materiallib.FLOAT_PARAMS
    float_params[materiallib.PBR_FLOAT_METALLIC] = float(meta.get("metallicFactor", 1.0))
    float_params[materiallib.PBR_FLOAT_ROUGHNESS] = float(meta.get("roughnessFactor", 1.0))
    float_params[materiallib.PBR_FLOAT_NORMAL_SCALE] = float(meta.get("normalScale", 1.0))
    float_params[materiallib.PBR_FLOAT_ALPHA_CUTOFF] = float(meta.get("alphaCutoff", 0.5))
    color_params = [(0.0, 0.0, 0.0, 0.0)] * materiallib.COLOR_PARAMS
    color_params[materiallib.PBR_COLOR_BASE] = tuple(float(c) for c in base_color)
    color_params[materiallib.PBR_COLOR_EMISSIVE] = (float(emissive[0]), float(emissive[1]), float(emissive[2]), 0.0)

    alpha_mode = str(meta.get("alphaMode", "opaque")).lower()
    flags = 0
    if alpha_mode == "mask":
        flags |= materiallib.FLAG_ALPHA_MASK
    elif alpha_mode == "blend":
        flags |= materiallib.FLAG_ALPHA_BLEND
    if meta.get("doubleSided", False):
        flags |= materiallib.FLAG_DOUBLE_SIDED

    payload = materiallib.pack_material(materiallib.SHADER_PBR_STANDARD, flags, float_params, color_params, texture_refs)
    return payload, ".mtl2", dep_names


def pack_asset(json_path, src_dir, dst_dir, cooklist=None):
    with open(json_path, "r", encoding="utf-8-sig") as f:
        meta = json.load(f)

    asset_type_str = meta.get("type", "").upper()
    source_name = meta.get("source", "")
    deps = meta.get("deps", [])

    if asset_type_str not in ps2a.TYPE_MAP:
        print(f"  ERROR: unknown type '{asset_type_str}' in {json_path}")
        return False

    policy = (cooklist or DEFAULT_COOKLIST).get("assets", {}).get(asset_type_str, {})
    if not policy.get("enabled", False):
        print(f"  SKIP:  {asset_type_str} is not cooked on this platform: {json_path}")
        return True

    # A material has no single 'source': its maps are named fields instead,
    # each resolved and baked as its own standalone texture dependency below.
    if asset_type_str != "MATERIAL":
        if not source_name:
            print(f"  ERROR: missing 'source' in {json_path}")
            return False
        source_path = os.path.join(src_dir, source_name)
        if not os.path.isfile(source_path):
            print(f"  SKIP:  source file not found: {source_path}")
            return True

    if len(deps) > ps2a.MAX_DEPS:
        print(f"  WARNING: truncating deps to {ps2a.MAX_DEPS} for {json_path}")
        deps = deps[:ps2a.MAX_DEPS]

    # --- Build payload ---
    if asset_type_str == "TEXTURE":
        tex_fmt = str(meta.get("format", "rgba32")).lower()
        mip_levels = int(meta.get("mipmaps", 0))
        tex_filter = meta.get("filter")

        # The cook list overrides the authored format unless it defers to it.
        # This is what lets one source tree cook palettised for the console and
        # directly-uploadable for the desktop.
        want = str(policy.get("format", "source")).lower()
        if want != "source":
            tex_fmt = want

        if tex_fmt not in ("rgba32", "pal8", "coverage8"):
            print(f"  WARNING: unknown texture format '{tex_fmt}', using rgba32 for {source_name}")
            tex_fmt = "rgba32"
        try:
            payload, ext_str = tim2.convert_texture_to_tim2(source_path, tex_fmt, mip_levels, tex_filter)
            print(f"  CONV:  {source_name} -> TIM2 {tex_fmt} mips={mip_levels} ({len(payload)} bytes)")
        except ImportError:
            print(f"  ERROR: Pillow required to bake TIM2 textures; cannot pack {source_name}")
            return False
        except ValueError as e:
            print(f"  ERROR: {source_name}: {e}")
            return False
    elif asset_type_str == "MODEL":
        if not source_name.lower().endswith(".obj"):
            print(f"  ERROR: MODEL baking supports .obj only (got '{source_name}')")
            return False
        try:
            payload, ext_str = mesh.bake_obj_model(source_path)
            print(f"  BAKE:  {source_name} -> BKM2 ({len(payload)} bytes)")
        except Exception as e:  # noqa: BLE001 — surface any parse failure
            print(f"  ERROR: model bake failed for {source_name}: {e}")
            return False
    elif asset_type_str == "FONT":
        # The source is the metrics table; the atlas is an ordinary texture
        # asset named as this one's dependency.
        if not deps:
            print(f"  ERROR: FONT must name its atlas as a dependency: {json_path}")
            return False
        try:
            with open(source_path, "r", encoding="utf-8-sig") as fh:
                metrics = json.load(fh)
            cells = metrics.get("cells") or []
            payload, ext_str = font.write_font(
                metrics["glyphs"],
                metrics["atlas_width"],
                metrics["atlas_height"],
                metrics["line_height"],
                metrics["baseline"],
                metrics["space_advance"],
                metrics.get("missing_index", 0),
                cells,
            )
            print(f"  BAKE:  {source_name} -> PSFN {len(metrics['glyphs'])} glyphs, {len(cells)} cell(s) ({len(payload)} bytes)")
        except (KeyError, font.FontError) as e:
            print(f"  ERROR: font bake failed for {source_name}: {e}")
            return False
    elif asset_type_str == "MATERIAL":
        result = _bake_material_payload(meta, json_path, src_dir, dst_dir, cooklist)
        if result is None:
            return False
        payload, ext_str, deps = result
    else:
        return False

    # --- Build header + write ---
    type_id = ps2a.TYPE_MAP[asset_type_str]
    dep_keys = [f"RASSETS/{d.upper()}.PS2A" for d in deps]
    blob = ps2a.write_ps2a(type_id, payload, dep_keys, ext_str)

    base_name = os.path.splitext(os.path.basename(json_path))[0]
    out_path = os.path.join(dst_dir, f"{base_name}.PS2A")
    with open(out_path, "wb") as fh:
        fh.write(blob)

    print(f"  OK:   {base_name}.PS2A ({len(payload)} bytes payload, {len(dep_keys)} deps)")
    return True


def cook_themes(themes_path, dst_dir, cooklist):
    """Cook every declared theme into a loadable asset.

    The same declaration also generates the built-in table, so a game switching
    themes at run time and a game using a compiled-in one cannot disagree."""
    policy = (cooklist or DEFAULT_COOKLIST).get("assets", {}).get("THEME", {})
    if not policy.get("enabled", False):
        print("  SKIP:  THEME is not cooked on this platform")
        return 0
    if not os.path.isfile(themes_path):
        print(f"  SKIP:  no theme declaration at {themes_path}")
        return 0

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import theme as theme_tool

    try:
        decl = theme_tool.load(themes_path)
        payloads = theme_tool.cook_payloads(decl)
    except (ValueError, OSError) as e:
        print(f"  ERROR: theme declaration unusable: {e}")
        return 1

    for name, blob in sorted(payloads.items()):
        out = ps2a.write_ps2a(ps2a.TYPE_MAP["THEME"], blob, [], ".thm")
        with open(os.path.join(dst_dir, f"{name}.PS2A"), "wb") as fh:
            fh.write(out)
        print(f"  OK:   {name}.PS2A ({len(blob)} bytes payload, 0 deps)")
    return 0


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    src_dirs = []
    themes_path = None
    dst_dir = None
    cooklist_path = None
    platform = None

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--src" and i + 1 < len(args):
            src_dirs.append(args[i + 1]); i += 2
        elif args[i] == "--dst" and i + 1 < len(args):
            dst_dir = args[i + 1]; i += 2
        elif args[i] == "--themes" and i + 1 < len(args):
            themes_path = args[i + 1]; i += 2
        elif args[i] == "--platform" and i + 1 < len(args):
            platform = args[i + 1]; i += 2
        elif args[i] == "--cooklist" and i + 1 < len(args):
            cooklist_path = args[i + 1]; i += 2
        else:
            i += 1

    # Cooked output is keyed by platform: the encodings differ, so one shared
    # directory would mean each platform overwriting the other's work.
    if dst_dir is None:
        if not platform:
            print("cook_assets: --platform or --dst is required")
            return 1
        dst_dir = os.path.join(project_root, "dist", "cooked", platform.lower(), "rassets")

    if cooklist_path is None:
        cooklist_path = cooklist_for_platform(project_root, platform)

    cooklist = load_cooklist(cooklist_path)
    listed = cooklist.get("platform", "<default>")

    if not src_dirs:
        src_dirs = [
            os.path.join(project_root, "assets", "textures"),
            os.path.join(project_root, "assets", "models"),
            os.path.join(project_root, "assets", "materials"),
        ]

    work = []
    for d in src_dirs:
        if not os.path.isdir(d):
            print("Source directory not found: " + d)
            continue
        for dirpath, _dirnames, filenames in sorted(os.walk(d)):
            for jf in sorted(f for f in filenames if f.lower().endswith(".json")):
                work.append((dirpath, jf))

    if not work:
        print("No .json asset descriptors found in " + ", ".join(src_dirs))
        return 0

    os.makedirs(dst_dir, exist_ok=True)
    print(f"Cooking {len(work)} asset(s) for '{platform or listed}' [cook list: {listed}]")
    for d in src_dirs:
        print(f"  {d} -> {dst_dir}")
    errors = 0
    for d, jf in work:
        if pack_asset(os.path.join(d, jf), d, dst_dir, cooklist) is False:
            errors += 1

    if themes_path:
        errors += cook_themes(themes_path, dst_dir, cooklist)

    print("")
    print(f"Finished ({errors} error(s)).")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
