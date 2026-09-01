"""Tests for tools/title.py — the title's identity.

Every platform's packaging and every platform's writable-storage location are
built from this one declaration, so a mistake here is wrong in several places at
once and in ways that only show up on the platform nobody was testing. Two of
these fields also name directories, which is why the character rules matter.
"""

import importlib.util
import json
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


title = _load("title", "tools/title.py")


def _declare(tmp_path, **over):
    doc = {
        "developer": "Studio",
        "name": "Game",
        "version": "01.00",
        "ids": {"ps2": "PSEN00001", "vita": "PSEN00001"},
    }
    doc.update(over)
    path = tmp_path / "title.json"
    path.write_text(json.dumps(doc), encoding="utf-8")
    return path


def test_a_valid_declaration_loads(tmp_path):
    declaration = title.load(str(_declare(tmp_path)))
    assert declaration["name"] == "Game"
    assert title.platform_id(declaration, "vita") == "PSEN00001"


@pytest.mark.parametrize("field", ["developer", "name"])
@pytest.mark.parametrize("bad", ["a/b", "a\\b", "a:b", "a*b", "a?b", 'a"b', "a<b", "a>b", "a|b"])
def test_directory_naming_fields_reject_path_characters(tmp_path, field, bad):
    """Both fields name a folder on at least one platform. A separator would
    silently file saves somewhere other than where they are looked for."""
    path = _declare(tmp_path, **{field: bad})
    with pytest.raises(title.TitleError, match="directory name"):
        title.load(str(path))


@pytest.mark.parametrize("field", ["developer", "name"])
def test_empty_naming_fields_rejected(tmp_path, field):
    with pytest.raises(title.TitleError, match="must not be empty"):
        title.load(str(_declare(tmp_path, **{field: ""})))


@pytest.mark.parametrize("bad", ["1.0", "1.00", "01.0", "01-00", "", "abcde"])
def test_version_must_be_two_digits_dot_two(tmp_path, bad):
    with pytest.raises(title.TitleError, match="##.##"):
        title.load(str(_declare(tmp_path, version=bad)))


@pytest.mark.parametrize("bad", ["psen00001", "PSEN0001", "PSEN000012", "PS3N0001A", ""])
def test_platform_ids_must_be_four_letters_then_five_digits(tmp_path, bad):
    with pytest.raises(title.TitleError, match="upper-case letters"):
        title.load(str(_declare(tmp_path, ids={"vita": bad})))


def test_missing_ids_block_rejected(tmp_path):
    path = tmp_path / "title.json"
    path.write_text(json.dumps({"developer": "S", "name": "G", "version": "01.00"}), encoding="utf-8")
    with pytest.raises(title.TitleError, match="ids is required"):
        title.load(str(path))


def test_asking_for_an_undeclared_platform_id_names_the_platform(tmp_path):
    declaration = title.load(str(_declare(tmp_path, ids={"vita": "PSEN00001"})))
    with pytest.raises(title.TitleError, match="ids.ps2"):
        title.platform_id(declaration, "ps2")


def test_malformed_json_is_reported_as_such(tmp_path):
    path = tmp_path / "title.json"
    path.write_text("{ not json", encoding="utf-8")
    with pytest.raises(title.TitleError, match="not valid JSON"):
        title.load(str(path))


def test_the_shipped_declaration_is_valid():
    declaration = title.load(str(ROOT / "game" / "config" / "title.json"))
    assert title.platform_id(declaration, "vita")
    assert title.platform_id(declaration, "ps2")


def test_vita_packaging_takes_its_identity_from_the_shared_declaration():
    """The identity a console shows and the identity a save is filed under come
    from one place; a second copy in the package config would drift."""
    package = json.loads((ROOT / "game" / "config" / "platform" / "vita" / "package.json").read_text(encoding="utf-8"))
    assert "title" not in package

    vp = _load("vita_package", "tools/vita_package.py")
    declaration = title.load(str(ROOT / "game" / "config" / "title.json"))
    config = vp.load_config(str(ROOT / "game" / "config" / "platform" / "vita" / "package.json"))
    assert config["title"]["id"] == title.platform_id(declaration, "vita")
    assert config["title"]["name"] == declaration["name"]
    assert config["title"]["version"] == declaration["version"]
