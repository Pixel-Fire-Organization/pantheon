"""Tests for tools/achievements.py — the title's achievement declaration.

The declaration feeds the engine and every platform's packaging, so a mistake in
it is a mistake everywhere at once, and the ones that matter are silent: an
identifier with no entry behind it, or two descriptions of one set that drifted.
These tests are weighted towards the rejection paths for that reason.
"""

import importlib.util
import json
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load():
    path = ROOT / "tools" / "achievements.py"
    spec = importlib.util.spec_from_file_location("achievements", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ach = _load()


def _entry(identifier, key, grade="bronze", **over):
    base = {"id": identifier, "key": key, "name": key, "grade": grade}
    base.update(over)
    return base


def _declare(tmp_path, entries):
    path = tmp_path / "achievements.json"
    path.write_text(json.dumps({"achievements": entries}), encoding="utf-8")
    return path


def test_max_entries_matches_the_engine_header():
    """The ceiling is a format constant shared with the record's bitfield, not a
    preference either side may change alone."""
    header = (ROOT / "engine" / "include" / "core" / "EngineAchievement.h").read_text(encoding="utf-8")
    assert "#define ACHV_MAX_ENTRIES {}".format(ach.ACHV_MAX_ENTRIES) in header


def test_a_valid_declaration_loads_in_id_order(tmp_path):
    path = _declare(tmp_path, [_entry(2, "Third"), _entry(0, "First"), _entry(1, "Second")])
    assert [e["id"] for e in ach.load(str(path))] == [0, 1, 2]


def test_sparse_ids_rejected(tmp_path):
    path = _declare(tmp_path, [_entry(0, "A"), _entry(2, "B")])
    with pytest.raises(ach.AchievementError, match="dense"):
        ach.load(str(path))


def test_duplicate_ids_rejected(tmp_path):
    path = _declare(tmp_path, [_entry(0, "A"), _entry(0, "B")])
    with pytest.raises(ach.AchievementError, match="duplicate ids"):
        ach.load(str(path))


def test_duplicate_keys_rejected(tmp_path):
    """Two entries sharing an enumerator would not compile, and the build error
    would name a generated file rather than the declaration behind it."""
    path = _declare(tmp_path, [_entry(0, "Same"), _entry(1, "Same")])
    with pytest.raises(ach.AchievementError, match="duplicate keys"):
        ach.load(str(path))


def test_two_platinums_rejected(tmp_path):
    path = _declare(tmp_path, [_entry(0, "A", "platinum"), _entry(1, "B", "platinum")])
    with pytest.raises(ach.AchievementError, match="platinum"):
        ach.load(str(path))


def test_unknown_grade_rejected(tmp_path):
    path = _declare(tmp_path, [_entry(0, "A", "diamond")])
    with pytest.raises(ach.AchievementError, match="grade"):
        ach.load(str(path))


def test_too_many_rejected(tmp_path):
    entries = [_entry(i, "A{}".format(i)) for i in range(ach.ACHV_MAX_ENTRIES + 1)]
    path = _declare(tmp_path, entries)
    with pytest.raises(ach.AchievementError, match="maximum"):
        ach.load(str(path))


def test_missing_icon_rejected(tmp_path):
    path = _declare(tmp_path, [_entry(0, "A", icon="nope.png")])
    with pytest.raises(ach.AchievementError, match="missing file"):
        ach.load(str(path))


def test_empty_declaration_rejected(tmp_path):
    path = tmp_path / "achievements.json"
    path.write_text(json.dumps({"achievements": []}), encoding="utf-8")
    with pytest.raises(ach.AchievementError, match="no achievements"):
        ach.load(str(path))


def test_platinum_id_reported(tmp_path):
    path = _declare(tmp_path, [_entry(0, "Plat", "platinum"), _entry(1, "B")])
    assert ach.platinum_id(ach.load(str(path))) == 0
    path = _declare(tmp_path, [_entry(0, "A"), _entry(1, "B")])
    assert ach.platinum_id(ach.load(str(path))) is None


def test_generated_ids_name_every_entry(tmp_path):
    path = _declare(tmp_path, [_entry(0, "First", "platinum"), _entry(1, "Second")])
    out = tmp_path / "AchievementIds.h"
    ach.emit_ids(ach.load(str(path)), str(out))
    text = out.read_text(encoding="utf-8")
    assert "enum class AchievementId : uint8_t" in text
    assert "First = 0," in text
    assert "Second = 1," in text
    assert "Count = 2" in text


def test_generated_table_escapes_quoted_text(tmp_path):
    """A name carrying a quote would otherwise end the string literal and break a
    build in a generated file nobody edits."""
    path = _declare(tmp_path, [_entry(0, "A", name='He said "go"', detail="back\\slash")])
    out = tmp_path / "AchievementTable.cpp"
    ach.emit_table(ach.load(str(path)), str(out))
    text = out.read_text(encoding="utf-8")
    assert '\\"go\\"' in text
    assert "back\\\\slash" in text


def test_the_shipped_declaration_is_valid():
    """The title's own set is held to every rule above."""
    entries = ach.load(str(ROOT / "game" / "config" / "achievements.json"))
    assert entries
    assert [e["id"] for e in entries] == list(range(len(entries)))
