"""Tests for tools/run_target.py - the distribution artifact launcher.

Weighted towards the parts that have already been wrong once: the emulator is
chosen from the artifact extension, and Vita3K takes its package as a positional
argument rather than behind a --run flag that does not exist.
"""

import importlib.util
import os
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load():
    path = ROOT / "tools" / "run_target.py"
    spec = importlib.util.spec_from_file_location("run_target", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


rt = _load()


@pytest.fixture
def clean_env(monkeypatch):
    for var in rt.ENV_OVERRIDE.values():
        monkeypatch.delenv(var, raising=False)


# --- dispatch ---------------------------------------------------------------

@pytest.mark.parametrize("artifact,expected", [
    ("dist/ps2pal/engine.iso", rt.PCSX2),
    ("dist/vita/PSEN00001.vpk", rt.VITA3K),
    ("dist/win32/game.exe", rt.NATIVE),
    ("dist/psp/EBOOT.PBP", rt.PPSSPP),
    ("dist/nx/game.nro", rt.RYUJINX),
    ("ENGINE.ISO", rt.PCSX2),
])
def test_launcher_chosen_from_extension(artifact, expected):
    assert rt.launcher_for(artifact) == expected


def test_named_launcher_overrides_the_extension():
    """A PSP disc image and a PlayStation 2 disc image share an extension, so a
    caller that knows which platform it built for names the launcher."""
    assert rt.launcher_for("dist/psp/game.iso", rt.PPSSPP) == rt.PPSSPP
    assert rt.launcher_for("dist/ps2pal/engine.iso") == rt.PCSX2


def test_unknown_named_launcher_is_refused():
    with pytest.raises(rt.RunError, match="unknown launcher"):
        rt.launcher_for("dist/psp/game.iso", "nintendo64")


@pytest.mark.parametrize("artifact", ["dist/thing.elf", "dist/thing", "a.tar.gz"])
def test_unknown_extension_is_refused(artifact):
    with pytest.raises(rt.RunError, match="do not know how to launch"):
        rt.launcher_for(artifact)


# --- launcher resolution ----------------------------------------------------

def test_environment_override_wins_over_search_paths(tmp_path, monkeypatch, clean_env):
    fake = tmp_path / "Vita3K"
    fake.write_text("", encoding="utf-8")
    monkeypatch.setenv("VITA3K_PATH", str(fake))
    assert rt.resolve_launcher(rt.VITA3K, host="win32") == str(fake)


def test_explicit_path_wins_over_environment(tmp_path, monkeypatch, clean_env):
    explicit = tmp_path / "explicit"
    explicit.write_text("", encoding="utf-8")
    other = tmp_path / "from-env"
    other.write_text("", encoding="utf-8")
    monkeypatch.setenv("PCSX2_PATH", str(other))
    assert rt.resolve_launcher(rt.PCSX2, explicit=str(explicit), host="win32") == str(explicit)


def test_missing_launcher_names_every_path_searched(monkeypatch, clean_env):
    monkeypatch.setattr(rt.shutil, "which", lambda _name: None)
    with pytest.raises(rt.RunError) as exc:
        rt.resolve_launcher(rt.VITA3K, host="win32")

    message = str(exc.value)
    assert "VITA3K_PATH" in message
    for candidate in rt.SEARCH_PATHS[rt.VITA3K]["win32"]:
        assert candidate in message


def test_missing_launcher_reports_a_bad_override(tmp_path, monkeypatch, clean_env):
    monkeypatch.setattr(rt.shutil, "which", lambda _name: None)
    monkeypatch.setenv("VITA3K_PATH", str(tmp_path / "nope"))
    with pytest.raises(rt.RunError) as exc:
        rt.resolve_launcher(rt.VITA3K, host="win32")
    assert "$VITA3K_PATH" in str(exc.value)


def test_native_target_needs_no_launcher():
    assert rt.resolve_launcher(rt.NATIVE, host="posix") is None


# --- command assembly -------------------------------------------------------

def test_vita3k_takes_the_package_positionally():
    """Vita3K has no --run option; -r takes an installed title id, not a path.
    Passing either would fail on every machine."""
    cmd = rt.build_command(rt.VITA3K, "/opt/Vita3K", "Q:\\dist\\vita\\PSEN00001.vpk", host="wsl")
    assert cmd == ["/opt/Vita3K", "Q:\\dist\\vita\\PSEN00001.vpk"]
    assert "--run" not in cmd
    assert "-r" not in cmd


def test_pcsx2_runs_batch_and_portable():
    cmd = rt.build_command(rt.PCSX2, "/opt/pcsx2", "/dist/engine.iso", host="posix")
    assert cmd[0] == "/opt/pcsx2"
    assert "-portable" in cmd
    assert cmd[-2:] == ["-batch", "/dist/engine.iso"]


def test_pcsx2_omits_portable_on_macos():
    cmd = rt.build_command(rt.PCSX2, "/PCSX2", "/dist/engine.iso", host="darwin")
    assert "-portable" not in cmd


def test_ryujinx_takes_the_container_positionally():
    cmd = rt.build_command(rt.RYUJINX, "/mnt/c/riujinx/Ryujinx.exe", "Q:\\dist\\nx\\game.nro", host="wsl")
    assert cmd == ["/mnt/c/riujinx/Ryujinx.exe", "Q:\\dist\\nx\\game.nro"]


def test_ryujinx_is_found_through_its_override(tmp_path, monkeypatch, clean_env):
    emulator = tmp_path / "Ryujinx.exe"
    emulator.write_bytes(b"")
    monkeypatch.setenv("RYUJINX_PATH", str(emulator))
    assert rt.resolve_launcher(rt.RYUJINX, host="posix") == str(emulator)


def test_native_target_is_launched_directly():
    assert rt.build_command(rt.NATIVE, None, "Q:\\dist\\win32\\game.exe", host="wsl") == ["Q:\\dist\\win32\\game.exe"]


# --- end to end -------------------------------------------------------------

def test_run_spawns_the_assembled_command(tmp_path, monkeypatch, clean_env):
    vpk = tmp_path / "PSEN00001.vpk"
    vpk.write_bytes(b"")
    emulator = tmp_path / "Vita3K"
    emulator.write_text("", encoding="utf-8")
    monkeypatch.setenv("VITA3K_PATH", str(emulator))
    monkeypatch.setattr(rt, "host_path", lambda p, host=None: p)

    spawned = []
    rt.run(str(vpk), spawner=spawned.append)

    assert len(spawned) == 1
    assert spawned[0] == [str(emulator), str(vpk)]


def test_run_refuses_a_missing_artifact(tmp_path):
    with pytest.raises(rt.RunError, match="no artifact at"):
        rt.run(str(tmp_path / "absent.vpk"), spawner=lambda _cmd: None)


def test_main_returns_nonzero_when_the_launcher_is_missing(tmp_path, monkeypatch, clean_env):
    vpk = tmp_path / "PSEN00001.vpk"
    vpk.write_bytes(b"")
    monkeypatch.setattr(rt.shutil, "which", lambda _name: None)
    monkeypatch.setattr(rt.os.path, "exists", lambda p: str(p) == str(vpk))
    assert rt.main([str(vpk)]) == 1


# --- host translation -------------------------------------------------------

def test_host_path_is_untouched_off_wsl():
    assert rt.host_path("/dist/engine.iso", host="posix") == "/dist/engine.iso"
    assert rt.host_path("C:\\x.iso", host="win32") == "C:\\x.iso"


def test_host_path_converts_under_wsl(monkeypatch):
    monkeypatch.setattr(rt.subprocess, "check_output", lambda *a, **k: "Q:\\dist\\engine.iso\n")
    assert rt.host_path("/mnt/q/dist/engine.iso", host="wsl") == "Q:\\dist\\engine.iso"


def test_host_path_falls_back_when_wslpath_is_absent(monkeypatch):
    def boom(*_a, **_k):
        raise FileNotFoundError
    monkeypatch.setattr(rt.subprocess, "check_output", boom)
    assert rt.host_path("/mnt/q/x.iso", host="wsl") == "/mnt/q/x.iso"


# --- Git Bash ---------------------------------------------------------------
# It reports "cygwin", which fell through to the bare-POSIX branch: no search
# paths, so no emulator was ever found on a shell the build documents as
# supported.

@pytest.mark.parametrize("platform", ["cygwin", "msys"])
def test_git_bash_is_recognised_as_its_own_host(platform, monkeypatch):
    monkeypatch.setattr(rt.sys, "platform", platform)
    assert rt.host_kind() == "cygwin"


def test_git_bash_searches_the_windows_locations(monkeypatch, tmp_path):
    fake = tmp_path / "pcsx2-qt.exe"
    fake.write_text("")
    monkeypatch.setitem(rt.SEARCH_PATHS[rt.PCSX2], "cygwin", [str(fake)])
    assert rt.resolve_launcher(rt.PCSX2, host="cygwin") == str(fake)


def test_git_bash_and_wsl_look_in_the_same_places():
    assert rt.SEARCH_PATHS[rt.PCSX2]["cygwin"] == rt.SEARCH_PATHS[rt.PCSX2]["wsl"]
    assert rt.SEARCH_PATHS[rt.VITA3K]["cygwin"] == rt.SEARCH_PATHS[rt.VITA3K]["wsl"]


def test_host_path_translates_with_cygpath_under_git_bash(monkeypatch):
    seen = {}

    def fake_check_output(cmd, *_args, **_kwargs):
        seen["tool"] = cmd[0]
        return "WINDOWS-FORM\n"

    monkeypatch.setattr(rt.subprocess, "check_output", fake_check_output)
    assert rt.host_path("/q/dist/engine.iso", host="cygwin") == "WINDOWS-FORM"
    assert seen["tool"] == "cygpath"
