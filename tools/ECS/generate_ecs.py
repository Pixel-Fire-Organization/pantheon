#!/usr/bin/env python3
"""ECS code generator.

Reads the game's entity/component definitions (ECS.json) and emits:
  * a C++ header  (EcsComponents.h) : POD component structs + per-entity aggregates
  * a C++ source  (EcsSpawn.cpp)    : classname -> typed-component spawn dispatch
  * a TrenchBroom FGD               : editor entity/component definitions

The ECS is owned by the *game*, not the engine: the engine only delivers generic
spawn records (game::EntitySpawn) and the generated dispatch turns them into the
game's typed components. This tool is the single source of truth for all three
outputs and runs at build time (and in CI) so the three can never drift.

Validation is intentionally fail-loud: any structural or semantic error prints an
actionable message with a JSON path and exits non-zero, so a bad ECS.json fails
the build rather than emitting broken code. jsonschema (draft-07) is used as an
extra structural pass when importable; the hand-rolled checks below are the source
of truth and need no third-party package.
"""

import argparse
import json
import os
import sys

# ---------------------------------------------------------------------------
# Type mappings (kept identical to the retired C# generator for FGD parity)
# ---------------------------------------------------------------------------

VALID_PROP_TYPES = ("int", "float", "bool", "string", "choices", "flags", "color255", "studio")
VALID_CLASS_TYPES = ("PointClass", "SolidClass")

_CPP_TYPE = {
    "int": "int",
    "float": "float",
    "bool": "bool",
    "string": "const char*",
    "color255": "const char*",
    "studio": "const char*",
    "choices": "int",
    "flags": "uint32_t",
}

_FGD_TYPE = {
    "int": "integer",
    "float": "string",
    "bool": "choices",
    "string": "string",
    "color255": "color255",
    "studio": "string",
    "choices": "choices",
    "flags": "flags",
}

# Reserved C++ identifiers a property/component/classname must not collide with.
_CPP_KEYWORDS = frozenset("""
alignas alignof and and_eq asm auto bitand bitor bool break case catch char char16_t
char32_t class compl const constexpr const_cast continue decltype default delete do
double dynamic_cast else enum explicit export extern false float for friend goto if
inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq
private protected public register reinterpret_cast return short signed sizeof static
static_assert static_cast struct switch template this thread_local throw true try
typedef typeid typename union unsigned using virtual void volatile wchar_t while xor
xor_eq
""".split())

# Names the generator itself emits; user identifiers must not shadow them.
_RESERVED_GENERATED = frozenset({"ComponentTypeId", "None", "EcsHooks", "IComponent"})


class EcsError(Exception):
    """A validation error carrying a JSON-path-ish location for the message."""

    def __init__(self, where, message):
        super().__init__(f"{where}: {message}")


# ---------------------------------------------------------------------------
# Loading + validation
# ---------------------------------------------------------------------------

def _load_json(path):
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    # Tolerate // line comments the way the C# tool did (JSON with comments).
    stripped = []
    for line in text.splitlines():
        s = line.lstrip()
        if s.startswith("//"):
            continue
        stripped.append(line)
    try:
        return json.loads("\n".join(stripped))
    except json.JSONDecodeError as exc:
        raise EcsError(os.path.basename(path), f"invalid JSON: {exc}") from exc


def _schema_validate(data, schema_path):
    """Optional draft-07 pass; silently skipped if jsonschema is unavailable."""
    try:
        import jsonschema  # type: ignore
    except ImportError:
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
        raise EcsError("schema", "draft-07 validation failed:\n" + "\n".join(parts))


def _is_ident(name):
    if not name or (not name[0].isalpha() and name[0] != "_"):
        return False
    return all(c.isalnum() or c == "_" for c in name)


def _check_ident(name, where, kind):
    if not isinstance(name, str) or not _is_ident(name):
        raise EcsError(where, f"{kind} '{name}' is not a valid C++ identifier")
    if name in _CPP_KEYWORDS:
        raise EcsError(where, f"{kind} '{name}' is a reserved C++ keyword")
    if name in _RESERVED_GENERATED:
        raise EcsError(where, f"{kind} '{name}' collides with a generated name")


def _default_matches_type(default, ptype):
    if ptype in ("int", "choices"):
        return isinstance(default, int) and not isinstance(default, bool)
    if ptype == "flags":
        return isinstance(default, int) and not isinstance(default, bool)
    if ptype == "float":
        return isinstance(default, (int, float)) and not isinstance(default, bool)
    if ptype == "bool":
        return isinstance(default, bool)
    if ptype in ("string", "studio", "color255"):
        return isinstance(default, str)
    return False


def validate(data):
    """Semantic validation. Raises EcsError on the first problem found."""
    if not isinstance(data, dict):
        raise EcsError("<root>", "top-level value must be an object")
    components = data.get("components", [])
    entities = data.get("entities", [])
    hooks = data.get("hooks", [])

    if not isinstance(components, list):
        raise EcsError("components", "must be an array")
    if not isinstance(entities, list):
        raise EcsError("entities", "must be an array")
    if not isinstance(hooks, list) or not all(isinstance(h, str) for h in hooks):
        raise EcsError("hooks", "must be an array of strings")
    hook_set = set(hooks)

    comp_by_name = {}
    for ci, comp in enumerate(components):
        where = f"components[{ci}]"
        name = comp.get("name")
        _check_ident(name, where, "component name")
        if name in comp_by_name:
            raise EcsError(where, f"duplicate component name '{name}'")
        comp_by_name[name] = comp

        prop_names = set()
        for pi, prop in enumerate(comp.get("properties", []) or []):
            pwhere = f"{where}.properties[{pi}]"
            pname = prop.get("name")
            _check_ident(pname, pwhere, "property name")
            if pname in prop_names:
                raise EcsError(pwhere, f"duplicate property name '{pname}' in component '{name}'")
            prop_names.add(pname)

            ptype = prop.get("type")
            if ptype not in VALID_PROP_TYPES:
                raise EcsError(pwhere, f"unknown property type '{ptype}'")

            if ptype in ("choices", "flags"):
                opts = prop.get("options")
                if not isinstance(opts, dict) or not opts:
                    raise EcsError(pwhere, f"type '{ptype}' requires a non-empty 'options' map")
                keys = []
                for k in opts:
                    try:
                        keys.append(int(k))
                    except (TypeError, ValueError):
                        raise EcsError(pwhere, f"option key '{k}' is not an integer")
                if ptype == "flags":
                    for k in keys:
                        if k <= 0 or (k & (k - 1)) != 0:
                            print(f"warning: {pwhere}: flags option {k} is not a power of two",
                                  file=sys.stderr)

            if "default" in prop and prop["default"] is not None:
                dval = prop["default"]
                if not _default_matches_type(dval, ptype):
                    raise EcsError(pwhere, f"default {dval!r} does not match type '{ptype}'")
                if ptype == "choices" and int(dval) not in [int(k) for k in prop["options"]]:
                    raise EcsError(pwhere, f"choices default {dval} is not one of the option keys")
                if ptype == "flags":
                    allowed = 0
                    for k in prop["options"]:
                        allowed |= int(k)
                    if int(dval) & ~allowed:
                        raise EcsError(pwhere, f"flags default {dval} sets bits outside the options")

        for ai, action in enumerate(comp.get("actions", []) or []):
            awhere = f"{where}.actions[{ai}]"
            _check_ident(action.get("name"), awhere, "action name")
            hook = action.get("engineHook")
            if not isinstance(hook, str) or not hook:
                raise EcsError(awhere, "action requires a non-empty 'engineHook'")
            if hook not in hook_set:
                raise EcsError(awhere,
                               f"engineHook '{hook}' is not declared in the top-level 'hooks' array")

    class_names = set()
    for ei, ent in enumerate(entities):
        where = f"entities[{ei}]"
        cname = ent.get("classname")
        _check_ident(cname, where, "classname")
        if cname in class_names:
            raise EcsError(where, f"duplicate classname '{cname}'")
        class_names.add(cname)

        ctype = ent.get("classType", "PointClass")
        if ctype not in VALID_CLASS_TYPES:
            raise EcsError(where, f"unknown classType '{ctype}'")

        seen_props = {}
        for ref in ent.get("components", []) or []:
            if ref not in comp_by_name:
                raise EcsError(where, f"references unknown component '{ref}'")
            for prop in comp_by_name[ref].get("properties", []) or []:
                pn = prop["name"]
                if pn in seen_props:
                    raise EcsError(where,
                                   f"property '{pn}' from component '{ref}' collides with the same "
                                   f"property from component '{seen_props[pn]}' (FGD base-class merge "
                                   f"would be ambiguous)")
                seen_props[pn] = ref


# ---------------------------------------------------------------------------
# Small helpers shared by the emitters
# ---------------------------------------------------------------------------

def _member_name(component_name):
    """HealthComponent -> healthComponent (aggregate member name)."""
    return component_name[0].lower() + component_name[1:]


def _prop_default(prop):
    return prop.get("default") if prop.get("default") is not None else None


# ---------------------------------------------------------------------------
# C++ header
# ---------------------------------------------------------------------------

def _cpp_float_literal(default):
    text = repr(float(default))
    if "." not in text and "e" not in text and "E" not in text:
        text += ".0"
    return text + "f"


def generate_header(data):
    lines = []
    w = lines.append
    w("// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY")
    w("// Generated from tools/ECS/ECS.json by tools/ECS/generate_ecs.py")
    w("#pragma once")
    w("")
    w("#include <cstdint>")
    w("")

    hooks = data.get("hooks", [])
    if hooks:
        w("// Engine hooks the game must implement (see game/src/EcsHooks.cpp).")
        w("namespace EcsHooks")
        w("{")
        for hook in hooks:
            w(f"    void {hook}(void* component);")
        w("}  // namespace EcsHooks")
        w("")

    components = data.get("components", [])
    w("enum class ComponentTypeId : uint16_t")
    w("{")
    w("    None = 0,")
    for i, comp in enumerate(components):
        w(f"    {comp['name']} = {i + 1},")
    w("};")
    w("")

    for comp in components:
        w(f"struct {comp['name']}")
        w("{")
        w(f"    static const ComponentTypeId kTypeId = ComponentTypeId::{comp['name']};")
        for prop in comp.get("properties", []) or []:
            ptype = prop["type"]
            cpp_type = _CPP_TYPE[ptype]
            default = _prop_default(prop)
            if default is None:
                literal = '""' if cpp_type == "const char*" else "0"
            elif cpp_type == "const char*":
                literal = '"%s"' % default
            elif cpp_type == "bool":
                literal = "true" if default else "false"
            elif cpp_type == "float":
                literal = _cpp_float_literal(default)
            else:
                literal = str(int(default))
            w(f"    {cpp_type} {prop['name']} = {literal};")
        for action in comp.get("actions", []) or []:
            w(f"    void {action['name']}() {{ EcsHooks::{action['engineHook']}(this); }}")
        w("};")
        w("")

    entities = data.get("entities", [])
    if entities:
        w("// Per-entity aggregates: one typed struct per Trenchbroom classname.")
        for ent in entities:
            w(f"struct Ecs_{ent['classname']}")
            w("{")
            for ref in ent.get("components", []) or []:
                w(f"    {ref} {_member_name(ref)};")
            w("};")
            w("")
        w("// Spawn dispatch entry point (defined in the generated EcsSpawn.cpp).")
        w("// Register it with game::SetSpawnHandler(&Ecs_SpawnDispatch) in GameInit().")
        w("namespace game")
        w("{")
        w("    struct EntitySpawn;")
        w("}")
        w("bool Ecs_SpawnDispatch(const game::EntitySpawn& spawn);")

    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# C++ spawn dispatch source
# ---------------------------------------------------------------------------

def _parse_expr(prop, member, var="v"):
    """C++ expression assigning parsed prop value `var` into def.<member>.<prop>."""
    ptype = prop["type"]
    target = f"def.{member}.{prop['name']}"
    if ptype in ("int", "choices"):
        return f"{target} = (int){var};", "long"
    if ptype == "flags":
        return f"{target} = (uint32_t){var};", "ulong"
    if ptype == "float":
        return f"{target} = (float){var};", "double"
    if ptype == "bool":
        return f"{target} = {var};", "bool"
    # string / studio / color255 : point straight at the spawn-record string.
    return f"{target} = {var};", "str"


def generate_source(data):
    lines = []
    w = lines.append
    w("// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY")
    w("// Generated from tools/ECS/ECS.json by tools/ECS/generate_ecs.py")
    w('#include "EcsComponents.h"')
    w('#include "GameAPI.h"')
    w("")
    w("#include <cstdlib>")
    w("#include <cstring>")
    w("")
    w("// Game-implemented spawn handlers, one per classname. A missing definition is a")
    w("// link error on purpose: adding an entity to ECS.json forces the game to handle it.")
    for ent in data.get("entities", []):
        cn = ent["classname"]
        w(f"void Game_Spawn_{cn}(const Ecs_{cn}& def, const game::EntitySpawn& spawn);")
    w("")
    w("namespace")
    w("{")
    w("    const char* Internal_FindProp(const game::EntitySpawn& spawn, const char* key)")
    w("    {")
    w("        for (int i = 0; i < spawn.propCount; ++i)")
    w("        {")
    w("            if (std::strcmp(spawn.props[i].key, key) == 0)")
    w("            {")
    w("                return spawn.props[i].value;")
    w("            }")
    w("        }")
    w("        return nullptr;")
    w("    }")
    w("}  // namespace")
    w("")
    w("// Turn a generic engine spawn record into the game's typed components, applying")
    w("// declared defaults for any property the map did not set. Returns false for an")
    w("// unknown classname. String properties point into the spawn record, which is only")
    w("// valid for the duration of the handler call -- copy anything you need to keep.")
    w("bool Ecs_SpawnDispatch(const game::EntitySpawn& spawn)")
    w("{")

    comp_by_name = {c["name"]: c for c in data.get("components", [])}
    for ent in data.get("entities", []):
        cn = ent["classname"]
        w(f'    if (std::strcmp(spawn.classname, "{cn}") == 0)')
        w("    {")
        w(f"        Ecs_{cn} def;")
        for ref in ent.get("components", []) or []:
            member = _member_name(ref)
            for prop in comp_by_name[ref].get("properties", []) or []:
                assign, parse = _parse_expr(prop, member)
                pname = prop["name"]
                w("        {")
                w(f'            const char* raw = Internal_FindProp(spawn, "{pname}");')
                if parse == "long":
                    w("            if (raw) { long v = std::strtol(raw, nullptr, 10); " + assign + " }")
                elif parse == "ulong":
                    w("            if (raw) { unsigned long v = std::strtoul(raw, nullptr, 10); " + assign + " }")
                elif parse == "double":
                    w("            if (raw) { double v = std::strtod(raw, nullptr); " + assign + " }")
                elif parse == "bool":
                    w("            if (raw) { bool v = (std::strcmp(raw, \"1\") == 0 || "
                      "std::strcmp(raw, \"true\") == 0); " + assign + " }")
                else:  # str
                    w("            if (raw) { const char* v = raw; " + assign + " }")
                w("        }")
        w(f"        Game_Spawn_{cn}(def, spawn);")
        w("        return true;")
        w("    }")
    w("    return false;")
    w("}")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# FGD (byte-for-byte compatible with the retired C# generator)
# ---------------------------------------------------------------------------

def _fgd_default(prop):
    ptype = prop["type"]
    default = _prop_default(prop)
    if default is None:
        return '""' if ptype in ("string", "studio") else "0"
    if ptype == "bool":
        return "1" if default else "0"
    if ptype in ("string", "studio"):
        return '"%s"' % default
    return str(default)


def generate_fgd(data):
    out = []

    def wl(s=""):
        out.append(s + "\n")

    wl("// AUTO-GENERATED BY ECS TOOL")
    wl()

    for comp in data.get("components", []):
        modifiers = ""
        studio = next((p for p in comp.get("properties", []) or [] if p["type"] == "studio"), None)
        if studio is not None:
            modifiers = 'model({ "path": %s }) ' % studio["name"]
        wl(f"@BaseClass {modifiers}= {comp['name']} [")
        for prop in comp.get("properties", []) or []:
            fgd_type = _FGD_TYPE[prop["type"]]
            default = _fgd_default(prop)
            ptype = prop["type"]
            if ptype in ("choices", "flags", "bool"):
                assign = f' : "{default}" =' if ptype in ("choices", "bool") else " ="
                wl(f"    {prop['name']}({fgd_type}){assign} [")
                options = prop.get("options")
                if ptype == "bool":
                    options = {"0": "No", "1": "Yes"}
                if options:
                    for key, value in options.items():
                        if ptype == "flags":
                            is_set = (int(default) & int(key)) != 0
                            wl(f'        {key} : "{value}" : {1 if is_set else 0}')
                        else:
                            wl(f'        {key} : "{value}"')
                wl("    ]")
            else:
                wl(f'    {prop["name"]}({fgd_type}) : "{prop.get("description", "")}" : {default}')
        wl("]")
        wl()

    for ent in data.get("entities", []):
        bases = ", ".join(ent.get("components", []) or [])
        base_attr = f"base({bases}) " if bases else ""
        class_type = ent.get("classType") or "PointClass"
        size_attr = "size(-16 -16 -16, 16 16 16) " if class_type == "PointClass" else ""
        wl(f'@{class_type} {base_attr}{size_attr}= {ent["classname"]} : "{ent.get("description", "")}" []')
        wl()

    return "".join(out)


# ---------------------------------------------------------------------------
# Output writing + CLI
# ---------------------------------------------------------------------------

def _write_lf(path, text):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    # Always LF so output matches the git-normalized (text=auto) form on every OS.
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Generate ECS C++ + FGD from ECS.json")
    ap.add_argument("--ecs", required=True, help="path to ECS.json")
    ap.add_argument("--schema", help="path to schema.json (optional draft-07 pass)")
    ap.add_argument("--out-header", help="path to write EcsComponents.h")
    ap.add_argument("--out-source", help="path to write EcsSpawn.cpp")
    ap.add_argument("--out-fgd", help="path to write the TrenchBroom FGD")
    ap.add_argument("--check", action="store_true", help="validate only; write nothing")
    args = ap.parse_args(argv)

    try:
        data = _load_json(args.ecs)
        if args.schema:
            _schema_validate(data, args.schema)
        validate(data)
    except EcsError as exc:
        print(f"BUILD FAILED: ECS validation error in {args.ecs}:\n  {exc}", file=sys.stderr)
        return 1

    if args.check:
        print("ECS validation passed.")
        return 0

    if args.out_header:
        _write_lf(args.out_header, generate_header(data))
    if args.out_source:
        _write_lf(args.out_source, generate_source(data))
    if args.out_fgd:
        _write_lf(args.out_fgd, generate_fgd(data))

    generated = [p for p in (args.out_header, args.out_source, args.out_fgd) if p]
    print("ECS generated:")
    for path in generated:
        print(f" -> {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
