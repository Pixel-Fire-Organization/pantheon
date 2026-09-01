"""The title's action declaration: reader, validator and per-platform code generator.

One description of the action map, declared once for every platform, feeds a
generated identifier header (platform-independent: action and context ids
never vary) and a generated table (platform-specific: which of an action's
candidate bindings actually exist on the running hardware does vary, so the
table is compiled once per active platform against that platform's own
engine/config/<name>/input_capabilities.json).

    python3 tools/actions.py --declaration game/config/actions.json \
        --emit-ids build/generated/ActionIds.h

    python3 tools/actions.py --declaration game/config/actions.json \
        --platform win32 \
        --emit-table build/generated/win32/ActionTable.cpp

See docs/subsystems/ACTION.md and docs/formats/ACTION_OVERLAY.md.
"""

import argparse
import json
import os
import re
import sys

# Format constants -- mirrored in engine/include/core/EngineAction.h and
# cross-checked against it by tools/tests/test_actions.py.
ACTION_MAX_ENTRIES = 256
ACTION_MAX_BINDINGS = 8
ACTION_MAX_SOURCES_PER_BINDING = 4
ACTION_MAX_CONTEXTS = 32
ACTION_MAX_DYNAMIC_BINDINGS = 64

DEFAULT_DECLARATION = os.path.join("game", "config", "actions.json")

PLATFORM_KEYS_HEADER = os.path.join("engine", "include", "platform", "PlatformKeys.h")

DEADZONE_HEADER = {
    "ps2pal": os.path.join("engine", "include", "platform", "ps2", "PlatformConstantsPs2.h"),
    "ps2ntsc": os.path.join("engine", "include", "platform", "ps2", "PlatformConstantsPs2.h"),
    "win32": os.path.join("engine", "include", "platform", "win32", "PlatformConstants.h"),
    "vita": os.path.join("engine", "include", "platform", "vita", "PlatformConstantsVita.h"),
    "vitatv": os.path.join("engine", "include", "platform", "vita", "PlatformConstantsVita.h"),
    "psp": os.path.join("engine", "include", "platform", "psp", "PlatformConstants.h"),
    "nx": os.path.join("engine", "include", "platform", "nx", "PlatformConstants.h"),
}

CAPABILITIES_PATH = {
    "ps2pal": os.path.join("engine", "config", "ps2", "input_capabilities.json"),
    "ps2ntsc": os.path.join("engine", "config", "ps2", "input_capabilities.json"),
    "win32": os.path.join("engine", "config", "win32", "input_capabilities.json"),
    "vita": os.path.join("engine", "config", "vita", "handheld", "input_capabilities.json"),
    "vitatv": os.path.join("engine", "config", "vita", "tv", "input_capabilities.json"),
    "psp": os.path.join("engine", "config", "psp", "input_capabilities.json"),
    "nx": os.path.join("engine", "config", "nx", "input_capabilities.json"),
}

# --- Source name grammar -----------------------------------------------------
# "device.source" as declared in game/config/actions.json. Gamepad buttons
# resolve to a bit POSITION, not the hardware mask (ACTION_OVERLAY.md: the
# mask does not fit ten bits and is not denser if it did) -- this table and
# engine/src/GameAPI.cpp's kGamepadButtons must be read together if either
# changes. Every other device's codes are the engine enumerator's own ordinal.

GAMEPAD_BUTTON_BIT = {
    "select": 0, "l3": 1, "r3": 2, "start": 3,
    "dpad_up": 4, "dpad_right": 5, "dpad_down": 6, "dpad_left": 7,
    "l2": 8, "r2": 9, "l1": 10, "r1": 11,
    "triangle": 12, "circle": 13, "cross": 14, "square": 15,
}
GAMEPAD_STICK = {"stick_left": 0, "stick_right": 1}
GAMEPAD_TRIGGER = {"trigger_left": 0, "trigger_right": 1}
MOUSE_BUTTON = {"left": 0, "right": 1, "middle": 2, "extra1": 3, "extra2": 4}
MOUSE_AXIS = {"delta": 0}

# KeyboardKey's own declaration order in PlatformKeys.h, lowercased. Index 0
# (Unknown) is never a valid source. Matches engine/src/GameAPI.cpp's
# kKeyboardKeyNames exactly -- the two must be read together if either changes.
KEYBOARD_KEY_NAMES = (
    "", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v",
    "w", "x", "y", "z", "num0", "num1", "num2", "num3", "num4", "num5", "num6", "num7", "num8", "num9", "f1", "f2",
    "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12", "left", "right", "up", "down", "space", "enter",
    "escape", "tab", "backspace", "delete", "insert", "home", "end", "pageup", "pagedown", "leftshift", "rightshift",
    "leftcontrol", "rightcontrol", "leftalt", "rightalt", "minus", "equal", "leftbracket", "rightbracket",
    "semicolon", "apostrophe", "comma", "period", "slash", "backslash", "grave",
)
KEYBOARD_KEY = {name: i for i, name in enumerate(KEYBOARD_KEY_NAMES) if name}

DEVICE_CODE = {"gamepad": 0, "keyboard": 1, "mouse": 2, "touch": 3}
KIND_CODE = {"button": 0, "stick": 1, "trigger": 2, "contact": 3}

# Reverse of the source tables above, for rendering a compiled (device, kind,
# code) tuple back into "device.control" in an error message.
_REVERSE_GAMEPAD_BUTTON = {v: k for k, v in GAMEPAD_BUTTON_BIT.items()}
_REVERSE_GAMEPAD_STICK = {v: k for k, v in GAMEPAD_STICK.items()}
_REVERSE_GAMEPAD_TRIGGER = {v: k for k, v in GAMEPAD_TRIGGER.items()}
_REVERSE_MOUSE_BUTTON = {v: k for k, v in MOUSE_BUTTON.items()}
_REVERSE_MOUSE_AXIS = {v: k for k, v in MOUSE_AXIS.items()}


def describe_source(source):
    """@return "device.control" for a compiled (device, kind, code) tuple."""
    device, kind, code = source
    if device == "gamepad":
        table = {"button": _REVERSE_GAMEPAD_BUTTON, "stick": _REVERSE_GAMEPAD_STICK, "trigger": _REVERSE_GAMEPAD_TRIGGER}.get(kind, {})
        return "gamepad.{}".format(table.get(code, code))
    if device == "keyboard":
        return "keyboard.{}".format(KEYBOARD_KEY_NAMES[code] if code < len(KEYBOARD_KEY_NAMES) else code)
    if device == "mouse":
        table = _REVERSE_MOUSE_BUTTON if kind == "button" else _REVERSE_MOUSE_AXIS
        return "mouse.{}".format(table.get(code, code))
    return "{}.{}".format(device, code)

# The three actions with the highest declared ids, in this exact order, are
# always the engine's own debug intents -- mirrored in
# engine/include/core/EngineAction.h's ACTION_RESERVED_KEY_* defines and
# cross-checked against them by tools/tests/test_actions.py. "Last three by
# id" (not a fixed id) keeps a title's own ids dense from zero with no gap;
# engine code that cannot see a title's generated ActionId enumerators finds
# them via Engine_Action_Reserved*() instead of a literal.
ACTION_RESERVED_COUNT = 3
RESERVED_KEYS = ("EngineDebugPerfSnapshot", "EngineDebugOverlayToggle", "EngineDebugMenu")


class ActionError(Exception):
    """A declaration (or a platform it will not compile for) with the reason a person needs."""

    def __init__(self, where, message):
        super().__init__("{}: {}".format(where, message))


def resolve_source(text, where):
    """@return (device, kind, code, control_name) for a "device.control" string. Raises ActionError."""
    if not isinstance(text, str) or "." not in text:
        raise ActionError(where, "source '{}' is not \"device.control\"".format(text))
    device, name = text.split(".", 1)

    if device == "gamepad":
        if name in GAMEPAD_STICK:
            return "gamepad", "stick", GAMEPAD_STICK[name], name
        if name in GAMEPAD_TRIGGER:
            return "gamepad", "trigger", GAMEPAD_TRIGGER[name], name
        if name in GAMEPAD_BUTTON_BIT:
            return "gamepad", "button", GAMEPAD_BUTTON_BIT[name], name
        raise ActionError(where, "unknown gamepad source '{}'".format(name))
    if device == "keyboard":
        if name not in KEYBOARD_KEY:
            raise ActionError(where, "unknown keyboard source '{}'".format(name))
        return "keyboard", "button", KEYBOARD_KEY[name], name
    if device == "mouse":
        if name in MOUSE_AXIS:
            return "mouse", "stick", MOUSE_AXIS[name], name
        if name in MOUSE_BUTTON:
            return "mouse", "button", MOUSE_BUTTON[name], name
        raise ActionError(where, "unknown mouse source '{}'".format(name))
    if device == "touch":
        raise ActionError(where, "touch sources are not yet supported (see ACTION_INTEGRATION notes on scope)")
    raise ActionError(where, "unknown device '{}' in source '{}'".format(device, text))


def source_viable(device, name, manifest):
    """@return Whether a resolved source's control exists on a platform's capability manifest."""
    if device == "gamepad":
        gp = manifest.get("gamepad")
        if not gp:
            return False
        if name in GAMEPAD_STICK:
            return name in gp.get("sticks", [])
        if name in GAMEPAD_TRIGGER:
            return name in gp.get("triggers", [])
        return name in gp.get("buttons", [])
    if device == "keyboard":
        return bool(manifest.get("keyboard"))
    if device == "mouse":
        return bool(manifest.get("mouse"))
    return False


def load_capabilities(project_root, platform):
    path = os.path.join(project_root, CAPABILITIES_PATH[platform]) if platform in CAPABILITIES_PATH else None
    if not path or not os.path.isfile(path):
        raise ActionError(platform, "no engine/config/.../input_capabilities.json for this platform")
    with open(path, "r", encoding="utf-8-sig") as handle:
        return json.load(handle)


def platform_deadzone(project_root, platform):
    if platform not in DEADZONE_HEADER:
        raise ActionError(platform, "unknown platform")
    path = os.path.join(project_root, DEADZONE_HEADER[platform])
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()
    match = re.search(r"#define\s+INPUT_ANALOG_DEADZONE\s+([0-9.]+)f?", text)
    if not match:
        raise ActionError(path, "no INPUT_ANALOG_DEADZONE #define found")
    return float(match.group(1))


# --- Declaration loading and structural validation ---------------------------

def load_declaration(path):
    """Read and structurally validate the declaration. Raises ActionError."""
    where = os.path.basename(path)
    try:
        with open(path, "r", encoding="utf-8-sig") as handle:
            declaration = json.load(handle)
    except OSError as error:
        raise ActionError(where, "cannot be read: {}".format(error))
    except ValueError as error:
        raise ActionError(where, "is not valid JSON: {}".format(error))

    contexts = declaration.get("contexts")
    actions = declaration.get("actions")
    if not isinstance(contexts, list) or not contexts:
        raise ActionError(where, "declares no contexts")
    if not isinstance(actions, list) or not actions:
        raise ActionError(where, "declares no actions")

    validate_contexts(contexts, where)
    context_keys = {c["key"] for c in contexts}
    validate_actions(actions, context_keys, where)

    sorted_actions = sorted(actions, key=lambda entry: entry["id"])
    validate_reserved_actions(sorted_actions, where)

    settings = declaration.get("settings") or {}
    settings.setdefault("combinationWindow", 0.1)
    settings.setdefault("opposing", "neutral")

    declaration["contexts"] = contexts
    declaration["actions"] = sorted_actions
    declaration["settings"] = settings
    return declaration


def validate_reserved_actions(sorted_actions, where):
    """The engine's own debug intents are the last ACTION_RESERVED_COUNT
    entries by id, in RESERVED_KEYS order -- see the constants above. This is
    what lets Debug/PerfLogger/Testbed check for input exclusively through
    Action: no title-specific lookup is needed, because every title's
    declaration ends with the same three keys at the same relative position."""
    if len(sorted_actions) < ACTION_RESERVED_COUNT:
        raise ActionError(
            where, "must declare at least the {} reserved engine actions: {}".format(ACTION_RESERVED_COUNT, list(RESERVED_KEYS)))

    tail = sorted_actions[-ACTION_RESERVED_COUNT:]
    tail_keys = [entry["key"] for entry in tail]
    if tail_keys != list(RESERVED_KEYS):
        raise ActionError(
            where,
            "the {} actions with the highest ids must be the reserved engine actions, in this order: {}. Got: {}. "
            "These are how Debug/PerfLogger/Testbed check for input -- routed through Action like everything "
            "else, found by position rather than a per-title lookup, so they must be exactly this.".format(
                ACTION_RESERVED_COUNT, list(RESERVED_KEYS), tail_keys))

    for entry in tail:
        if entry.get("kind") != "digital":
            raise ActionError(where, "reserved action '{}' must be kind 'digital'".format(entry["key"]))


def validate_contexts(contexts, where):
    if len(contexts) > ACTION_MAX_CONTEXTS:
        raise ActionError(where, "{} contexts declared, the maximum is {}".format(len(contexts), ACTION_MAX_CONTEXTS))
    keys = [c.get("key") for c in contexts]
    if len(set(keys)) != len(keys):
        duplicates = sorted({k for k in keys if keys.count(k) > 1})
        raise ActionError(where, "duplicate context keys: {}".format(duplicates))
    for c in contexts:
        if c.get("blocks") not in ("nothing", "digital", "everything"):
            raise ActionError(where, "context '{}' has an invalid 'blocks'".format(c.get("key")))


def validate_actions(actions, context_keys, where):
    if len(actions) > ACTION_MAX_ENTRIES:
        raise ActionError(where, "{} actions declared, the maximum is {}".format(len(actions), ACTION_MAX_ENTRIES))

    ids = [entry.get("id") for entry in actions]
    if any(not isinstance(value, int) for value in ids):
        raise ActionError(where, "every action needs an integer id")
    if len(set(ids)) != len(ids):
        duplicates = sorted({value for value in ids if ids.count(value) > 1})
        raise ActionError(where, "duplicate ids: {}".format(duplicates))
    if sorted(ids) != list(range(len(ids))):
        raise ActionError(
            where,
            "ids must be dense and start at zero; got {}. A gap is an identifier with no entry behind it, "
            "which nothing at runtime can recover from.".format(sorted(ids)))

    keys = [entry.get("key") for entry in actions]
    if len(set(keys)) != len(keys):
        duplicates = sorted({k for k in keys if keys.count(k) > 1})
        raise ActionError(where, "duplicate keys: {}".format(duplicates))

    for entry in actions:
        identifier = entry.get("key", entry.get("id"))
        if entry.get("context") not in context_keys:
            raise ActionError(where, "action '{}' references undeclared context '{}'".format(identifier, entry.get("context")))
        if entry.get("kind") not in ("digital", "axis1d", "axis2d", "scalar"):
            raise ActionError(where, "action '{}' has an invalid 'kind'".format(identifier))

        bindings = entry.get("bindings")
        if not isinstance(bindings, list) or not bindings:
            raise ActionError(where, "action '{}' declares no bindings".format(identifier))
        if len(bindings) > ACTION_MAX_BINDINGS:
            raise ActionError(where, "action '{}' declares {} bindings, the maximum is {}".format(identifier, len(bindings), ACTION_MAX_BINDINGS))

        for binding in bindings:
            if "sources" in binding:
                sources = binding["sources"]
                if not isinstance(sources, list) or not sources or len(sources) > ACTION_MAX_SOURCES_PER_BINDING:
                    raise ActionError(where, "action '{}' has a binding with an invalid 'sources' list".format(identifier))
                for s in sources:
                    resolve_source(s, where)
            elif "composite" in binding:
                if entry.get("kind") not in ("axis1d", "axis2d"):
                    raise ActionError(where, "action '{}' has a composite binding, legal only for axis1d/axis2d".format(identifier))
                composite = binding["composite"]
                if not isinstance(composite, dict) or not composite:
                    raise ActionError(where, "action '{}' has an empty composite binding".format(identifier))
                allowed_legs = ("up", "down", "left", "right") if entry.get("kind") == "axis2d" else ("positive", "negative")
                for leg in composite:
                    if leg not in allowed_legs:
                        raise ActionError(where, "action '{}' composite names '{}', not legal for {}".format(identifier, leg, entry.get("kind")))
                for value in composite.values():
                    resolve_source(value, where)
            else:
                raise ActionError(where, "action '{}' has a binding with neither 'sources' nor 'composite'".format(identifier))


# --- Per-platform compilation --------------------------------------------------

# Padding for an unused source slot. Deliberately NOT (gamepad, button, 0):
# bit 0 is gamepad.select, a real, meaningful source, so padding with it would
# make a 1-source binding indistinguishable from a 2-source one that happens
# to include Select once compared as a set. Touch/contact/1023 can never be a
# real resolved source (resolve_source rejects every touch.* string), so it
# carries no meaning to collide with.
ZERO_SOURCE = ("touch", "contact", 1023)


def compile_binding(binding, declared_index, kind, manifest, deadzone, action_key, platform, warnings):
    """@return A compiled binding dict, or None if it is not viable on this platform."""
    threshold = float(binding.get("threshold", 0.0))
    if threshold > 0.0 and threshold < deadzone:
        warnings.append(
            "actions: '{}' binding {}'s threshold {} raised to {} ({}'s INPUT_ANALOG_DEADZONE) -- a declared "
            "threshold can only narrow the platform deadzone, never widen it".format(action_key, declared_index, threshold, deadzone, platform))
        threshold = deadzone

    common = {
        "declaredIndex": declared_index,
        "threshold": threshold,
        "scale": float(binding.get("scale", 1.0)),
        "invert": bool(binding.get("invert", False)),
    }

    if "sources" in binding:
        sources = []
        for s in binding["sources"]:
            device, skind, code, name = resolve_source(s, action_key)
            if not source_viable(device, name, manifest):
                return None
            sources.append((device, skind, code))
        common.update({
            "isComposite": False,
            "sourceCount": len(sources),
            "sources": sources + [ZERO_SOURCE] * (ACTION_MAX_SOURCES_PER_BINDING - len(sources)),
            "legMask": 0,
            "normalize": False,
        })
        return common

    composite = binding["composite"]
    legs = ("up", "down", "left", "right") if kind == "axis2d" else ("positive", "negative")
    sources = [ZERO_SOURCE] * ACTION_MAX_SOURCES_PER_BINDING
    leg_mask = 0
    set_count = 0
    for i, leg in enumerate(legs):
        value = composite.get(leg)
        if value is None:
            continue
        device, skind, code, name = resolve_source(value, action_key)
        if not source_viable(device, name, manifest):
            return None
        sources[i] = (device, skind, code)
        leg_mask |= (1 << i)
        set_count += 1

    common.update({
        "isComposite": True,
        "sourceCount": set_count,
        "sources": sources,
        "legMask": leg_mask,
        "normalize": bool(binding.get("normalize", False)),
    })
    return common


def compile_for_platform(declaration, project_root, platform, warnings):
    """@return (compiled_by_action, digest). compiled_by_action[i] is the list of
    compiled binding dicts that survived pruning for action i, in declared order.
    Raises ActionError naming the action and platform if none survive."""
    manifest = load_capabilities(project_root, platform)
    deadzone = platform_deadzone(project_root, platform)
    window = float(declaration["settings"]["combinationWindow"])
    opposing = declaration["settings"]["opposing"]

    actions = declaration["actions"]
    compiled_by_action = []
    for entry in actions:
        survivors = []
        for idx, binding in enumerate(entry["bindings"]):
            compiled = compile_binding(binding, idx, entry["kind"], manifest, deadzone, entry["key"], platform, warnings)
            if compiled is not None:
                survivors.append(compiled)
        if not survivors:
            tried = ", ".join(json.dumps(b) for b in entry["bindings"])
            raise ActionError(platform, "action '{}' has no binding viable on this platform; tried: {}".format(entry["key"], tried))
        compiled_by_action.append(survivors)

    compute_subsets(compiled_by_action)
    check_no_duplicate_combinations(compiled_by_action, actions, platform)
    assign_dynamic_slots(compiled_by_action, opposing, declaration["actions"], platform)

    combination_mode = {entry["id"]: entry.get("combination", "deferred") for entry in actions}
    for entry, bindings in zip(actions, compiled_by_action):
        deferred = combination_mode[entry["id"]] == "deferred"
        for b in bindings:
            b["combinationWindowSeconds"] = window if (deferred and b["waitsForCombination"]) else 0.0
            if not deferred and b["waitsForCombination"]:
                warnings.append(
                    "actions: warning: '{}' is declared immediate but its binding is a subset of a wider "
                    "combination -- it fires before that combination can form; confirm this is intended".format(entry["key"]))

    digest = compute_map_digest(actions)
    return compiled_by_action, digest


def compute_subsets(compiled_by_action):
    """Derive, for every non-composite binding, whether its sources are a
    strict subset of some other (wider) non-composite binding's sources --
    across every action, since a subset relationship is a fact about shared
    physical controls, not about which action declared which binding.

    b["sources"] is always padded to ACTION_MAX_SOURCES_PER_BINDING with the
    zero-value sentinel (gamepad.select's own code is also 0, so the sentinel
    is a real, meaningful source and must never enter this comparison) --
    only the first b["sourceCount"] entries are the binding's actual sources."""
    flat = []
    for ai, bindings in enumerate(compiled_by_action):
        for bi, b in enumerate(bindings):
            if not b["isComposite"]:
                flat.append((ai, bi, frozenset(b["sources"][:b["sourceCount"]])))

    for ai, bindings in enumerate(compiled_by_action):
        for bi, b in enumerate(bindings):
            if b["isComposite"]:
                b["widerSiblings"] = []
                b["waitsForCombination"] = False
                continue
            my_set = frozenset(b["sources"][:b["sourceCount"]])
            supersets = [(oa, ob) for (oa, ob, other) in flat if (oa, ob) != (ai, bi) and my_set < other]
            b["widerSiblings"] = supersets[:4]
            b["waitsForCombination"] = bool(supersets)


def check_no_duplicate_combinations(compiled_by_action, actions, platform):
    """Two different actions bound to the exact same combination is not a
    subset relationship -- ACTION.md's "widest combination wins" only
    arbitrates between bindings of DIFFERENT width. Two bindings of equal
    width and identical sources tie-break by declaration order (see
    EngineAction.cpp's ResolveClaims, a stable sort), so whichever is declared
    later is permanently shadowed by the earlier one's claim, every time both
    are held -- not a rare race, a deterministic dead binding. Caught here
    rather than left to be found as an unreachable control, the same as an
    unviable binding is caught by compile_binding rather than left to a
    runtime report."""
    seen = {}
    for entry, bindings in zip(actions, compiled_by_action):
        for b in bindings:
            if b["isComposite"] or b["sourceCount"] <= 1:
                continue
            key = frozenset(b["sources"][:b["sourceCount"]])
            prior = seen.get(key)
            if prior is not None:
                prior_key, prior_index = prior
                names = ", ".join(describe_source(s) for s in b["sources"][:b["sourceCount"]])
                raise ActionError(
                    platform,
                    "'{}' binding {} and '{}' binding {} are both bound to exactly {{{}}}; only the "
                    "earlier-declared one can ever claim it, so the other is permanently unreachable. "
                    "Give one of them a different combination.".format(
                        prior_key, prior_index, entry["key"], b["declaredIndex"], names))
            seen[key] = (entry["key"], b["declaredIndex"])


def assign_dynamic_slots(compiled_by_action, opposing, actions, platform):
    slot_of = {}
    next_slot = 0
    for ai, bindings in enumerate(compiled_by_action):
        for bi, b in enumerate(bindings):
            needs_slot = (b["sourceCount"] > 1 and not b["isComposite"]) or b["waitsForCombination"] or (b["isComposite"] and opposing == "lastWins")
            if not needs_slot:
                b["dynamicSlot"] = 0xFF
                continue
            if next_slot >= ACTION_MAX_DYNAMIC_BINDINGS:
                raise ActionError(
                    platform,
                    "more than {} bindings need combination, deferral or lastWins state on this platform; "
                    "raise ACTION_MAX_DYNAMIC_BINDINGS or simplify the declaration".format(ACTION_MAX_DYNAMIC_BINDINGS))
            slot_of[(ai, bi)] = next_slot
            b["dynamicSlot"] = next_slot
            next_slot += 1

    for bindings in compiled_by_action:
        for b in bindings:
            b["widerSiblingDynamicSlots"] = [slot_of[key] for key in b["widerSiblings"]]


def compute_map_digest(actions):
    """A stable digest of the declaration's identity: which positions exist,
    what they are called, whether they are rebindable, and how many binding
    slots each has. Deliberately excludes which candidate survived pruning on
    any one platform, so one title-wide overlay stays valid across builds of
    the same declaration for different platforms."""
    hash_value = 2166136261
    for entry in actions:
        text = "{}|{}|{}|{}".format(entry["id"], entry["key"], int(bool(entry.get("rebindable", True))), len(entry["bindings"]))
        for byte in text.encode("utf-8"):
            hash_value ^= byte
            hash_value = (hash_value * 16777619) & 0xFFFFFFFF
    return hash_value


# --- Code generation -----------------------------------------------------------

def _escape(text):
    return str(text).replace("\\", "\\\\").replace('"', '\\"')


def _write(path, text):
    parent = os.path.dirname(os.path.abspath(path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def emit_ids(declaration, out_path):
    """The enumerators game code and the engine name actions and contexts by.
    Platform-independent: ids and context membership never vary by platform."""
    lines = [
        "#pragma once",
        "",
        "// Generated by tools/actions.py from the title's action declaration.",
        "// Do not edit: regenerate by building.",
        "",
        "#include <cstdint>",
        "",
        "enum class ActionContextId : uint8_t",
        "{",
    ]
    contexts = declaration["contexts"]
    for c in contexts:
        lines.append("    {},".format(c["key"]))
    lines += ["", "    Count = {}".format(len(contexts)), "};", ""]

    lines += ["enum class ActionId : uint8_t", "{"]
    actions = declaration["actions"]
    for entry in actions:
        lines.append("    {} = {},".format(entry["key"], entry["id"]))
    lines += ["", "    Count = {}".format(len(actions)), "};", ""]

    _write(out_path, "\n".join(lines))
    return out_path


def _source_literal(source):
    device, kind, code = source[0], source[1], source[2]
    return "Action_PackSource(ActionSourceDevice::{}, ActionSourceKind::{}, {})".format(device.capitalize(), kind.capitalize(), code)


def _binding_literal(b):
    sources = ", ".join(_source_literal(s) for s in b["sources"])
    wider = b["widerSiblingDynamicSlots"] + [0] * (4 - len(b["widerSiblingDynamicSlots"]))
    return "{{ {}, {}, {}, {{ {} }}, {}, {}f, {}f, {}, {}, {}, {}f, {}, {{ {} }}, {} }}".format(
        b["declaredIndex"], b["sourceCount"], "true" if b["isComposite"] else "false", sources, b["legMask"],
        b["threshold"], b["scale"], "true" if b["invert"] else "false", "true" if b["normalize"] else "false",
        "true" if b["waitsForCombination"] else "false", b["combinationWindowSeconds"], len(b["widerSiblingDynamicSlots"]),
        ", ".join(str(w) for w in wider), b["dynamicSlot"])


BLOCK_NAME = {"nothing": "Nothing", "digital": "Digital", "everything": "Everything"}
KIND_NAME = {"digital": "Digital", "axis1d": "Axis1d", "axis2d": "Axis2d", "scalar": "Scalar"}
COMBINATION_NAME = {"deferred": "Deferred", "immediate": "Immediate"}
OPPOSING_NAME = {"neutral": "Neutral", "lastWins": "LastWins"}


def emit_table(declaration, compiled_by_action, digest, platform, out_path):
    """The compiled map for one platform: which of each action's declared
    bindings survived capability pruning, and the static combination/deferral
    graph tools/actions.py derived for them."""
    contexts = declaration["contexts"]
    actions = declaration["actions"]
    opposing = OPPOSING_NAME[declaration["settings"]["opposing"]]

    lines = [
        "// Generated by tools/actions.py from the title's action declaration, for {}.".format(platform),
        "// Do not edit: regenerate by building.",
        "",
        '#include "core/EngineAction.h"',
        "",
        "namespace",
        "{",
        "    const ActionContextRecord kActionContexts[] =",
        "    {",
    ]
    for c in contexts:
        lines.append('        {{ "{}", ActionBlock::{} }},'.format(_escape(c["key"]), BLOCK_NAME[c["blocks"]]))
    lines += ["    };", "", "    const ActionRecord kActionTable[] =", "    {"]

    for entry, bindings in zip(actions, compiled_by_action):
        context_index = next(i for i, c in enumerate(contexts) if c["key"] == entry["context"])
        repeat = entry.get("repeat") or {}
        binding_literals = ",\n            ".join(_binding_literal(b) for b in bindings)
        lines.append("        {{ // {}".format(entry["key"]))
        lines.append('            "{}", {}, ActionKind::{}, ActionCombination::{}, ActionOpposingPolicy::{}, {},'.format(
            _escape(entry["key"]), context_index, KIND_NAME[entry["kind"]], COMBINATION_NAME[entry.get("combination", "deferred")], opposing,
            "true" if entry.get("rebindable", True) else "false"))
        lines.append("            {}f, {}f, {},".format(repeat.get("delay", 0.0), repeat.get("interval", 0.0), len(bindings)))
        lines.append("            {")
        lines.append("            " + binding_literals)
        lines.append("            }")
        lines.append("        },")
    lines += ["    };", ""]
    lines.append("    const uint32_t kActionCount = {};".format(len(actions)))
    lines.append("    const uint32_t kActionContextCount = {};".format(len(contexts)))
    lines.append("    const uint32_t kActionMapDigest = {}u;".format(digest))
    lines += [
        "}",
        "",
        "uint32_t Engine_Action_DeclaredCount() { return kActionCount; }",
        "",
        "uint32_t Engine_Action_DeclaredContextCount() { return kActionContextCount; }",
        "",
        "uint32_t Engine_Action_GetMapDigest() { return kActionMapDigest; }",
        "",
        "const ActionRecord* Engine_Action_GetRecord(ActionId action)",
        "{",
        "    const uint32_t index = static_cast<uint32_t>(action);",
        "    return (index < kActionCount) ? &kActionTable[index] : nullptr;",
        "}",
        "",
        "const ActionContextRecord* Engine_Action_GetContextRecord(ActionContextId context)",
        "{",
        "    const uint32_t index = static_cast<uint32_t>(context);",
        "    return (index < kActionContextCount) ? &kActionContexts[index] : nullptr;",
        "}",
        "",
    ]
    _write(out_path, "\n".join(lines))
    return out_path


def main(argv=None):
    parser = argparse.ArgumentParser(description="Action declaration reader and per-platform generator")
    parser.add_argument("--declaration", default=DEFAULT_DECLARATION)
    parser.add_argument("--platform", help="Required for --emit-table; selects the capability manifest and deadzone.")
    parser.add_argument("--emit-ids")
    parser.add_argument("--emit-table")
    parser.add_argument("--project-root", default=os.getcwd())
    args = parser.parse_args(argv)

    try:
        declaration = load_declaration(args.declaration)
    except ActionError as error:
        print("actions: {}".format(error), file=sys.stderr)
        return 1

    if args.emit_ids:
        print("action ids ->", emit_ids(declaration, args.emit_ids))

    if args.emit_table:
        if not args.platform:
            print("actions: --emit-table requires --platform", file=sys.stderr)
            return 1
        warnings = []
        try:
            compiled_by_action, digest = compile_for_platform(declaration, args.project_root, args.platform, warnings)
        except ActionError as error:
            print("actions: {}".format(error), file=sys.stderr)
            return 1
        for warning in warnings:
            print(warning, file=sys.stderr)
        print("action table ({}) ->".format(args.platform), emit_table(declaration, compiled_by_action, digest, args.platform, args.emit_table))

    print("actions: OK - {} declared, {} contexts".format(len(declaration["actions"]), len(declaration["contexts"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
