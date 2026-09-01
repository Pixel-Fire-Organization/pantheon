"""Tests for tools/build.py's container path.

The decision that matters is the silent one: a host with its own toolchain must
never be pushed into the container, and a host without one must never try to
configure against a toolchain root that is not there. The docker invocation is
checked for the two properties the build tree depends on - the checkout mounted
at its own path, and the host user owning what the container writes.
"""

import importlib.util
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load():
    path = ROOT / "tools" / "build.py"
    spec = importlib.util.spec_from_file_location("engine_build", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


build = _load()
NX = "toolchains/devkita64.cmake"


def test_every_container_toolchain_is_a_known_toolchain():
    assert set(build.CONTAINERS) <= set(build.TOOLCHAINS.values())


def test_every_container_dockerfile_exists_and_pins_its_base_by_digest():
    for spec in build.CONTAINERS.values():
        dockerfile = ROOT / spec["dockerfile"]
        assert dockerfile.exists()
        base = next(line for line in dockerfile.read_text(encoding="utf-8").splitlines() if line.startswith("FROM "))
        assert "@sha256:" in base, "an unpinned base image changes under the build without the Dockerfile changing"


def test_auto_uses_the_host_toolchain_when_present(tmp_path):
    (tmp_path / "cmake").mkdir()
    (tmp_path / "cmake" / "Switch.cmake").write_text("", encoding="utf-8")
    assert build.use_container(NX, "auto", {"DEVKITPRO": str(tmp_path)}) is None


def test_auto_uses_the_container_when_the_host_has_none(tmp_path):
    assert build.use_container(NX, "auto", {"DEVKITPRO": str(tmp_path / "absent")}) is build.CONTAINERS[NX]


def test_never_and_always_override_the_host(tmp_path):
    assert build.use_container(NX, "never", {"DEVKITPRO": str(tmp_path / "absent")}) is None
    (tmp_path / "cmake").mkdir()
    (tmp_path / "cmake" / "Switch.cmake").write_text("", encoding="utf-8")
    assert build.use_container(NX, "always", {"DEVKITPRO": str(tmp_path)}) is build.CONTAINERS[NX]


def test_toolchains_without_an_image_build_on_the_host():
    assert build.use_container("toolchains/pspdev.cmake", "auto", {}) is None


def test_always_refuses_a_toolchain_without_an_image():
    with pytest.raises(SystemExit):
        build.use_container("toolchains/pspdev.cmake", "always", {})


def test_image_tag_follows_the_dockerfile(tmp_path):
    dockerfile = tmp_path / "Dockerfile"
    dockerfile.write_text("FROM a\n", encoding="utf-8")
    spec = {"dockerfile": "Dockerfile", "image": "probe"}
    first = build.image_tag(tmp_path, spec)
    assert first == build.image_tag(tmp_path, spec)
    dockerfile.write_text("FROM b\n", encoding="utf-8")
    assert build.image_tag(tmp_path, spec) != first
    assert first.startswith("probe:")


def test_container_command_mounts_the_checkout_at_its_own_path_as_the_host_user():
    cmd = build.container_command("/mnt/q/repo", "probe:abc", [["cmake", "-B", "build dir"], ["cmake", "--build", "build dir"]], 1000, 1000)
    assert cmd[:3] == ["docker", "run", "--rm"]
    assert cmd[cmd.index("-v") + 1] == "/mnt/q/repo:/mnt/q/repo"
    assert cmd[cmd.index("-w") + 1] == "/mnt/q/repo"
    assert cmd[cmd.index("-u") + 1] == "1000:1000"
    assert "probe:abc" in cmd
    assert cmd[-1] == "cmake -B 'build dir' && cmake --build 'build dir'"


def test_container_mode_is_forwarded_when_re_entering_wsl():
    args = build.parse_args(["debug", "--platforms", "NX", "--container", "never"])
    assert args.container == "never"
    assert build.parse_args(["debug"]).container == "auto"
