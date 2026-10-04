"""Tests for the shader cook and its packaging gate.

The lint is what keeps one WGSL source valid for Vulkan, DX12 and Metal and one GLSL
source valid for every desktop driver, so these tests care most about it refusing the
right things. A lint that only ever passes converts a portability error into a black
screen on somebody else's hardware.
"""

import importlib.util
import json
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
SHADERS = ROOT / "assets" / "engine" / "shaders"


def _load(name, relpath):
    sys.path.insert(0, str(TOOLS))
    spec = importlib.util.spec_from_file_location(name, TOOLS / relpath)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


cook_shaders = _load("cook_shaders", "cook_shaders.py")
validate_cooked = _load("validate_cooked", "validate_cooked.py")

WGSL_OK = b"@vertex\nfn vs_main() -> @builtin(position) vec4<f32> {\n    return vec4<f32>(0.0);\n}\n"
GLSL_OK = b"void main() {\n}\n"


def _cooklist(dialects):
    return {"platform": "test", "assets": {"SHADER": {"enabled": True, "dialects": list(dialects)}}}


def _full_tree(directory, dialects=("wgsl", "glsl", "glsl_legacy")):
    for dialect in dialects:
        for rel in cook_shaders.SHADER_SET[dialect]:
            path = directory / pathlib.PurePosixPath(rel)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(WGSL_OK if rel.endswith(".wgsl") else GLSL_OK)


def test_dialect_classification():
    assert cook_shaders.dialect_of("pbr.wgsl") == "wgsl"
    assert cook_shaders.dialect_of("pbr.vert.glsl") == "glsl"
    assert cook_shaders.dialect_of("legacy/pbr.vert.glsl") == "glsl_legacy"
    assert cook_shaders.dialect_of("readme.txt") is None


def test_every_shipped_shader_passes_its_own_lint():
    sources = cook_shaders.collect(str(SHADERS))
    assert sources, "assets/engine/shaders is empty"
    for rel, data in sources.items():
        cook_shaders.lint(rel, cook_shaders.normalise(data))


def test_shipped_tree_supplies_every_required_shader():
    sources = cook_shaders.collect(str(SHADERS))
    for dialect, names in cook_shaders.SHADER_SET.items():
        for rel in names:
            assert rel in sources, "{} (dialect {}) is required but absent".format(rel, dialect)


def test_shipped_sources_are_lf_ascii_only():
    for rel, data in cook_shaders.collect(str(SHADERS)).items():
        assert b"\r" not in data, rel
        assert all(b < 128 for b in data), rel


@pytest.mark.parametrize("directive", ["enable f16;", "requires readonly_and_readwrite_storage_textures;", "diagnostic(off, derivative_uniformity);"])
def test_wgsl_outside_the_baseline_is_refused(directive):
    data = (directive + "\n").encode() + WGSL_OK
    with pytest.raises(cook_shaders.ShaderError, match="baseline"):
        cook_shaders.lint("x.wgsl", data)


def test_wgsl_directive_inside_a_comment_is_not_a_directive():
    cook_shaders.lint("x.wgsl", b"// enable f16;\n/* requires foo; */\n" + WGSL_OK)


def test_wgsl_without_an_entry_point_is_refused():
    with pytest.raises(cook_shaders.ShaderError, match="entry point"):
        cook_shaders.lint("x.wgsl", b"fn helper() {}\n")


@pytest.mark.parametrize("line", ["#version 330 core", "  #  version 120"])
def test_glsl_carrying_its_own_version_is_refused(line):
    with pytest.raises(cook_shaders.ShaderError, match="#version"):
        cook_shaders.lint("x.frag.glsl", (line + "\n").encode() + GLSL_OK)


def test_glsl_extension_is_refused():
    with pytest.raises(cook_shaders.ShaderError, match="#extension"):
        cook_shaders.lint("x.frag.glsl", b"#extension GL_ARB_foo : enable\n" + GLSL_OK)


def test_glsl_without_main_is_refused():
    with pytest.raises(cook_shaders.ShaderError, match="main"):
        cook_shaders.lint("x.frag.glsl", b"uniform float u;\n")


def test_glsl_must_be_ascii_but_wgsl_may_be_utf8():
    with pytest.raises(cook_shaders.ShaderError, match="ASCII"):
        cook_shaders.lint("x.frag.glsl", "// café\n".encode("utf-8") + GLSL_OK)
    cook_shaders.lint("x.wgsl", "// café\n".encode("utf-8") + WGSL_OK)


@pytest.mark.parametrize(
    "data,reason",
    [
        (b"", "empty"),
        (b"\xef\xbb\xbf" + WGSL_OK, "byte-order mark"),
        (WGSL_OK + b"\x00", "NUL"),
        (b"\xff\xfe" + WGSL_OK, "UTF-8"),
        (WGSL_OK + b" " * cook_shaders.SHADER_SOURCE_MAX_BYTES, "ceiling"),
    ],
)
def test_malformed_bytes_are_refused_with_the_reason(data, reason):
    with pytest.raises(cook_shaders.ShaderError, match=reason):
        cook_shaders.lint("x.wgsl", data)


def test_unknown_extension_is_refused():
    with pytest.raises(cook_shaders.ShaderError, match="not a shader source"):
        cook_shaders.lint("x.txt", b"x")


def test_cook_normalises_line_endings(tmp_path):
    src = tmp_path / "src"
    _full_tree(src, ("wgsl",))
    (src / "flat.wgsl").write_bytes(WGSL_OK.replace(b"\n", b"\r\n"))
    written, errors = cook_shaders.cook(str(src), str(tmp_path / "out"), ("wgsl",))
    assert not errors
    assert (tmp_path / "out" / "flat.wgsl").read_bytes() == WGSL_OK
    assert sorted(written) == sorted(cook_shaders.SHADER_SET["wgsl"])


def test_cook_refuses_a_missing_required_shader_and_writes_nothing(tmp_path):
    src = tmp_path / "src"
    _full_tree(src, ("wgsl",))
    (src / "pbr.wgsl").unlink()
    out = tmp_path / "out"
    written, errors = cook_shaders.cook(str(src), str(out), ("wgsl",))
    assert written == []
    assert any("pbr.wgsl" in e and "absent" in e for e in errors)
    assert not out.exists() or not any(out.iterdir())


def test_cook_writes_nothing_when_any_one_shader_fails_lint(tmp_path):
    src = tmp_path / "src"
    _full_tree(src, ("wgsl",))
    (src / "shadow.wgsl").write_bytes(b"enable f16;\n" + WGSL_OK)
    out = tmp_path / "out"
    written, errors = cook_shaders.cook(str(src), str(out), ("wgsl",))
    assert written == []
    assert len(errors) == 1 and "shadow.wgsl" in errors[0]
    assert not out.exists() or not any(out.iterdir())


def test_cook_ships_only_the_dialects_the_cook_list_asks_for(tmp_path):
    src = tmp_path / "src"
    _full_tree(src)
    out = tmp_path / "out"
    written, errors = cook_shaders.cook(str(src), str(out), ("wgsl", "glsl"))
    assert not errors
    assert not any(rel.startswith("legacy/") for rel in written)
    assert not (out / "legacy").exists()


def test_cook_removes_a_stale_shader_it_no_longer_ships(tmp_path):
    src = tmp_path / "src"
    _full_tree(src)
    out = tmp_path / "out"
    cook_shaders.cook(str(src), str(out), ("wgsl", "glsl", "glsl_legacy"))
    assert (out / "legacy" / "pbr.frag.glsl").exists()
    cook_shaders.cook(str(src), str(out), ("wgsl", "glsl"))
    assert not (out / "legacy" / "pbr.frag.glsl").exists()
    assert (out / "pbr.frag.glsl").exists()


def test_policy_dialects():
    assert cook_shaders.policy_dialects({"assets": {}}) == ()
    assert cook_shaders.policy_dialects({"assets": {"SHADER": {"enabled": False}}}) == ()
    assert cook_shaders.policy_dialects({"assets": {"SHADER": {"enabled": True}}}) == cook_shaders.DIALECTS
    assert cook_shaders.policy_dialects(_cooklist(["glsl", "wgsl"])) == ("wgsl", "glsl")


def test_main_skips_a_platform_with_no_policy(tmp_path, capsys):
    assert cook_shaders.main(["--platform", "ps2pal", "--dst", str(tmp_path / "out")]) == 0
    assert "not cooked on this platform" in capsys.readouterr().out
    assert not (tmp_path / "out").exists()


def test_main_cooks_the_real_tree_for_win32(tmp_path):
    assert cook_shaders.main(["--platform", "win32", "--dst", str(tmp_path / "out")]) == 0
    assert (tmp_path / "out" / "pbr.wgsl").read_bytes() == (SHADERS / "pbr.wgsl").read_bytes()
    assert (tmp_path / "out" / "legacy" / "pbr.frag.glsl").exists()


def test_validator_passes_a_cooked_tree_and_names_a_missing_shader(tmp_path):
    cooked = tmp_path / "shaders"
    _full_tree(cooked, ("wgsl", "glsl"))
    report = validate_cooked.Report()
    validate_cooked.validate_shaders(str(cooked), _cooklist(["wgsl", "glsl"]), report)
    assert report.ok()
    assert report.checked == len(cook_shaders.SHADER_SET["wgsl"]) + len(cook_shaders.SHADER_SET["glsl"])

    (cooked / "pbr.wgsl").unlink()
    report = validate_cooked.Report()
    validate_cooked.validate_shaders(str(cooked), _cooklist(["wgsl", "glsl"]), report)
    assert not report.ok()
    assert any("pbr.wgsl" in e and "missing" in e for e in report.errors)


def test_validator_flags_a_carriage_return_and_a_lint_failure(tmp_path):
    cooked = tmp_path / "shaders"
    _full_tree(cooked, ("wgsl",))
    (cooked / "flat.wgsl").write_bytes(WGSL_OK.replace(b"\n", b"\r\n"))
    (cooked / "shadow.wgsl").write_bytes(b"enable f16;\n" + WGSL_OK)
    report = validate_cooked.Report()
    validate_cooked.validate_shaders(str(cooked), _cooklist(["wgsl"]), report)
    assert any("flat.wgsl" in e and "carriage return" in e for e in report.errors)
    assert any("shadow.wgsl" in e and "baseline" in e for e in report.errors)


def test_validator_ignores_shaders_when_the_platform_has_no_policy(tmp_path):
    report = validate_cooked.Report()
    validate_cooked.validate_shaders(str(tmp_path / "absent"), {"assets": {}}, report)
    assert report.ok() and report.checked == 0


def test_win32_cook_list_declares_its_shader_policy():
    cooklist = json.loads((ROOT / "engine" / "config" / "win32" / "cooklist.json").read_text())
    assert cook_shaders.policy_dialects(cooklist) == cook_shaders.DIALECTS


def test_cooklist_schema_accepts_the_shader_policy():
    jsonschema = pytest.importorskip("jsonschema")
    schema = json.loads((ROOT / "tools" / "schemas" / "cooklist.schema.json").read_text())
    cooklist = json.loads((ROOT / "engine" / "config" / "win32" / "cooklist.json").read_text())
    jsonschema.validate(cooklist, schema)
    bad = {"platform": "x", "assets": {"SHADER": {"enabled": True, "dialects": ["hlsl"]}}}
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(bad, schema)
