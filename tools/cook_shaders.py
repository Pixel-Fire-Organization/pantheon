#!/usr/bin/env python3
"""
cook_shaders.py - the shader class of stage 3 (cook).

Shaders are not archive entries. The renderer is built before the IO, archive and
resource subsystems exist, and those subsystems are selectable while a renderer is
not, so a renderer reads its shaders as loose files next to the archive through
the platform file API. This tool turns the shader sources under assets/engine/shaders
into that directory: it checks each file against the portability baseline, writes it
out, and refuses a platform whose cook list asks for a dialect with a source missing.
See docs/formats/SHADER_ASSETS.md.

Usage:
    python3 tools/cook_shaders.py --platform macos
    python3 tools/cook_shaders.py --platform win32 --src assets/engine/shaders --dst dist/cooked/win32/shaders

A platform whose cook list has no SHADER policy cooks nothing and succeeds.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cook_assets

# Mirrors SHADER_SOURCE_MAX_BYTES in engine/include/graphics/ShaderAssets.h. The reader
# refuses a larger file, so the cook refuses to produce one.
SHADER_SOURCE_MAX_BYTES = 256 * 1024

DIALECTS = ("wgsl", "glsl", "glsl_legacy")

# The shaders each dialect must supply, by the names the renderers load. A dialect
# in the cook list with any of these missing from the source tree is a build error.
SHADER_SET = {
    "wgsl": ("flat.wgsl", "pbr.wgsl", "shadow.wgsl"),
    "glsl": (
        "flat.vert.glsl",
        "flat.frag.glsl",
        "pbr.vert.glsl",
        "pbr.frag.glsl",
        "shadow.vert.glsl",
        "shadow.frag.glsl",
    ),
    "glsl_legacy": (
        "legacy/flat.vert.glsl",
        "legacy/flat.frag.glsl",
        "legacy/pbr.vert.glsl",
        "legacy/pbr.frag.glsl",
        "legacy/shadow.vert.glsl",
        "legacy/shadow.frag.glsl",
    ),
}

SOURCE_EXTENSIONS = (".wgsl", ".glsl")

_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
_LINE_COMMENT = re.compile(r"//[^\n]*")
_WGSL_DIRECTIVE = re.compile(r"^\s*(enable|requires|diagnostic)\b", re.M)
_WGSL_ENTRY = re.compile(r"@(vertex|fragment)\b")
_GLSL_VERSION = re.compile(r"^\s*#\s*version\b", re.M)
_GLSL_EXTENSION = re.compile(r"^\s*#\s*extension\b", re.M)
_GLSL_MAIN = re.compile(r"\bvoid\s+main\s*\(")


class ShaderError(Exception):
    """A shader source this build refuses to ship, naming the file and the rule."""


def dialect_of(relpath):
    """@return The dialect a source belongs to, or None when it is not a shader source."""
    normal = relpath.replace("\\", "/")
    if normal.endswith(".wgsl"):
        return "wgsl"
    if normal.endswith(".glsl"):
        return "glsl_legacy" if normal.startswith("legacy/") else "glsl"
    return None


def policy_dialects(cooklist):
    """@return The dialects this platform ships, empty when the class is off."""
    policy = cooklist.get("assets", {}).get("SHADER")
    if not policy or not policy.get("enabled", False):
        return ()
    listed = policy.get("dialects", DIALECTS)
    return tuple(d for d in DIALECTS if d in listed)


def _strip_comments(text):
    return _LINE_COMMENT.sub("", _BLOCK_COMMENT.sub("", text))


def lint(relpath, data):
    """Check one source against the rules of its dialect.

    @param relpath Path relative to the shader root, forward slashes.
    @param data The file's bytes, line endings already normalised.
    @return The text, validated.
    @raise ShaderError Naming the file and the rule it breaks.
    """
    dialect = dialect_of(relpath)
    if dialect is None:
        raise ShaderError("{}: not a shader source (.wgsl or .glsl)".format(relpath))
    if not data:
        raise ShaderError("{}: empty".format(relpath))
    if len(data) > SHADER_SOURCE_MAX_BYTES:
        raise ShaderError("{}: {} bytes, past the {}-byte ceiling".format(relpath, len(data), SHADER_SOURCE_MAX_BYTES))
    if data.startswith(b"\xef\xbb\xbf"):
        raise ShaderError("{}: starts with a byte-order mark".format(relpath))
    if b"\x00" in data:
        raise ShaderError("{}: contains a NUL byte, which would end the source early".format(relpath))
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ShaderError("{}: not UTF-8 ({})".format(relpath, error))

    if dialect != "wgsl" and any(ord(c) > 127 for c in text):
        raise ShaderError("{}: GLSL must be ASCII; some drivers reject any other character, comments included".format(relpath))

    code = _strip_comments(text)
    if dialect == "wgsl":
        found = _WGSL_DIRECTIVE.search(code)
        if found:
            raise ShaderError(
                "{}: '{}' is outside the WebGPU 1.0 core baseline; the same source must translate for Vulkan, DX12 and Metal".format(
                    relpath, found.group(1)
                )
            )
        if not _WGSL_ENTRY.search(code):
            raise ShaderError("{}: declares no @vertex or @fragment entry point".format(relpath))
    else:
        if _GLSL_VERSION.search(code):
            raise ShaderError("{}: carries its own #version; the renderer prefixes the one it needs".format(relpath))
        if _GLSL_EXTENSION.search(code):
            raise ShaderError("{}: uses #extension, which is outside the portable core baseline".format(relpath))
        if not _GLSL_MAIN.search(code):
            raise ShaderError("{}: declares no main()".format(relpath))
    return text


def normalise(data):
    """Line endings to LF, so a file edited on Windows cooks to the same bytes."""
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def collect(src_dir):
    """@return Every shader source under src_dir as {relative path: bytes}."""
    found = {}
    for dirpath, _dirnames, filenames in sorted(os.walk(src_dir)):
        for name in sorted(filenames):
            if not name.lower().endswith(SOURCE_EXTENSIONS):
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, src_dir).replace(os.sep, "/")
            with open(full, "rb") as handle:
                found[rel] = handle.read()
    return found


def cook(src_dir, dst_dir, dialects):
    """Write the shaders `dialects` ask for into dst_dir.

    @return (written relative paths, error messages).
    """
    errors = []
    sources = collect(src_dir)

    wanted = []
    for dialect in dialects:
        for rel in SHADER_SET[dialect]:
            if rel not in sources:
                errors.append("{}: required by dialect '{}' but absent from {}".format(rel, dialect, src_dir))
            else:
                wanted.append(rel)

    staged = []
    for rel in wanted:
        data = normalise(sources[rel])
        try:
            lint(rel, data)
        except ShaderError as error:
            errors.append(str(error))
            continue
        staged.append((rel, data))

    if errors:
        return [], errors

    for rel, data in staged:
        out = os.path.join(dst_dir, *rel.split("/"))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as handle:
            handle.write(data)

    keep = set(rel for rel, _data in staged)
    for rel in collect(dst_dir):
        if rel not in keep:
            os.remove(os.path.join(dst_dir, *rel.split("/")))
    return [rel for rel, _data in staged], errors


def main(argv=None):
    parser = argparse.ArgumentParser(description="Cook shader sources into the loose directory a renderer reads")
    parser.add_argument("--platform", help="platform whose cook list decides which dialects ship")
    parser.add_argument("--cooklist", help="cook list file (overrides --platform)")
    parser.add_argument("--src", help="shader source directory (default assets/engine/shaders)")
    parser.add_argument("--dst", help="output directory (default dist/cooked/<platform>/shaders)")
    args = parser.parse_args(argv)

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = args.src or os.path.join(root, "assets", "engine", "shaders")

    dst = args.dst
    if dst is None:
        if not args.platform:
            print("cook_shaders: --platform or --dst is required")
            return 2
        dst = os.path.join(root, "dist", "cooked", args.platform.lower(), "shaders")

    cooklist = cook_assets.load_cooklist(args.cooklist or cook_assets.cooklist_for_platform(root, args.platform))
    label = cooklist.get("platform", "<default>")

    dialects = policy_dialects(cooklist)
    if not dialects:
        print("cook_shaders: SHADER is not cooked on this platform [cook list: {}]".format(label))
        return 0

    if not os.path.isdir(src):
        print("cook_shaders: source directory not found: {}".format(src))
        return 1

    written, errors = cook(src, dst, dialects)
    print("cook_shaders: {} shader(s) -> {} [cook list: {}, dialects: {}]".format(len(written), dst, label, ", ".join(dialects)))
    for rel in written:
        print("  OK    {}".format(rel))
    for message in errors:
        print("  FAIL  {}".format(message))
    if errors:
        print("\n{} problem(s) - no shaders cooked.".format(len(errors)))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
