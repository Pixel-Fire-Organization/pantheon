#!/usr/bin/env python3
"""nx package config reader, validator and emitter.

Reads game/config/platform/nx/package.json - what is specific to the homebrew
executable container - folds in the shared title identity, and emits the
variables the CMake fragment builds the container from.

Validation is fail-loud: the container tool embeds whatever icon it is handed and
the launcher then draws it wrong or not at all, so a wrong icon must fail here,
with the file name, rather than on the console.

jsonschema (draft-07) is used as an extra structural pass when importable; the
hand-rolled checks below are the source of truth and need no third-party package.

Usage:
    python3 tools/nx_package.py --validate
    python3 tools/nx_package.py --title examples/primitives/config/title.json --emit-cmake build/nx.cmake
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import title as title_declaration

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CONFIG = os.path.join(PROJECT_ROOT, "game", "config", "platform", "nx", "package.json")
DEFAULT_SCHEMA = os.path.join(PROJECT_ROOT, "tools", "schemas", "package.schema.json")
DEFAULT_TITLE = os.path.join(PROJECT_ROOT, "game", "config", "title.json")

ICON_SIZE = (256, 256)

# Start-of-frame markers carry the image dimensions. The other markers in the
# C0-CF range (define Huffman table, JPEG extension, define arithmetic coding)
# share the range and carry none.
JPEG_SOF_MARKERS = set(range(0xC0, 0xD0)) - {0xC4, 0xC8, 0xCC}

# Characters the fragment cannot hand to a command line through a CMake
# variable without changing what the tool receives.
CMAKE_UNSAFE = set(";")


class NxPackageError(Exception):
    """A package declaration this build refuses to package from."""


def jpeg_size(path):
    """Read a JPEG's dimensions without a decoder.

    @param path File to inspect.
    @return (width, height), or None when the file is not a JPEG.
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:2] != b"\xFF\xD8":
        return None

    offset = 2
    while offset + 4 <= len(data):
        if data[offset] != 0xFF:
            return None
        marker = data[offset + 1]
        if marker == 0xFF:
            offset += 1
            continue
        length = int.from_bytes(data[offset + 2:offset + 4], "big")
        if marker in JPEG_SOF_MARKERS:
            if offset + 9 > len(data):
                return None
            height = int.from_bytes(data[offset + 5:offset + 7], "big")
            width = int.from_bytes(data[offset + 7:offset + 9], "big")
            return (width, height)
        if length < 2:
            return None
        offset += 2 + length
    return None


def load(config_path=DEFAULT_CONFIG, schema_path=DEFAULT_SCHEMA, title_path=DEFAULT_TITLE):
    """Read the declaration, fold in the shared title identity and validate.

    @param config_path The platform package declaration.
    @param schema_path The schema to validate it against.
    @param title_path The title declaration identity comes from: the game's, or an example's.
    @return The merged configuration.
    """
    if not os.path.exists(config_path):
        raise NxPackageError("package config not found: {}".format(config_path))

    with open(config_path, encoding="utf-8") as handle:
        config = json.load(handle)

    if config.get("platform") != "nx":
        raise NxPackageError("package config declares platform '{}', expected 'nx'".format(config.get("platform")))

    declaration = title_declaration.load(title_path)
    title = dict(config.get("title") or {})
    title.setdefault("name", declaration["name"])
    title.setdefault("author", declaration["developer"])
    title.setdefault("version", declaration["version"])
    config["title"] = title

    _validate_schema(config, schema_path)
    _validate(config, os.path.dirname(os.path.abspath(config_path)))
    return config


def _validate_schema(config, schema_path):
    try:
        import jsonschema
    except ImportError:
        return
    if not os.path.exists(schema_path):
        return
    with open(schema_path, encoding="utf-8") as handle:
        schema = json.load(handle)
    try:
        jsonschema.validate(config, schema)
    except jsonschema.ValidationError as error:
        raise NxPackageError("package config failed schema validation: {}".format(error.message))


def _validate(config, config_dir):
    title = config["title"]
    for field in ("name", "author", "version"):
        value = title.get(field) or ""
        if not value:
            raise NxPackageError("title.{} is missing".format(field))
        if CMAKE_UNSAFE & set(value):
            raise NxPackageError("title.{} '{}' contains a character the build cannot pass through: {}".format(field, value, "".join(sorted(CMAKE_UNSAFE))))

    icon = (config.get("nro") or {}).get("icon")
    if not icon:
        raise NxPackageError("nro.icon is required")
    resolved = os.path.join(config_dir, icon)
    if not os.path.exists(resolved):
        raise NxPackageError("nro.icon points at a missing file: {}".format(resolved))
    size = jpeg_size(resolved)
    if size is None:
        raise NxPackageError("nro.icon is not a JPEG: {}".format(resolved))
    if size != ICON_SIZE:
        raise NxPackageError("nro.icon is {}x{}, expected {}x{}: {}".format(size[0], size[1], ICON_SIZE[0], ICON_SIZE[1], resolved))

    for entry in config.get("files") or []:
        source = os.path.join(config_dir, entry["src"])
        if not os.path.exists(source):
            raise NxPackageError("files[] entry points at a missing file: {}".format(source))


def package_files(config, config_dir):
    """(source, destination) pairs staged into the container's file system."""
    return [(os.path.join(config_dir, entry["src"]), entry["dst"]) for entry in (config.get("files") or [])]


def _cmake_string(value):
    return value.replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$")


def emit_cmake(config, config_dir, out_path):
    """Emit set() lines the platform fragment includes."""
    title = config["title"]
    icon = os.path.abspath(os.path.join(config_dir, config["nro"]["icon"])).replace("\\", "/")
    files = ";".join("{}|{}".format(src.replace("\\", "/"), dst) for src, dst in package_files(config, config_dir))
    lines = [
        "# Generated by tools/nx_package.py - do not edit.",
        'set(ENGINE_NX_TITLE_NAME "{}")'.format(_cmake_string(title["name"])),
        'set(ENGINE_NX_TITLE_AUTHOR "{}")'.format(_cmake_string(title["author"])),
        'set(ENGINE_NX_TITLE_VERSION "{}")'.format(_cmake_string(title["version"])),
        'set(ENGINE_NX_ICON "{}")'.format(_cmake_string(icon)),
        'set(ENGINE_NX_PACKAGE_FILES "{}")'.format(_cmake_string(files)),
    ]
    parent = os.path.dirname(os.path.abspath(out_path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    return out_path


def main(argv=None):
    parser = argparse.ArgumentParser(description="nx package reader and validator")
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    parser.add_argument("--schema", default=DEFAULT_SCHEMA)
    parser.add_argument("--title", default=DEFAULT_TITLE)
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--emit-cmake")
    args = parser.parse_args(argv)

    try:
        config = load(args.config, args.schema, args.title)
    except (NxPackageError, title_declaration.TitleError, json.JSONDecodeError) as error:
        sys.stderr.write("nx_package: {}\n".format(error))
        return 1

    if args.emit_cmake:
        emit_cmake(config, os.path.dirname(os.path.abspath(args.config)), args.emit_cmake)
    elif args.validate:
        print("nx_package: {} by {} ({}) is valid".format(config["title"]["name"], config["title"]["author"], config["title"]["version"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
