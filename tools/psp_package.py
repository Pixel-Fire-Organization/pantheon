#!/usr/bin/env python3
"""PSP package config reader, validator and emitter.

Reads game/config/platform/psp/package.json - what is specific to this
platform's containers - folds in the shared title identity, and emits the
pieces the build needs:

  * the argument lists the CMake fragment feeds to the packaging tools
  * the positional container slot list
  * the disc identity file the disc image carries

Validation is fail-loud for the same reason the Vita reader is: the system menu
refuses a malformed container without naming what is wrong with it, so a wrong
slot or a missing image must fail here, with the file name, or it costs someone
an afternoon.

jsonschema (draft-07) is used as an extra structural pass when importable; the
hand-rolled checks below are the source of truth and need no third-party package.

Usage:
    python3 tools/psp_package.py --validate
    python3 tools/psp_package.py --emit-cmake build/psp.cmake
    python3 tools/psp_package.py --emit-umd-data build/iso/UMD_DATA.BIN
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import title as title_declaration

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CONFIG = os.path.join(PROJECT_ROOT, "game", "config", "platform", "psp", "package.json")
DEFAULT_SCHEMA = os.path.join(PROJECT_ROOT, "tools", "schemas", "package.schema.json")
DEFAULT_TITLE = os.path.join(PROJECT_ROOT, "game", "config", "title.json")

# The container is a fixed sequence. An absent slot is filled with the
# placeholder the packer expects, never omitted: omitting one shifts every
# later slot, which the system menu shows as the wrong image rather than as an
# error.
EBOOT_SLOTS = ("icon", "icon_anim", "overlay", "picture", "sound")
SLOT_PLACEHOLDER = "NULL"

# A 144x80 icon and a 480x272 background are what the menu draws. The sizes are
# checked because a wrong one is accepted by the packer and then rendered
# stretched, which reads as an art problem rather than a packaging one.
IMAGE_SIZES = {
    "icon": (144, 80),
    "overlay": (480, 272),
    "picture": (480, 272),
}


class PspPackageError(Exception):
    """A package declaration this build refuses to package from."""


def _png_size(path):
    """Read a PNG's dimensions without a decoder.

    @param path File to inspect.
    @return (width, height), or None when the file is not a PNG.
    """
    with open(path, "rb") as handle:
        header = handle.read(24)
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return (int.from_bytes(header[16:20], "big"), int.from_bytes(header[20:24], "big"))


def load(config_path=DEFAULT_CONFIG, schema_path=DEFAULT_SCHEMA, title_path=DEFAULT_TITLE):
    """Read the declaration, fold in the shared title identity and validate.

    @param config_path The platform package declaration.
    @param schema_path The schema to validate it against.
    @param title_path The shared title declaration identity comes from.
    @return The merged configuration.
    """
    if not os.path.exists(config_path):
        raise PspPackageError("package config not found: {}".format(config_path))

    with open(config_path, encoding="utf-8") as handle:
        config = json.load(handle)

    if config.get("platform") != "psp":
        raise PspPackageError("package config declares platform '{}', expected 'psp'".format(config.get("platform")))

    declaration = title_declaration.load(title_path)
    title = dict(config.get("title") or {})
    title.setdefault("id", title_declaration.platform_id(declaration, "psp", title_path))
    title.setdefault("name", declaration["name"])
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
        raise PspPackageError("package config failed schema validation: {}".format(error.message))


def _validate(config, config_dir):
    title = config["title"]
    if not title.get("id"):
        raise PspPackageError("title id is missing; declare ids.psp in game/config/title.json")
    if len(title["id"]) != 9:
        raise PspPackageError("title id '{}' must be nine characters".format(title["id"]))

    version = title.get("version", "")
    if len(version) != 5 or version[2] != ".":
        raise PspPackageError("title version '{}' must read ##.##".format(version))

    eboot = config.get("eboot") or {}
    for slot, path in eboot.items():
        resolved = os.path.join(config_dir, path)
        if not os.path.exists(resolved):
            raise PspPackageError("eboot.{} points at a missing file: {}".format(slot, resolved))

        expected = IMAGE_SIZES.get(slot)
        if not expected:
            continue
        actual = _png_size(resolved)
        if actual is None:
            raise PspPackageError("eboot.{} is not a PNG: {}".format(slot, resolved))
        if actual != expected:
            raise PspPackageError(
                "eboot.{} is {}x{}, expected {}x{}: {}".format(slot, actual[0], actual[1], expected[0], expected[1], resolved)
            )

    for entry in config.get("files") or []:
        resolved = os.path.join(config_dir, entry["src"])
        if not os.path.exists(resolved):
            raise PspPackageError("files[] entry points at a missing file: {}".format(resolved))


def sfo_args(config):
    """Arguments for the metadata generator, in the order it expects them."""
    sfo = config.get("sfo") or {}
    args = ["-d", "MEMSIZE={}".format(int(sfo.get("extra", {}).get("MEMSIZE", 1)))]
    args += ["-s", "APP_VER={}".format(config["title"]["version"])]
    args += ["-s", "CATEGORY={}".format(sfo.get("category", "MG"))]

    for key, value in sorted((sfo.get("extra") or {}).items()):
        if key == "MEMSIZE":
            continue
        flag = "-d" if isinstance(value, int) else "-s"
        args += [flag, "{}={}".format(key, value)]
    return args


def pbp_slots(config, config_dir):
    """The container slots, positionally, with placeholders for absent ones."""
    eboot = config.get("eboot") or {}
    slots = []
    for name in EBOOT_SLOTS:
        path = eboot.get(name)
        slots.append(os.path.join(config_dir, path) if path else SLOT_PLACEHOLDER)
    return slots


def package_files(config, config_dir):
    """(source, destination) pairs staged into both containers."""
    return [(os.path.join(config_dir, entry["src"]), entry["dst"]) for entry in (config.get("files") or [])]


def emit_cmake(config, config_dir, out_path):
    """Emit set() lines the platform fragment includes.

    The root requires CMake 3.10, whose string(JSON) does not exist, so the
    config is read here and handed over as plain variables.
    """
    title = config["title"]
    lines = [
        "# Generated by tools/psp_package.py - do not edit.",
        'set(PSP_TITLE_ID "{}")'.format(title["id"]),
        'set(PSP_TITLE_NAME "{}")'.format(title["name"].replace('"', '\\"')),
        'set(PSP_TITLE_VERSION "{}")'.format(title["version"]),
        'set(PSP_SFO_ARGS "{}")'.format(";".join(sfo_args(config))),
        'set(PSP_PBP_SLOTS "{}")'.format(";".join(pbp_slots(config, config_dir))),
        'set(PSP_PACKAGE_FILES "{}")'.format(";".join("{}|{}".format(src, dst) for src, dst in package_files(config, config_dir))),
    ]
    _write(out_path, "\n".join(lines) + "\n")
    return out_path


def emit_umd_data(config, out_path):
    """Write the disc identity file the disc image carries.

    It is a fixed-width ASCII record: the title id, then four zeroed fields the
    reader expects to be present even though nothing here uses them.
    """
    title_id = config["title"]["id"]
    body = "{}|0000000000000000|0001|G".format(title_id)
    _write(out_path, body, binary=body.encode("ascii"))
    return out_path


def _write(path, text, binary=None):
    parent = os.path.dirname(os.path.abspath(path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    if binary is not None:
        with open(path, "wb") as handle:
            handle.write(binary)
    else:
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)


def main(argv=None):
    parser = argparse.ArgumentParser(description="PSP package reader and validator")
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    parser.add_argument("--schema", default=DEFAULT_SCHEMA)
    parser.add_argument("--title", default=DEFAULT_TITLE)
    parser.add_argument("--generated-dir")
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--emit-cmake")
    parser.add_argument("--emit-umd-data")
    args = parser.parse_args(argv)

    try:
        config = load(args.config, args.schema, args.title)
    except (PspPackageError, title_declaration.TitleError, json.JSONDecodeError) as error:
        sys.stderr.write("psp_package: {}\n".format(error))
        return 1

    config_dir = os.path.dirname(os.path.abspath(args.config))

    if args.emit_cmake:
        emit_cmake(config, config_dir, args.emit_cmake)
    if args.emit_umd_data:
        emit_umd_data(config, args.emit_umd_data)
    if args.validate and not (args.emit_cmake or args.emit_umd_data):
        print("psp_package: {} ({}) is valid".format(config["title"]["name"], config["title"]["id"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
