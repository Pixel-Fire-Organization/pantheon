#!/usr/bin/env python3
"""Vita package config reader, validator and emitter.

Reads game/config/platform/vita/package.json - the title's identity,
store-front presentation and achievements - and emits the pieces the build
needs:

  * the argument lists the CMake fragment feeds to the packaging tools
  * a store-front layout template, when the config does not supply one
  * the trophy configuration files and the trophy pack
  * a C++ header of trophy identifiers, so no trophy number is written by hand

Validation is fail-loud, for one concrete reason: every mistake this catches is
otherwise silent until the console refuses to install the package, and the error
it gives then names nothing. A wrong icon size must fail here, with the file name
and the expected size, or it costs someone an afternoon.

jsonschema (draft-07) is used as an extra structural pass when importable; the
hand-rolled checks below are the source of truth and need no third-party package.

Usage:
    python3 tools/vita_package.py --validate --variant vita
    python3 tools/vita_package.py --variant vitatv --emit-cmake build/vitatv.cmake
    python3 tools/vita_package.py --emit-ids build/generated/TrophyIds.h
"""

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import title as title_declaration

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CONFIG = os.path.join(PROJECT_ROOT, "game", "config", "platform", "vita", "package.json")
DEFAULT_SCHEMA = os.path.join(PROJECT_ROOT, "tools", "schemas", "package.schema.json")
DEFAULT_TITLE = os.path.join(PROJECT_ROOT, "game", "config", "title.json")

ACHV_MAX_ENTRIES = 128

PNG_COLOUR_INDEXED = 3

LIVEAREA_ASSETS = {
    "icon": (128, 128, True, False),
    "background": (840, 500, True, False),
    "startup": (280, 158, True, True),
    "picture": (960, 544, False, False),
}

TROPHY_GRADES = ("platinum", "gold", "silver", "bronze")


class PackageError(Exception):
    """A configuration error with an actionable message."""

    def __init__(self, where, message):
        super().__init__(f"{where}: {message}")
        self.where = where
        self.message = message


def load_config(path, title_path=None):
    """Read the package config and fold in the title's identity.

    @param path The platform package config.
    @param title_path The shared title declaration; defaults to the game's.
    @return The config, with a 'title' block taken from the shared declaration.
    """
    if not os.path.exists(path):
        raise PackageError(os.path.basename(path), f"no package config at {path}")
    with open(path, "r", encoding="utf-8") as fh:
        try:
            config = json.load(fh)
        except json.JSONDecodeError as exc:
            raise PackageError(os.path.basename(path), f"invalid JSON: {exc}") from exc

    declaration_path = title_path or DEFAULT_TITLE
    try:
        declaration = title_declaration.load(declaration_path)
        config["title"] = {
            "id": title_declaration.platform_id(declaration, "vita", declaration_path),
            "name": declaration["name"],
            "version": declaration["version"],
        }
    except title_declaration.TitleError as exc:
        raise PackageError(os.path.basename(declaration_path), str(exc)) from exc

    return config


def deep_merge(base, override):
    """Recursive merge. Dicts merge key-wise; lists and scalars replace outright.

    Replacing rather than appending is deliberate: a variant that overrides the
    trophy list means to supply a different list, not to extend the base one.
    """
    out = dict(base)
    for key, value in override.items():
        if key in out and isinstance(out[key], dict) and isinstance(value, dict):
            out[key] = deep_merge(out[key], value)
        else:
            out[key] = value
    return out


def resolve_variant(config, variant):
    """Apply the variant override block, and drop 'variants' from the result."""
    resolved = dict(config)
    variants = resolved.pop("variants", {})
    if variant and variant in variants:
        resolved = deep_merge(resolved, variants[variant])
        resolved.pop("variants", None)
    return resolved


def schema_validate(data, schema_path):
    """Optional draft-07 pass; silently skipped if jsonschema is unavailable."""
    try:
        import jsonschema  # type: ignore
    except ImportError:
        return
    if not os.path.exists(schema_path):
        return
    with open(schema_path, "r", encoding="utf-8") as fh:
        schema = json.load(fh)
    validator = jsonschema.Draft7Validator(schema)
    errors = sorted(validator.iter_errors(data), key=lambda e: list(e.path))
    if errors:
        parts = []
        for err in errors:
            loc = "/".join(str(p) for p in err.path) or "<root>"
            parts.append(f"  {loc}: {err.message}")
        raise PackageError("schema", "draft-07 validation failed:\n" + "\n".join(parts))


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_info(path):
    """Return (width, height, bit_depth, colour_type, has_transparency)."""
    with open(path, "rb") as fh:
        if fh.read(8) != PNG_SIGNATURE:
            raise PackageError(os.path.basename(path), "not a PNG file")
        length = struct.unpack(">I", fh.read(4))[0]
        if fh.read(4) != b"IHDR" or length != 13:
            raise PackageError(os.path.basename(path), "malformed PNG: no IHDR")
        width, height, depth, colour = struct.unpack(">IIBB", fh.read(10))
        fh.read(3 + 4)  # rest of IHDR, then its CRC

        has_alpha = colour in (4, 6)
        while not has_alpha:
            head = fh.read(8)
            if len(head) < 8:
                break
            clen, ctype = struct.unpack(">I4s", head)
            if ctype == b"tRNS":
                has_alpha = True
                break
            if ctype == b"IDAT":
                break  # tRNS must precede IDAT, so there is none
            fh.seek(clen + 4, os.SEEK_CUR)
    return width, height, depth, colour, has_alpha


def _resolve(config_dir, rel):
    return rel if os.path.isabs(rel) else os.path.normpath(os.path.join(config_dir, rel))


def validate(config, config_dir, variant=None):
    """Raise PackageError on the first structural problem; return the config."""
    where = "package.json" + (f" [{variant}]" if variant else "")

    title = config.get("title") or {}
    tid = title.get("id", "")
    if len(tid) != 9 or not tid[:4].isalpha() or not tid[:4].isupper() or not tid[4:].isdigit():
        raise PackageError(where, f"title.id '{tid}' must be four upper-case letters then five digits")

    version = title.get("version", "")
    if len(version) != 5 or version[2] != "." or not (version[:2] + version[3:]).isdigit():
        raise PackageError(where, f"title.version '{version}' must be ##.##")

    if not title.get("name"):
        raise PackageError(where, "title.name is required and must not be empty")

    _validate_livearea(config, config_dir, where)
    _validate_trophies(config, config_dir, where)
    _validate_files(config, config_dir, where)
    return config


def _validate_livearea(config, config_dir, where):
    livearea = config.get("livearea")
    if not livearea:
        return

    for key, (want_w, want_h, required, alpha_ok) in LIVEAREA_ASSETS.items():
        rel = livearea.get(key)
        if not rel:
            if required:
                raise PackageError(where, f"livearea.{key} is required when a store-front is declared")
            continue

        path = _resolve(config_dir, rel)
        if not os.path.exists(path):
            raise PackageError(where, f"livearea.{key} points at a missing file: {rel}")

        width, height, depth, colour, has_alpha = png_info(path)
        if (width, height) != (want_w, want_h):
            raise PackageError(
                where,
                f"livearea.{key} ({rel}) is {width}x{height}, must be exactly {want_w}x{want_h}. "
                "The console rejects the whole package on a wrong size and names nothing.")
        if colour != PNG_COLOUR_INDEXED:
            raise PackageError(
                where,
                f"livearea.{key} ({rel}) has PNG colour type {colour}, must be {PNG_COLOUR_INDEXED} (indexed). "
                "Convert it to an indexed palette; a plain RGB image installs as an error code.")
        if depth > 8:
            raise PackageError(where, f"livearea.{key} ({rel}) is {depth}-bit, must be 8-bit or less")
        if has_alpha and not alpha_ok:
            raise PackageError(where, f"livearea.{key} ({rel}) has transparency, which is not allowed for this layer")

    template = livearea.get("template")
    if template and not os.path.exists(_resolve(config_dir, template)):
        raise PackageError(where, f"livearea.template points at a missing file: {template}")


def _validate_trophies(config, config_dir, where):
    trophies = config.get("trophies")
    if not trophies:
        return

    has_trp = bool(trophies.get("trp"))
    entries = trophies.get("list") or []
    enabled = bool(trophies.get("enabled"))

    if has_trp and entries:
        raise PackageError(where, "trophies.trp and trophies.list are mutually exclusive - supply a pre-built pack or definitions, not both")

    if enabled:
        comm = trophies.get("np_communication_id", "")
        if not (len(comm) == 12 and comm.startswith("NPWR") and comm[4:9].isdigit() and comm.endswith("_00")):
            raise PackageError(where, f"trophies.np_communication_id '{comm}' must look like NPWR#####_00")
        if not has_trp and not entries:
            raise PackageError(where, "trophies.enabled is set but neither trophies.trp nor trophies.list was given")

    if has_trp:
        path = _resolve(config_dir, trophies["trp"])
        if not os.path.exists(path):
            raise PackageError(where, f"trophies.trp points at a missing file: {trophies['trp']}")
        return

    if not entries:
        return

    if len(entries) > ACHV_MAX_ENTRIES:
        raise PackageError(where, f"{len(entries)} trophies declared, the maximum is {ACHV_MAX_ENTRIES}")

    ids = [e.get("id") for e in entries]
    if len(set(ids)) != len(ids):
        dupes = sorted({i for i in ids if ids.count(i) > 1})
        raise PackageError(where, f"duplicate trophy ids: {dupes}")
    if sorted(ids) != list(range(len(ids))):
        raise PackageError(
            where,
            f"trophy ids must be dense and start at zero; got {sorted(ids)}. "
            "A gap means an icon the pack does not carry, which nothing can recover from at runtime.")

    platinums = [e for e in entries if e.get("grade") == "platinum"]
    if len(platinums) > 1:
        raise PackageError(where, f"{len(platinums)} platinum trophies declared, at most one is allowed")

    for entry in entries:
        if entry.get("grade") not in TROPHY_GRADES:
            raise PackageError(where, f"trophy {entry.get('id')} has grade '{entry.get('grade')}', must be one of {list(TROPHY_GRADES)}")
        icon = entry.get("icon")
        if icon and not os.path.exists(_resolve(config_dir, icon)):
            raise PackageError(where, f"trophy {entry.get('id')} icon points at a missing file: {icon}")

    icon0 = trophies.get("icon")
    if icon0 and not os.path.exists(_resolve(config_dir, icon0)):
        raise PackageError(where, f"trophies.icon points at a missing file: {icon0}")


def _validate_files(config, config_dir, where):
    for item in config.get("files") or []:
        src = _resolve(config_dir, item["src"])
        if not os.path.exists(src):
            raise PackageError(where, f"files[] entry points at a missing file: {item['src']}")
        if item["dst"].startswith("/") or ".." in item["dst"].split("/"):
            raise PackageError(where, f"files[] destination must be a relative path inside the package: {item['dst']}")


def _sfo_args(config):
    """Metadata arguments, in a stable order so the output is reproducible."""
    title = config["title"]
    sfo = config.get("sfo") or {}
    args = [f"-s TITLE_ID={title['id']}", f"-s APP_VER={title['version']}"]
    if sfo.get("category"):
        args.append(f"-s CATEGORY={sfo['category']}")
    if "parental_level" in sfo:
        args.append(f"-d PARENTAL_LEVEL={sfo['parental_level']}")
    if (config.get("self") or {}).get("extended_memory"):
        args.append("-d ATTRIBUTE2=12")
    trophies = config.get("trophies") or {}
    if trophies.get("enabled") and trophies.get("np_communication_id"):
        args.append(f"-s NP_COMMUNICATION_ID={trophies['np_communication_id']}")
    for key in sorted((sfo.get("extra") or {}).keys()):
        value = sfo["extra"][key]
        args.append(f"-d {key}={value}" if isinstance(value, int) else f"-s {key}={value}")
    return args


def _package_files(config, config_dir, generated_dir):
    """(absolute source, destination inside the package) pairs, in stable order."""
    pairs = []
    livearea = config.get("livearea") or {}
    dests = {
        "icon": "sce_sys/icon0.png",
        "picture": "sce_sys/pic0.png",
        "background": "sce_sys/livearea/contents/bg0.png",
        "startup": "sce_sys/livearea/contents/startup.png",
    }
    for key in ("icon", "picture", "background", "startup"):
        if livearea.get(key):
            pairs.append((_resolve(config_dir, livearea[key]), dests[key]))

    if livearea:
        template = livearea.get("template")
        src = _resolve(config_dir, template) if template else os.path.join(generated_dir, "template.xml")
        pairs.append((src, "sce_sys/livearea/contents/template.xml"))

    trophies = config.get("trophies") or {}
    if trophies.get("enabled"):
        trp = trophies.get("trp")
        src = _resolve(config_dir, trp) if trp else os.path.join(generated_dir, "TROPHY.TRP")
        pairs.append((src, trophy_pack_path(trophies["np_communication_id"])))

    for item in config.get("files") or []:
        pairs.append((_resolve(config_dir, item["src"]), item["dst"]))
    return pairs


def emit_cmake(config, config_dir, generated_dir, out_path):
    """Emit set() lines the platform fragment includes.

    The root requires CMake 3.10, whose string(JSON) does not exist, so the config
    is read here and handed over as plain variables.
    """
    title = config["title"]
    self_cfg = config.get("self") or {}
    trophies = config.get("trophies") or {}

    fself = []
    if self_cfg.get("compress", True):
        fself.append("-c")
    if self_cfg.get("safe", True):
        fself.append("-s")
    if self_cfg.get("memsize"):
        fself.append(f"-m {self_cfg['memsize']}")

    lines = [
        "# Generated by tools/vita_package.py - do not edit.",
        f'set(VITA_TITLE_ID "{title["id"]}")',
        f'set(VITA_TITLE_NAME "{title["name"]}")',
        f'set(VITA_TITLE_VERSION "{title["version"]}")',
        'set(VITA_MKSFOEX_ARGS "{}")'.format(";".join(_sfo_args(config)).replace('"', '\\"')),
        'set(VITA_MAKE_FSELF_ARGS "{}")'.format(";".join(fself)),
        'set(VITA_TROPHIES_ENABLED "{}")'.format("ON" if trophies.get("enabled") else "OFF"),
        'set(VITA_NP_COMM_ID "{}")'.format(trophies.get("np_communication_id", "")),
        'set(VITA_TROPHY_GENERATE "{}")'.format("ON" if trophies.get("enabled") and not trophies.get("trp") else "OFF"),
        'set(VITA_TROPHY_COUNT "{}")'.format(len(trophies.get("list") or []) if trophies.get("enabled") else 0),
    ]

    pairs = _package_files(config, config_dir, generated_dir)
    flat = ";".join(f"{src}|{dst}" for src, dst in pairs)
    lines.append(f'set(VITA_PACKAGE_FILES "{flat}")')

    _write(out_path, "\n".join(lines) + "\n")
    return out_path


TEMPLATE_XML = """<?xml version="1.0" encoding="utf-8"?>
<!-- Generated by tools/vita_package.py from livearea.style - do not edit. -->
<livearea style="{style}" format-ver="01.00" content-rev="1">
  <livearea-background>
    <image>bg0.png</image>
  </livearea-background>
  <gate>
    <startup-image>startup.png</startup-image>
  </gate>
</livearea>
"""


def emit_template(config, out_path):
    style = (config.get("livearea") or {}).get("style", "a1")
    _write(out_path, TEMPLATE_XML.format(style=style))
    return out_path


def _xml_escape(text):
    return (str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace('"', "&quot;").replace("'", "&apos;"))


# Observed on a registered set: every trophy carries this when none of them
# contributes to a platinum. A set that has one links to it instead, and what
# that link looks like has not been seen, so it is not written.
TROPHY_NO_PLATINUM = -1
# A registered configuration carries a signature as a comment ahead of the root
# element, 320 hex characters. Nothing here can produce a real one, and a console
# that checks it refuses the pack either way; what its absence changes is that the
# file does not parse at all. A placeholder of the right shape is written so the
# structure is right and only the authenticity is missing.
TROPHY_SIGNATURE_DIGITS = 320


def trophy_pack_path(comm):
    """Where the container sits inside the package.

    A console looks for it under a directory named after the communication
    identifier, not directly under the trophy directory. A pack one level up is
    simply not found, and what the console then reports is that the set is not
    registered -- which reads as a bad pack rather than a misplaced one.
    """
    return "sce_sys/trophy/{}/TROPHY.TRP".format(comm)


TRPPARAM_INI = (
    "\ufeffTROPSYSVER=1.0\r\n"
    "NPCOMMID={comm}\r\n"
    "TROPAPPVER=1.0\r\n"
    "LANG=1\r\n"
)


def emit_trophy_conf(config, out_dir):
    """Write the trophy set definition, its localised strings and its parameters.

    The shape here is taken from a configuration read off a console that had
    registered it, rather than from a description of the format. Identifiers are
    three digits, the root element carries a version and the platforms the set is
    valid for, and every trophy names the platinum it contributes to.
    """
    trophies = config["trophies"]
    entries = sorted(trophies["list"], key=lambda e: e["id"])
    comm = trophies["np_communication_id"]
    parental = int((config.get("sfo") or {}).get("parental_level", 0))
    name = _xml_escape(config["title"]["name"])
    detail = _xml_escape(trophies.get("detail") or config["title"]["name"])

    signature = "0" * TROPHY_SIGNATURE_DIGITS

    def head():
        return [f'<!--Sce-Np-Trophy-Signature: {signature}-->',
                '<trophyconf version="1.1" platform="psp2">',
                f' <npcommid>{comm}</npcommid>',
                ' <trophyset-version>01.00</trophyset-version>',
                f' <parental-level license-area="default">{parental}</parental-level>',
                f' <title-name>{name}</title-name>',
                f' <title-detail>{detail}</title-detail>']

    conf = head()
    strings = head()
    platinum = next((e["id"] for e in entries if e["grade"] == "platinum"), None)

    for entry in entries:
        hidden = "yes" if entry.get("hidden") else "no"
        ttype = entry["grade"][0].upper()
        contributes = platinum is not None and entry["id"] != platinum
        pid = f"{platinum:03d}" if contributes else str(TROPHY_NO_PLATINUM)
        attrs = f'id="{entry["id"]:03d}" hidden="{hidden}" ttype="{ttype}" pid="{pid}"'
        conf.append(f' <trophy {attrs}/>')
        strings.append(f' <trophy {attrs}>')
        strings.append(f'  <name>{_xml_escape(entry["name"])}</name>')
        strings.append(f'  <detail>{_xml_escape(entry.get("detail", ""))}</detail>')
        strings.append(' </trophy>')

    conf.append("</trophyconf>")
    strings.append("</trophyconf>")

    os.makedirs(out_dir, exist_ok=True)
    _write(os.path.join(out_dir, "TROPCONF.SFM"), "\n".join(conf) + "\n")
    _write(os.path.join(out_dir, "TROP.SFM"), "\n".join(strings) + "\n")
    return out_dir


TRP_HEADER_SIZE = 0x40
TRP_ENTRY_SIZE = 0x40
TRP_NAME_SIZE = 0x24
# 2 is the PS3/Vita container; 3 is PS4. The Vita reader rejects 3 outright.
TRP_VERSION = 2
# Established by a reader that rejects anything else (TTEMMA/TRPWork) and by the
# PS3/PS4 developer wikis. See docs/formats/TROPHY_PACK.md.
TRP_MAGIC = 0xDCA24D00
# Left clear, as the reference builder for unencrypted packs leaves it. Setting
# it was an inference from the field's name and changed nothing on hardware.
TRP_DEV_FLAG = 0
# The entry field at 0x30 is a four-byte type tag, 1 for the configuration files
# and 0 for the icons. Read off a pack the reference builder produced.
TRP_TYPE_SFM = 1
TRP_TYPE_ICON = 0


def trp_payloads(config, config_dir, conf_dir):
    """(name, bytes) for every file that belongs in the container, in order.

    The set's own icon is named ICON0.PNG inside the container whatever it is
    called on disc, and is distinct from the per-trophy icons. A set may declare
    its own; where it does not the store-front icon stands in, so that a pack
    always carries the file the format lists as required.
    """
    trophies = config["trophies"]
    files = []
    for name in ("TROPCONF.SFM", "TROP.SFM"):
        with open(os.path.join(conf_dir, name), "rb") as fh:
            files.append((name, fh.read()))

    icon0 = trophies.get("icon") or (config.get("livearea") or {}).get("icon")
    if icon0:
        with open(_resolve(config_dir, icon0), "rb") as fh:
            files.append(("ICON0.PNG", fh.read()))

    for entry in sorted(trophies["list"], key=lambda e: e["id"]):
        icon = entry.get("icon")
        if not icon:
            continue
        with open(_resolve(config_dir, icon), "rb") as fh:
            files.append(("TROP{:03d}.PNG".format(entry["id"]), fh.read()))
    return files


def _host_path(path):
    """A path the packer can open.

    TRPWork is a Windows binary and the Vita build runs under WSL, so a Linux
    path reaches it as a drive-relative one that does not exist. Translate when
    the translator is there, and pass through when it is not.
    """
    if not shutil.which("wslpath"):
        return path
    try:
        done = subprocess.run(["wslpath", "-w", path], check=True, capture_output=True, text=True)
        return done.stdout.strip() or path
    except (OSError, subprocess.CalledProcessError):
        return path


def emit_trp_index(config, config_dir, conf_dir, out_dir, magic=TRP_MAGIC):
    """Write what TRPWork repacks from: a header and an entry table naming the
    payloads, with the payloads themselves in a directory beside it.

    Offsets, sizes, the total length and the digest are left zero on purpose.
    TRPWork recomputes all four, including the sixteen-byte payload alignment
    that is the reason the container is its job rather than ours.
    """
    files = trp_payloads(config, config_dir, conf_dir)
    payload_dir = os.path.join(out_dir, "TROPHY")
    os.makedirs(payload_dir, exist_ok=True)
    for name, blob in files:
        _write_bytes(os.path.join(payload_dir, name), blob)

    count = len(files)
    header = (struct.pack(">IIQIII", magic, TRP_VERSION, 0, count, TRP_HEADER_SIZE, TRP_DEV_FLAG)
              + b"\0" * 20 + b"\0" * 16)
    entries = b"".join(
        name.encode("ascii").ljust(TRP_NAME_SIZE, b"\0") + b"\0" * 28 for name, _ in files)
    index_path = os.path.join(out_dir, "TROPHY.index")
    _write_bytes(index_path, header + entries)
    return index_path


def pack_trp(trpwork, index_path):
    """Hand the entry table and payloads to TRPWork, which writes the container."""
    done = subprocess.run([trpwork, _host_path(index_path)], capture_output=True, text=True)
    output = (done.stdout or "") + (done.stderr or "")
    if done.returncode != 0 or "Exception" in output:
        raise PackageError("TRPWork", output.strip() or "exited {}".format(done.returncode))

    out = os.path.join(os.path.dirname(index_path), "TROPHY.TRP")
    if not os.path.exists(out):
        raise PackageError("TRPWork", "produced no container at {}: {}".format(out, output.strip()))
    finalize_trp(out)
    return out


def finalize_trp(path):
    """Make sure the container still declares itself a development pack, and
    carries a digest that matches.

    The packer does not necessarily preserve the flag from the entry table it is
    given, and the flag is what tells a reader the payloads are in the clear. The
    digest covers the header, so setting the flag invalidates it; both are
    computed here rather than left to be discovered on hardware.
    """
    with open(path, "rb") as fh:
        data = bytearray(fh.read())

    struct.pack_into(">I", data, 0x18, TRP_DEV_FLAG)

    count, entry_size = struct.unpack_from(">I", data, 0x10)[0], struct.unpack_from(">I", data, 0x14)[0]
    for index in range(count):
        base = TRP_HEADER_SIZE + index * entry_size
        name = bytes(data[base:base + 32]).rstrip(b"\0").decode("ascii", "replace")
        struct.pack_into(">I", data, base + 0x30, TRP_TYPE_SFM if name.endswith(".SFM") else TRP_TYPE_ICON)

    data[0x1C:0x30] = b"\0" * 20
    data[0x1C:0x30] = hashlib.sha1(bytes(data)).digest()

    with open(path, "wb") as fh:
        fh.write(bytes(data))
    return path

def emit_ids(config, out_path):
    """A C++ header of trophy identifiers, generated from the same declaration
    the pack is built from, so the two cannot drift."""
    trophies = config.get("trophies") or {}
    entries = sorted(trophies.get("list") or [], key=lambda e: e["id"])

    lines = [
        "#pragma once",
        "",
        "// Generated by tools/vita_package.py from game/config/platform/vita/package.json.",
        "// Do not edit: regenerate by building.",
        "",
        "#include <cstdint>",
        "",
        "enum class TrophyId : uint8_t",
        "{",
    ]
    for entry in entries:
        ident = "".join(c if c.isalnum() else "_" for c in entry["name"]).strip("_")
        if not ident or ident[0].isdigit():
            ident = "Trophy" + ident
        detail = entry.get("detail", "")
        comment = f" // {entry['grade']}" + (f" - {detail}" if detail else "")
        lines.append(f"    {ident} = {entry['id']},{comment}")
    lines += ["", f"    Count = {len(entries)}", "};", ""]

    _write(out_path, "\n".join(lines))
    return out_path


def _write(path, text):
    parent = os.path.dirname(os.path.abspath(path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def _write_crlf(path, text):
    """Write a file whose line endings and byte order mark are part of its
    format, rather than a detail a text writer may normalise away."""
    parent = os.path.dirname(os.path.abspath(path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)


def _write_bytes(path, data):
    parent = os.path.dirname(os.path.abspath(path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(data)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Vita package config reader and emitter")
    ap.add_argument("--config", default=DEFAULT_CONFIG)
    ap.add_argument("--schema", default=DEFAULT_SCHEMA)
    ap.add_argument("--variant", help="variant whose override block to apply (vita, vitatv)")
    ap.add_argument("--generated-dir", default=".", help="where generated pieces are written, for path emission")
    ap.add_argument("--validate", action="store_true")
    ap.add_argument("--emit-cmake", metavar="FILE")
    ap.add_argument("--emit-template", metavar="FILE")
    ap.add_argument("--emit-trophy-conf", metavar="DIR")
    ap.add_argument("--emit-trp-index", metavar="DIR",
                    help="write TROPHY.index and TROPHY/ for TRPWork to repack into a container")
    ap.add_argument("--trpwork", metavar="EXE",
                    help="TRPWork, run after --emit-trp-index to build the container")
    ap.add_argument("--emit-ids", metavar="FILE")
    ap.add_argument("--trp-magic", help="override the container magic written into the index. See docs/formats/TROPHY_PACK.md")
    args = ap.parse_args(argv)

    config_dir = os.path.dirname(os.path.abspath(args.config))

    try:
        raw = load_config(args.config)
        schema_validate(raw, args.schema)
        config = resolve_variant(raw, args.variant)
        validate(config, config_dir, args.variant)

        did_something = args.validate
        if args.emit_template:
            print("template  ->", emit_template(config, args.emit_template))
            did_something = True
        if args.emit_trophy_conf:
            if (config.get("trophies") or {}).get("list"):
                print("trophy conf ->", emit_trophy_conf(config, args.emit_trophy_conf))
            did_something = True
        if args.emit_trp_index:
            if (config.get("trophies") or {}).get("list"):
                conf_dir = args.emit_trophy_conf or args.emit_trp_index
                index_path = emit_trp_index(config, config_dir, conf_dir, args.emit_trp_index)
                print("trophy index ->", index_path)
                if args.trpwork:
                    print("trophy pack  ->", pack_trp(args.trpwork, index_path))
            did_something = True
        if args.emit_ids:
            print("trophy ids ->", emit_ids(config, args.emit_ids))
            did_something = True
        if args.emit_cmake:
            print("cmake     ->", emit_cmake(config, config_dir, args.generated_dir, args.emit_cmake))
            did_something = True

        if args.validate:
            name = config["title"]["name"]
            print(f"vita_package: OK - {config['title']['id']} '{name}' {config['title']['version']}"
                  + (f" [{args.variant}]" if args.variant else ""))
        if not did_something:
            ap.error("nothing to do: pass --validate or one of the --emit-* options")
    except PackageError as exc:
        print(f"vita_package: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
