"""Tests for tools/ps2/emu_capture.py - the emulator capture harness.

Weighted towards the things that were wrong before this tool existed and are
invisible until someone looks at a frame: the emulator eats the first launch
argument, an exclusively-fullscreen surface captures as a black rectangle, and
the snapshot directory is configured rather than fixed.
"""

import importlib.util
import os
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load(relative, name):
    path = ROOT / relative
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ec = _load(pathlib.Path("tools") / "ps2" / "emu_capture.py", "emu_capture")
rt = _load(pathlib.Path("tools") / "run_target.py", "run_target")


# --- launch arguments -------------------------------------------------------

def test_gameargs_is_none_when_the_engine_needs_nothing():
    assert ec.build_gameargs() is None
    assert ec.build_gameargs(None, None) is None


def test_gameargs_pads_argv0_so_the_first_option_survives():
    # The emulator does not prepend the booted path, so an unpadded string loses
    # its first token to the engine's argv[0].
    args = ec.build_gameargs(renderer="ps2gl")
    assert args.startswith(ec.GAMEARGS_ARGV0 + " ")
    assert args.split()[1:] == ["--renderer", "ps2gl"]


def test_gameargs_appends_extra_arguments_after_the_renderer():
    args = ec.build_gameargs(renderer="giftag", extra="--scene primitives")
    assert args.split()[1:] == ["--renderer", "giftag", "--scene", "primitives"]


def test_gameargs_pads_argv0_for_extra_arguments_alone():
    args = ec.build_gameargs(extra="--renderer null")
    assert args.split()[0] == ec.GAMEARGS_ARGV0


# --- emulator command -------------------------------------------------------

def test_command_never_requests_fullscreen():
    # A fullscreen surface reads back black through the desktop capture path,
    # which is indistinguishable from a backend that drew nothing.
    cmd = ec.build_command("pcsx2-qt", "Q:\\engine.iso")
    assert "-fullscreen" not in cmd


def test_command_puts_the_image_last_behind_a_separator():
    cmd = ec.build_command("pcsx2-qt", "Q:\\engine.iso")
    assert cmd[-2:] == ["--", "Q:\\engine.iso"]


def test_command_is_headless_only_when_asked():
    assert "-nogui" in ec.build_command("pcsx2-qt", "i.iso", headless=True)
    assert "-nogui" not in ec.build_command("pcsx2-qt", "i.iso", headless=False)


def test_command_carries_the_log_and_game_arguments():
    cmd = ec.build_command("pcsx2-qt", "i.iso", logfile="C:\\run.log", gameargs="argv0 --renderer ps2gl")
    assert cmd[cmd.index("-logfile") + 1] == "C:\\run.log"
    assert cmd[cmd.index("-gameargs") + 1] == "argv0 --renderer ps2gl"


def test_command_omits_absent_options():
    cmd = ec.build_command("pcsx2-qt", "i.iso", logfile=None, gameargs=None, fastboot=False)
    for absent in ("-logfile", "-gameargs", "-fastboot"):
        assert absent not in cmd


# --- snapshot discovery -----------------------------------------------------

def test_snapshot_dir_is_read_from_the_log_not_assumed():
    log = "[    0,0156] Snapshots Directory: Q:\\Games\\pcsx2\\snaps\n[ 0,02] other\n"
    assert ec.snapshot_dir(log) == "Q:\\Games\\pcsx2\\snaps"


def test_snapshot_dir_is_none_when_the_emulator_did_not_say():
    assert ec.snapshot_dir("nothing of interest here\n") is None


def test_newest_file_picks_the_latest_matching_one(tmp_path):
    old = tmp_path / "old.png"
    new = tmp_path / "new.png"
    other = tmp_path / "new.txt"
    for path in (old, new, other):
        path.write_bytes(b"x")
    os.utime(old, (1000, 1000))
    os.utime(new, (2000, 2000))
    os.utime(other, (3000, 3000))

    assert ec.newest_file(str(tmp_path), ".png") == str(new)


def test_newest_file_ignores_snapshots_older_than_this_run(tmp_path):
    stale = tmp_path / "stale.png"
    stale.write_bytes(b"x")
    os.utime(stale, (1000, 1000))

    assert ec.newest_file(str(tmp_path), ".png", newer_than=2000) is None


def test_newest_file_searches_below_the_root(tmp_path):
    nested = tmp_path / "TITLE" / "shot.png"
    nested.parent.mkdir()
    nested.write_bytes(b"x")

    assert ec.newest_file(str(tmp_path), ".png") == str(nested)


# --- refusals ---------------------------------------------------------------

def test_frame_capture_is_refused_where_there_is_no_windows_desktop():
    with pytest.raises(ec.CaptureError) as err:
        ec.run_window_helper([], host="posix")
    assert "--log" in str(err.value)


@pytest.mark.parametrize("host", ["win32", "wsl", "cygwin"])
def test_frame_capture_is_attempted_on_every_windows_shell(host, monkeypatch):
    # Not finding PowerShell is a different refusal from the host being unable
    # to have a desktop at all; only the latter should reject these hosts.
    monkeypatch.setattr(ec.shutil, "which", lambda _name: None)
    with pytest.raises(ec.CaptureError) as err:
        ec.run_window_helper([], host=host)
    assert "powershell" in str(err.value).lower()


def test_capture_refuses_an_image_that_was_never_built(tmp_path):
    with pytest.raises(ec.CaptureError) as err:
        ec.capture(str(tmp_path / "engine.iso"))
    assert "Build it first" in str(err.value)


# --- the helper the driver shells out to ------------------------------------

def test_window_helper_script_ships_beside_the_driver():
    assert ec.WINDOW_HELPER.exists(), "tools/ps2/emu_window.ps1 is missing"
