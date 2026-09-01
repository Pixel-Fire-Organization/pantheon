"""Tests for the shader parameter guard in tools/compile_shader.py.

An attribute a shader never uses is optimised away, and the renderer - which
binds parameters by name - then fails to start. That failed silently on hardware
once; these cover the build-time check that now catches it.
"""

import importlib.util
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load():
    spec = importlib.util.spec_from_file_location("compile_shader", ROOT / "tools" / "compile_shader.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


cs = _load()


def _blob(*names):
    """A stand-in for a compiled shader carrying NUL-delimited parameter names."""
    out = b"GXP" + bytes([0])
    for name in names:
        out += bytes([0]) + name.encode("ascii") + bytes([0])
    return out


def test_no_requirements_is_satisfied():
    assert cs.missing_parameters(_blob("aPosition"), []) == []


def test_present_parameters_are_satisfied():
    blob = _blob("aPosition", "aTexcoord", "aColor", "uViewProj")
    assert cs.missing_parameters(blob, ["aPosition", "aColor", "uViewProj"]) == []


def test_an_optimised_away_parameter_is_reported():
    """The original defect: aNormal was bound but unused, so it vanished."""
    blob = _blob("aPosition", "aTexcoord", "aColor", "uViewProj")
    assert cs.missing_parameters(blob, ["aPosition", "aNormal"]) == ["aNormal"]


def test_every_missing_parameter_is_reported_in_order():
    blob = _blob("aPosition")
    assert cs.missing_parameters(blob, ["aNormal", "aPosition", "uViewProj"]) == ["aNormal", "uViewProj"]


def test_a_partial_name_does_not_count_as_present():
    """Names are matched whole: aColor must not satisfy a requirement for Color."""
    assert cs.missing_parameters(_blob("aColor"), ["Color"]) == ["Color"]


def test_the_shipped_vertex_shader_declares_what_the_backend_binds():
    """The backend binds these by name in engine/src/platform/vita/renderer/Gxm.cpp."""
    source = (ROOT / "engine" / "config" / "vita" / "renderer" / "shaders" / "scene_v.cg").read_text(encoding="utf-8")
    for name in ("aPosition", "aTexcoord", "aColor", "uViewProj"):
        assert name in source, name
    assert "aNormal" not in source, "the backend no longer binds a normal attribute"
