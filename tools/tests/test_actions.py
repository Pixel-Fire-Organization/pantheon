"""Tests for tools/actions.py — the title's action declaration.

Weighted towards the rejection paths, same reasoning as test_achievements.py:
a gap in a dense id or a binding that silently fails to compile on one
platform is a fault a player finds, not one CI catches without a test here.
"""

import importlib.util
import json
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load():
    path = ROOT / "tools" / "actions.py"
    spec = importlib.util.spec_from_file_location("actions", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


act = _load()


def _context(key, blocks="nothing"):
    return {"key": key, "blocks": blocks}


def _action(identifier, key, context="Global", kind="digital", bindings=None, **over):
    base = {"id": identifier, "key": key, "context": context, "kind": kind, "bindings": bindings or [{"sources": ["keyboard.a"]}]}
    base.update(over)
    return base


def _reserved_actions(start_id):
    """The three engine debug intents every declaration must end with, using
    the same gamepad-only bindings as game/config/actions.json (so they
    compile on every platform including ps2pal, which has no keyboard, and
    so the two fallbacks don't accidentally become subsets of DebugMenu's
    r1+select+start). DebugMenu itself is not select+start alone: that is
    also PauseGame's binding in the shipped declaration, and two different
    actions bound to the identical combination is exactly what
    check_no_duplicate_combinations rejects -- see
    test_shipped_declaration_debug_menu_does_not_collide_with_pause."""
    return [
        _action(start_id, act.RESERVED_KEYS[0],
                bindings=[{"sources": ["gamepad.l1", "gamepad.l2", "gamepad.r1", "gamepad.r2"]}, {"sources": ["gamepad.l1", "gamepad.r1", "gamepad.select"]}]),
        _action(start_id + 1, act.RESERVED_KEYS[1],
                bindings=[{"sources": ["gamepad.l1", "gamepad.l2", "gamepad.l3", "gamepad.r3"]}, {"sources": ["gamepad.l1", "gamepad.r1", "gamepad.start"]}]),
        _action(start_id + 2, act.RESERVED_KEYS[2], bindings=[{"sources": ["gamepad.r1", "gamepad.select", "gamepad.start"]}]),
    ]


def _declare(tmp_path, actions, contexts=None, settings=None, name="actions.json", with_reserved=True):
    if with_reserved:
        actions = list(actions) + _reserved_actions(max((a["id"] for a in actions), default=-1) + 1)
    path = tmp_path / name
    doc = {"contexts": contexts or [_context("Global")], "actions": actions}
    if settings:
        doc["settings"] = settings
    path.write_text(json.dumps(doc), encoding="utf-8")
    return path


# --- Format-constant and source-table drift guards --------------------------

def test_format_constants_match_the_engine_header():
    header = (ROOT / "engine" / "include" / "core" / "EngineAction.h").read_text(encoding="utf-8")
    assert "#define ACTION_MAX_ENTRIES {}".format(act.ACTION_MAX_ENTRIES) in header
    assert "#define ACTION_MAX_BINDINGS {}".format(act.ACTION_MAX_BINDINGS) in header
    assert "#define ACTION_MAX_SOURCES_PER_BINDING {}".format(act.ACTION_MAX_SOURCES_PER_BINDING) in header
    assert "#define ACTION_MAX_CONTEXTS {}".format(act.ACTION_MAX_CONTEXTS) in header
    assert "#define ACTION_MAX_DYNAMIC_BINDINGS {}".format(act.ACTION_MAX_DYNAMIC_BINDINGS) in header


def test_gamepad_button_bits_match_the_header():
    """Gamepad buttons are a hardware bit MASK in the header but a bit POSITION
    here (ACTION_OVERLAY.md); this derives the position from the header's own
    mask so the two cannot drift without a test failure."""
    header = (ROOT / "engine" / "include" / "platform" / "PlatformKeys.h").read_text(encoding="utf-8")
    cpp_name = {
        "select": "Select", "l3": "L3", "r3": "R3", "start": "Start", "dpad_up": "DPadUp", "dpad_right": "DPadRight",
        "dpad_down": "DPadDown", "dpad_left": "DPadLeft", "l2": "L2", "r2": "R2", "l1": "L1", "r1": "R1",
        "triangle": "Triangle", "circle": "Circle", "cross": "Cross", "square": "Square",
    }
    import re
    for name, bit in act.GAMEPAD_BUTTON_BIT.items():
        # Anchored to an actual enum-member line (not the "legacy enum had these
        # transposed" comment a few lines above, which names the same buttons
        # with the same "=0x..." shape but no surrounding whitespace).
        match = re.search(r"^\s*{}\s*=\s*(0x[0-9A-Fa-f]+)".format(cpp_name[name]), header, re.MULTILINE)
        assert match, "{} not found in PlatformKeys.h".format(cpp_name[name])
        mask = int(match.group(1), 16)
        assert mask.bit_length() - 1 == bit, "{}: mask 0x{:x} is not bit {}".format(name, mask, bit)


def test_keyboard_key_ordinals_match_the_header():
    import re

    header = (ROOT / "engine" / "include" / "platform" / "PlatformKeys.h").read_text(encoding="utf-8")
    # KeyboardKey's body, from the enum line to its closing brace: one member
    # name per non-comment, non-blank line, in declaration order (== ordinal).
    body = header.split("enum class KeyboardKey", 1)[1].split("{", 1)[1].split("};", 1)[0]
    members = []
    for line in body.splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        match = re.match(r"([A-Za-z][A-Za-z0-9]*)", line)
        if match and match.group(1) != "Count":
            members.append(match.group(1))
    assert members[act.KEYBOARD_KEY["a"]] == "A"
    assert members[act.KEYBOARD_KEY["z"]] == "Z"
    assert members[act.KEYBOARD_KEY["leftshift"]] == "LeftShift"
    assert members[act.KEYBOARD_KEY["escape"]] == "Escape"
    assert members[act.KEYBOARD_KEY["grave"]] == "Grave"


# --- Structural validation ----------------------------------------------------

def test_a_valid_declaration_loads(tmp_path):
    path = _declare(tmp_path, [_action(0, "A")])
    declaration = act.load_declaration(str(path))
    assert declaration["actions"][0]["id"] == 0
    assert declaration["actions"][0]["key"] == "A"
    assert [e["key"] for e in declaration["actions"][-3:]] == list(act.RESERVED_KEYS)
    assert declaration["settings"]["combinationWindow"] == 0.1
    assert declaration["settings"]["opposing"] == "neutral"


def test_sparse_action_ids_rejected(tmp_path):
    path = _declare(tmp_path, [_action(0, "A"), _action(2, "B")])
    with pytest.raises(act.ActionError, match="dense"):
        act.load_declaration(str(path))


def test_duplicate_action_ids_rejected(tmp_path):
    path = _declare(tmp_path, [_action(0, "A"), _action(0, "B")])
    with pytest.raises(act.ActionError, match="duplicate ids"):
        act.load_declaration(str(path))


def test_duplicate_action_keys_rejected(tmp_path):
    path = _declare(tmp_path, [_action(0, "Same"), _action(1, "Same")])
    with pytest.raises(act.ActionError, match="duplicate keys"):
        act.load_declaration(str(path))


def test_action_referencing_unknown_context_rejected(tmp_path):
    path = _declare(tmp_path, [_action(0, "A", context="Nope")])
    with pytest.raises(act.ActionError, match="undeclared context"):
        act.load_declaration(str(path))


def test_duplicate_context_keys_rejected(tmp_path):
    path = _declare(tmp_path, [_action(0, "A")], contexts=[_context("Global"), _context("Global")])
    with pytest.raises(act.ActionError, match="duplicate context keys"):
        act.load_declaration(str(path))


def test_too_many_actions_rejected(tmp_path):
    actions = [_action(i, "A{}".format(i)) for i in range(act.ACTION_MAX_ENTRIES + 1)]
    path = _declare(tmp_path, actions)
    with pytest.raises(act.ActionError, match="maximum"):
        act.load_declaration(str(path))


def test_too_many_bindings_rejected(tmp_path):
    bindings = [{"sources": ["keyboard.a"]} for _ in range(act.ACTION_MAX_BINDINGS + 1)]
    path = _declare(tmp_path, [_action(0, "A", bindings=bindings)])
    with pytest.raises(act.ActionError, match="maximum"):
        act.load_declaration(str(path))


def test_too_many_sources_rejected(tmp_path):
    path = _declare(tmp_path, [_action(0, "A", bindings=[{"sources": ["keyboard.a", "keyboard.b", "keyboard.c", "keyboard.d", "keyboard.e"]}])])
    with pytest.raises(act.ActionError):
        act.load_declaration(str(path))


def test_unknown_source_string_rejected(tmp_path):
    path = _declare(tmp_path, [_action(0, "A", bindings=[{"sources": ["gamepad.circl"]}])])
    with pytest.raises(act.ActionError, match="unknown gamepad source"):
        act.load_declaration(str(path))


def test_composite_on_digital_kind_rejected(tmp_path):
    composite = {"composite": {"up": "keyboard.w", "down": "keyboard.s", "left": "keyboard.a", "right": "keyboard.d"}}
    path = _declare(tmp_path, [_action(0, "A", kind="digital", bindings=[composite])])
    with pytest.raises(act.ActionError, match="composite"):
        act.load_declaration(str(path))


def test_touch_source_rejected(tmp_path):
    path = _declare(tmp_path, [_action(0, "A", bindings=[{"sources": ["touch.front"]}])])
    with pytest.raises(act.ActionError, match="not yet supported"):
        act.load_declaration(str(path))


# --- Reserved engine debug actions ---------------------------------------------
# The mechanism that lets Debug/PerfLogger/Testbed check for input exclusively
# through Action: see docs/subsystems/ACTION.md and EngineAction.h's
# ACTION_RESERVED_* defines.

def test_reserved_constant_matches_the_engine_header():
    header = (ROOT / "engine" / "include" / "core" / "EngineAction.h").read_text(encoding="utf-8")
    assert "#define ACTION_RESERVED_COUNT {}".format(act.ACTION_RESERVED_COUNT) in header
    for key in act.RESERVED_KEYS:
        assert '"{}"'.format(key) in header


def test_missing_reserved_actions_rejected(tmp_path):
    path = _declare(tmp_path, [_action(0, "A")], with_reserved=False)
    with pytest.raises(act.ActionError, match="reserved engine actions"):
        act.load_declaration(str(path))


def test_reserved_actions_out_of_order_rejected(tmp_path):
    """They must be the top three ids, in RESERVED_KEYS order -- engine code
    finds them by position (Engine_Action_DeclaredCount() - 3/-2/-1), not by
    searching for a name, so a title cannot reorder them."""
    reserved = list(reversed(_reserved_actions(1)))
    for i, entry in enumerate(reserved):
        entry["id"] = i + 1
    path = _declare(tmp_path, [_action(0, "A")] + reserved, with_reserved=False)
    with pytest.raises(act.ActionError, match="reserved engine actions, in this order"):
        act.load_declaration(str(path))


def test_reserved_action_wrong_kind_rejected(tmp_path):
    reserved = _reserved_actions(1)
    reserved[0]["kind"] = "scalar"
    path = _declare(tmp_path, [_action(0, "A")] + reserved, with_reserved=False)
    with pytest.raises(act.ActionError, match="must be kind 'digital'"):
        act.load_declaration(str(path))


def test_reserved_actions_may_be_the_only_actions_declared(tmp_path):
    """A title with no gameplay actions of its own still builds -- the
    reserved trio has nothing to do with what a title chooses to declare."""
    path = _declare(tmp_path, [], with_reserved=True)
    declaration = act.load_declaration(str(path))
    assert [e["key"] for e in declaration["actions"]] == list(act.RESERVED_KEYS)


def test_reserved_actions_compile_on_every_platform():
    """The exact fixture used everywhere above must itself be viable
    everywhere, or every other test in this file would be resting on a
    fixture that could never actually ship."""
    path_actions = _reserved_actions(0)
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = pathlib.Path(tmp)
        path = _declare(tmp_path, [], with_reserved=False)
        path.write_text(json.dumps({"contexts": [_context("Global")], "actions": path_actions}), encoding="utf-8")
        declaration = act.load_declaration(str(path))
        for platform in ("ps2pal", "ps2ntsc", "win32", "vita", "vitatv", "psp", "nx"):
            act.compile_for_platform(declaration, str(ROOT), platform, [])


# --- Per-platform compilation --------------------------------------------------

def test_binding_unreachable_on_every_platform_rejected(tmp_path):
    path = _declare(tmp_path, [_action(0, "A", bindings=[{"sources": ["mouse.left"]}])])
    declaration = act.load_declaration(str(path))
    with pytest.raises(act.ActionError, match="no binding viable"):
        act.compile_for_platform(declaration, str(ROOT), "ps2pal", [])


def test_binding_unreachable_on_named_platform_only(tmp_path):
    """A keyboard-only action compiles for win32 (has a keyboard) and fails for
    ps2pal (has none) -- the per-platform build failure the spec calls for."""
    path = _declare(tmp_path, [_action(0, "A", bindings=[{"sources": ["keyboard.a"]}])])
    declaration = act.load_declaration(str(path))
    act.compile_for_platform(declaration, str(ROOT), "win32", [])
    with pytest.raises(act.ActionError, match="no binding viable"):
        act.compile_for_platform(declaration, str(ROOT), "ps2pal", [])


def test_vita_handheld_falls_back_past_l3_to_l1(tmp_path):
    """The worked example from the design note: a binding chain preferring L3,
    falling back to L1, is resolved differently per Vita variant."""
    bindings = [{"sources": ["gamepad.l3"]}, {"sources": ["gamepad.l1"]}]
    path = _declare(tmp_path, [_action(0, "Sprint", bindings=bindings)])
    declaration = act.load_declaration(str(path))

    compiled_vita, _ = act.compile_for_platform(declaration, str(ROOT), "vita", [])
    survivors_vita = [b["declaredIndex"] for b in compiled_vita[0]]
    assert survivors_vita == [1], "handheld has no L3; only the L1 fallback should survive"

    compiled_tv, _ = act.compile_for_platform(declaration, str(ROOT), "vitatv", [])
    survivors_tv = [b["declaredIndex"] for b in compiled_tv[0]]
    assert 0 in survivors_tv, "TV pairs a full pad; L3 should survive"


def test_psp_has_no_right_stick(tmp_path):
    path = _declare(tmp_path, [_action(0, "Look", kind="axis2d", bindings=[{"sources": ["gamepad.stick_right"]}])])
    declaration = act.load_declaration(str(path))
    with pytest.raises(act.ActionError, match="no binding viable"):
        act.compile_for_platform(declaration, str(ROOT), "psp", [])


def test_threshold_below_platform_deadzone_is_clamped_and_reported(tmp_path):
    path = _declare(tmp_path, [_action(0, "Fire", kind="scalar", bindings=[{"sources": ["gamepad.trigger_left"], "threshold": 0.01}])])
    declaration = act.load_declaration(str(path))
    warnings = []
    compiled, _ = act.compile_for_platform(declaration, str(ROOT), "win32", warnings)
    deadzone = act.platform_deadzone(str(ROOT), "win32")
    assert compiled[0][0]["threshold"] == deadzone
    assert any("raised to" in w for w in warnings)


def test_immediate_subset_binding_warns_not_fails(tmp_path):
    wide = _action(0, "Pause", bindings=[{"sources": ["keyboard.a", "keyboard.b"]}])
    narrow = _action(1, "OpenMenu", bindings=[{"sources": ["keyboard.a"]}], combination="immediate")
    path = _declare(tmp_path, [wide, narrow])
    declaration = act.load_declaration(str(path))
    warnings = []
    act.compile_for_platform(declaration, str(ROOT), "win32", warnings)
    assert any("declared immediate" in w for w in warnings)


def test_identical_combination_on_two_actions_rejected(tmp_path):
    """Regression for the bug this check exists to catch: two different
    actions bound to the exact same multi-source combination compile without
    error today unless this is checked, and at runtime the earlier-declared
    one always wins the claim (EngineAction.cpp's ResolveClaims ties break by
    declaration order) -- the later one is permanently unreachable, silently,
    on every platform where both bindings are viable."""
    first = _action(0, "Pause", bindings=[{"sources": ["gamepad.select", "gamepad.start"]}])
    second = _action(1, "OpenDebugMenu", bindings=[{"sources": ["gamepad.start", "gamepad.select"]}])  # order in the list doesn't matter
    path = _declare(tmp_path, [first, second])
    declaration = act.load_declaration(str(path))
    with pytest.raises(act.ActionError, match="permanently unreachable"):
        act.compile_for_platform(declaration, str(ROOT), "win32", [])


def test_identical_combination_only_flagged_where_both_survive_pruning(tmp_path):
    """The collision is real only where both bindings are actually viable --
    ps2pal has no keyboard, so a gamepad/keyboard pair sharing a combination
    on paper never competes there. win32 has both, so the same declaration
    is rejected there."""
    first = _action(0, "Pause", bindings=[{"sources": ["gamepad.select", "gamepad.start"]}, {"sources": ["keyboard.tab", "keyboard.escape"]}])
    second = _action(1, "OpenDebugMenu", bindings=[{"sources": ["gamepad.circle"]}, {"sources": ["keyboard.tab", "keyboard.escape"]}])
    path = _declare(tmp_path, [first, second])
    declaration = act.load_declaration(str(path))
    act.compile_for_platform(declaration, str(ROOT), "ps2pal", [])  # keyboard candidates pruned; no collision
    with pytest.raises(act.ActionError, match="permanently unreachable"):
        act.compile_for_platform(declaration, str(ROOT), "win32", [])  # both keyboard candidates survive here


def test_shipped_declaration_debug_menu_does_not_collide_with_pause():
    """The bug report this guards: PauseGame and EngineDebugMenu were both
    bound to gamepad.select+gamepad.start, so the debug menu could never win
    the claim against Pause and had no way to open on PS2/PSP/Vita (none of
    which have the keyboard fallback). test_the_shipped_declaration_is_valid
    already exercises compile_for_platform end to end, which would now raise
    on its own if this regressed; this test names the specific fault so a
    future collision here fails with a description instead of a generic one."""
    declaration = act.load_declaration(str(ROOT / "game" / "config" / "actions.json"))
    for platform in ("ps2pal", "ps2ntsc", "win32", "vita", "vitatv", "nx"):
        act.compile_for_platform(declaration, str(ROOT), platform, [])


def test_deferred_subset_binding_gets_the_declared_window(tmp_path):
    wide = _action(0, "Pause", bindings=[{"sources": ["keyboard.a", "keyboard.b"]}])
    narrow = _action(1, "OpenMenu", bindings=[{"sources": ["keyboard.a"]}])
    path = _declare(tmp_path, [wide, narrow], settings={"combinationWindow": 0.25})
    declaration = act.load_declaration(str(path))
    compiled, _ = act.compile_for_platform(declaration, str(ROOT), "win32", [])
    assert compiled[1][0]["waitsForCombination"] is True
    assert compiled[1][0]["combinationWindowSeconds"] == 0.25
    assert compiled[0][0]["waitsForCombination"] is False


def test_subset_detection_is_not_fooled_by_select_being_bit_zero(tmp_path):
    """Regression: an unused source slot is padded to a sentinel value, and
    gamepad.select's own bit position is 0 -- if the sentinel were also
    (gamepad, button, 0), a single-source Start binding would compare equal
    to (not a subset of) a real Start+Select combination, and the exact
    ACTION.md worked example (Pause = Start+Select, OpenMenu = Start) would
    silently stop deferring."""
    wide = _action(0, "Pause", bindings=[{"sources": ["gamepad.start", "gamepad.select"]}])
    narrow = _action(1, "OpenMenu", bindings=[{"sources": ["gamepad.start"]}])
    path = _declare(tmp_path, [wide, narrow])
    declaration = act.load_declaration(str(path))
    compiled, _ = act.compile_for_platform(declaration, str(ROOT), "win32", [])
    assert compiled[1][0]["waitsForCombination"] is True
    # >= 1: OpenMenu's lone "start" also turns out to be a subset of the
    # reserved EngineDebugMenu/EngineDebugOverlayToggle bindings, which
    # legitimately also name "start" -- the regression this guards is that
    # Pause's own dynamicSlot is among the wider siblings found at all.
    assert compiled[0][0]["dynamicSlot"] in compiled[1][0]["widerSiblingDynamicSlots"]


def test_dynamic_slots_are_dense_and_shared_across_actions(tmp_path):
    wide = _action(0, "Pause", bindings=[{"sources": ["keyboard.a", "keyboard.b"]}])
    narrow = _action(1, "OpenMenu", bindings=[{"sources": ["keyboard.a"]}])
    path = _declare(tmp_path, [wide, narrow])
    declaration = act.load_declaration(str(path))
    compiled, _ = act.compile_for_platform(declaration, str(ROOT), "win32", [])
    wide_slot = compiled[0][0]["dynamicSlot"]
    narrow_slot = compiled[1][0]["dynamicSlot"]
    assert wide_slot != 0xFF and narrow_slot != 0xFF
    assert compiled[1][0]["widerSiblingDynamicSlots"] == [wide_slot]


# --- Code generation -----------------------------------------------------------

def test_generated_ids_name_every_action_and_context(tmp_path):
    path = _declare(tmp_path, [_action(0, "First"), _action(1, "Second")], contexts=[_context("Global"), _context("Menu", "everything")])
    declaration = act.load_declaration(str(path))
    out = tmp_path / "ActionIds.h"
    act.emit_ids(declaration, str(out))
    text = out.read_text(encoding="utf-8")
    assert "enum class ActionId : uint8_t" in text
    assert "First = 0," in text
    assert "Second = 1," in text
    for i, key in enumerate(act.RESERVED_KEYS):
        assert "{} = {},".format(key, 2 + i) in text
    assert "Count = 5" in text
    assert "enum class ActionContextId : uint8_t" in text
    assert "Global," in text
    assert "Menu," in text


def test_generated_table_escapes_quoted_text(tmp_path):
    path = _declare(tmp_path, [_action(0, 'A"B')])
    declaration = act.load_declaration(str(path))
    compiled, digest = act.compile_for_platform(declaration, str(ROOT), "win32", [])
    out = tmp_path / "ActionTable.cpp"
    act.emit_table(declaration, compiled, digest, "win32", str(out))
    text = out.read_text(encoding="utf-8")
    assert '\\"' in text


def test_generated_table_digest_is_stable_for_unchanged_declaration(tmp_path):
    path = _declare(tmp_path, [_action(0, "A", bindings=[{"sources": ["gamepad.cross"]}])])
    declaration = act.load_declaration(str(path))
    _, digest1 = act.compile_for_platform(declaration, str(ROOT), "win32", [])
    _, digest2 = act.compile_for_platform(declaration, str(ROOT), "ps2pal", [])
    assert digest1 == digest2, "the digest must not depend on which candidates a platform prunes"


def test_digest_changes_when_a_binding_slot_count_changes(tmp_path):
    path1 = _declare(tmp_path, [_action(0, "A", bindings=[{"sources": ["keyboard.a"]}])], name="a1.json")
    path2 = _declare(tmp_path, [_action(0, "A", bindings=[{"sources": ["keyboard.a"]}, {"sources": ["keyboard.b"]}])], name="a2.json")
    d1 = act.load_declaration(str(path1))
    d2 = act.load_declaration(str(path2))
    assert act.compute_map_digest(d1["actions"]) != act.compute_map_digest(d2["actions"])


def test_the_shipped_declaration_is_valid():
    """Held to every rule above, for every platform the declaration was
    written against, PSP included: "Look" falls back to a dpad composite
    there, the same pattern "Move" already uses, since PSP has neither a
    mouse nor a second stick."""
    declaration = act.load_declaration(str(ROOT / "game" / "config" / "actions.json"))
    assert declaration["actions"]
    assert [e["id"] for e in declaration["actions"]] == list(range(len(declaration["actions"])))
    for platform in ("ps2pal", "ps2ntsc", "win32", "vita", "vitatv", "nx", "psp"):
        act.compile_for_platform(declaration, str(ROOT), platform, [])
