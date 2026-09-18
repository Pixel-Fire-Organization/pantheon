"""Tests for tools/ECS/generate_ecs.py.

Covers three things:
  * golden output parity for the current ECS.json (FGD must match the committed
    editor definition byte-for-byte; header/source match checked-in goldens),
  * that valid data passes validation,
  * that each semantic rule rejects a targeted invalid fixture with exit-worthy
    error (the "fail-loud in CI" contract).
"""

import importlib.util
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
GOLDEN = pathlib.Path(__file__).resolve().parent / "golden"
COMMITTED_FGD = ROOT / "tools" / "trenchbroom" / "games" / "Pantheon" / "Pantheon.fgd"
ECS_JSON = ROOT / "tools" / "ECS" / "ECS.json"


def _load_generator():
    path = ROOT / "tools" / "ECS" / "generate_ecs.py"
    spec = importlib.util.spec_from_file_location("generate_ecs", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gen = _load_generator()


def _read_lf(path):
    # Compare on content, not line endings: git may check the FGD out as CRLF on
    # Windows while the generator always emits LF.
    return pathlib.Path(path).read_text(encoding="utf-8").replace("\r\n", "\n")


def _real_ecs():
    return gen._load_json(ECS_JSON)


# --- golden parity -----------------------------------------------------------

def test_fgd_matches_committed_editor_definition():
    assert gen.generate_fgd(_real_ecs()) == _read_lf(COMMITTED_FGD)


def test_header_matches_golden():
    assert gen.generate_header(_real_ecs()) == _read_lf(GOLDEN / "EcsComponents.h")


def test_source_matches_golden():
    assert gen.generate_source(_real_ecs()) == _read_lf(GOLDEN / "EcsSpawn.cpp")


def test_real_ecs_is_valid():
    gen.validate(_real_ecs())  # must not raise


# --- semantic validation (negative fixtures) --------------------------------

def _base():
    """A minimal valid document that each negative test mutates into a failure."""
    return {
        "hooks": ["Boom"],
        "components": [
            {
                "name": "HealthComponent",
                "description": "hp",
                "properties": [{"name": "hp", "type": "int", "default": 10}],
                "actions": [{"name": "explode", "engineHook": "Boom"}],
            },
            {
                "name": "TeamComponent",
                "description": "team",
                "properties": [
                    {
                        "name": "team",
                        "type": "flags",
                        "default": 1,
                        "options": {"1": "RED", "2": "BLUE"},
                    }
                ],
            },
        ],
        "entities": [
            {
                "classname": "prop_thing",
                "classType": "PointClass",
                "description": "a thing",
                "components": ["HealthComponent", "TeamComponent"],
            }
        ],
    }


def _expect_error(doc, needle):
    with pytest.raises(gen.EcsError) as exc:
        gen.validate(doc)
    assert needle in str(exc.value)


def test_base_fixture_is_valid():
    gen.validate(_base())


def test_duplicate_component_name():
    doc = _base()
    doc["components"].append(dict(doc["components"][0]))
    _expect_error(doc, "duplicate component name")


def test_duplicate_classname():
    doc = _base()
    doc["entities"].append(dict(doc["entities"][0]))
    _expect_error(doc, "duplicate classname")


def test_entity_references_unknown_component():
    doc = _base()
    doc["entities"][0]["components"].append("GhostComponent")
    _expect_error(doc, "unknown component")


def test_unknown_engine_hook():
    doc = _base()
    doc["components"][0]["actions"][0]["engineHook"] = "Undeclared"
    _expect_error(doc, "not declared in the top-level 'hooks'")


def test_cpp_keyword_property_name():
    doc = _base()
    doc["components"][0]["properties"][0]["name"] = "class"
    _expect_error(doc, "reserved C++ keyword")


def test_invalid_identifier():
    doc = _base()
    doc["components"][0]["properties"][0]["name"] = "9lives"
    _expect_error(doc, "not a valid C++ identifier")


def test_flags_without_options():
    doc = _base()
    del doc["components"][1]["properties"][0]["options"]
    _expect_error(doc, "requires a non-empty 'options'")


def test_default_type_mismatch():
    doc = _base()
    doc["components"][0]["properties"][0]["default"] = "not-an-int"
    _expect_error(doc, "does not match type")


def test_flags_default_outside_options():
    doc = _base()
    doc["components"][1]["properties"][0]["default"] = 8  # no option key 8
    _expect_error(doc, "sets bits outside the options")


def test_cross_component_property_collision():
    doc = _base()
    # Give TeamComponent a property named 'hp' too, then compose both on one entity.
    doc["components"][1]["properties"].append({"name": "hp", "type": "int", "default": 0})
    _expect_error(doc, "collides with the same")


def test_unknown_property_type():
    doc = _base()
    doc["components"][0]["properties"][0]["type"] = "vector3"
    _expect_error(doc, "unknown property type")
