#!/usr/bin/env python3
"""Boot a PS2 disc image under the emulator and capture what it did.

Rendering defects on this platform are found by looking at a frame and at the
console log, and by putting the same scene through both backends and diffing
them. Doing that by hand is fiddly enough that it tends not to get done, so it
lives here instead.

    python3 tools/ps2/emu_capture.py dist/ps2pal/engine.iso --log run.log
    python3 tools/ps2/emu_capture.py dist/ps2pal/engine.iso --shot giftag.png
    python3 tools/ps2/emu_capture.py dist/ps2pal/engine.iso --shot ps2gl.png --renderer ps2gl
    python3 tools/ps2/emu_capture.py dist/ps2pal/engine.iso --shot menu.png --press "{F8}"

The emulator is located exactly as `tools/run_target.py` locates it, including
the $PCSX2_PATH override, so there is one answer to "where is the emulator" in
this repository. See docs/ps2/BUILD.md for what the captures are good for.
"""

import argparse
import importlib.util
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
WINDOW_HELPER = pathlib.Path(__file__).resolve().parent / "emu_window.ps1"

PCSX2_PROCESS = "pcsx2-qt"

# PCSX2 reports where it will write snapshots during start-up. It is a
# configured location rather than a fixed one, and it is not necessarily under
# the emulator directory, so it is read back rather than guessed.
SNAPSHOT_DIR_PATTERN = re.compile(r"Snapshots Directory:\s*(.+?)\s*$", re.MULTILINE)

# PCSX2 hands `-gameargs` to the executable without prepending the path it
# booted, so the first token is consumed as argv[0] and the first real option is
# silently eaten. Everything after this placeholder is what the engine parses.
GAMEARGS_ARGV0 = "cdrom0:\\MAIN.ELF;1"

DEFAULT_SETTLE_SECONDS = 20
DEFAULT_SNAPSHOT_KEY = "{F8}"


class CaptureError(Exception):
    """A capture that could not be attempted, with an actionable message."""


def load_run_target():
    """Load the shared launcher module.

    @return The imported `tools/run_target.py` module.
    """
    path = ROOT / "tools" / "run_target.py"
    spec = importlib.util.spec_from_file_location("run_target", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def local_path(path, host=None):
    """Translate a path the emulator reported back into one this host can open.

    The inverse of `run_target.host_path`: hosts that hand the emulator a
    Windows path get a Windows path back in its log, and cannot open it.

    @param path Path as the emulator wrote it.
    @param host Host kind as `run_target.host_kind()` reports it.
    @return A path openable by this interpreter.
    """
    translator = load_run_target().PATH_TRANSLATOR.get(host)
    if not translator:
        return path
    try:
        return subprocess.check_output([translator, "-u", path], text=True).strip()
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return path


def build_gameargs(renderer=None, extra=None):
    """Assemble the launch-argument string handed to the engine.

    @param renderer Backend name for `--renderer`, or None to leave the default.
    @param extra Further arguments as a single string, or None.
    @return The argument string, or None when the engine needs no arguments.
    """
    parts = []
    if renderer:
        parts += ["--renderer", renderer]
    if extra:
        parts += extra.split()
    if not parts:
        return None
    return " ".join([GAMEARGS_ARGV0] + parts)


def build_command(program, image, logfile=None, gameargs=None, headless=False, fastboot=True):
    """Assemble the emulator argument vector.

    Never requests fullscreen: an exclusively-fullscreen surface reads back as a
    black frame through the desktop capture path, which is indistinguishable
    from a backend that drew nothing.

    @param program Resolved emulator path.
    @param image Disc image path, already host-translated.
    @param logfile Where the emulator should write its log, or None.
    @param gameargs Engine launch arguments as one string, or None.
    @param headless Whether to run with no window, for a log-only capture.
    @param fastboot Whether to skip the console boot animation.
    @return The argument vector to spawn.
    """
    cmd = [program, "-batch"]
    if headless:
        cmd.append("-nogui")
    if fastboot:
        cmd.append("-fastboot")
    if logfile:
        cmd += ["-logfile", logfile]
    if gameargs:
        cmd += ["-gameargs", gameargs]
    cmd += ["--", image]
    return cmd


def snapshot_dir(log_text):
    """Read the emulator's snapshot directory out of its log.

    @param log_text Contents of the emulator log.
    @return The directory as the emulator reported it, or None if absent.
    """
    match = SNAPSHOT_DIR_PATTERN.search(log_text)
    return match.group(1) if match else None


def newest_file(root, suffix=".png", newer_than=0.0):
    """Find the most recently written file below a directory.

    @param root Directory to search, recursively.
    @param suffix Extension to match.
    @param newer_than Ignore files not modified after this timestamp.
    @return The path, or None when nothing matched.
    """
    best = None
    best_mtime = newer_than
    for base, _dirs, names in os.walk(root):
        for name in names:
            if not name.lower().endswith(suffix):
                continue
            candidate = os.path.join(base, name)
            try:
                mtime = os.path.getmtime(candidate)
            except OSError:
                continue
            if mtime > best_mtime:
                best, best_mtime = candidate, mtime
    return best


def run_window_helper(args, host):
    """Drive the emulator window through the PowerShell helper.

    @param args Arguments to append after the helper path.
    @param host Host kind as `run_target.host_kind()` reports it.
    @return The helper's standard output.
    @raise CaptureError When the host has no PowerShell, or the helper failed.
    """
    if host not in ("win32", "wsl", "cygwin"):
        raise CaptureError(
            "capturing a frame and sending input need the Windows desktop; "
            "this host can only take a log capture (--log without --shot)."
        )

    powershell = shutil.which("powershell.exe") or shutil.which("powershell")
    if not powershell:
        raise CaptureError("powershell.exe not found; cannot reach the emulator window.")

    rt = load_run_target()
    cmd = [powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", rt.host_path(str(WINDOW_HELPER), host)] + args

    # The helper compiles a small P/Invoke shim, which needs somewhere writable
    # to put the assembly. A Windows process launched from a POSIX shell can
    # inherit a working directory it has no rights to, so both the directory and
    # the temporary location are named explicitly rather than left to chance.
    env = dict(os.environ)
    scratch = rt.host_path(tempfile.gettempdir(), host)
    env["TEMP"] = scratch
    env["TMP"] = scratch

    result = subprocess.run(cmd, capture_output=True, text=True, cwd=str(ROOT), env=env)
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        raise CaptureError(f"emulator window helper failed: {detail}")
    return result.stdout.strip()


def capture(image, shot=None, log=None, renderer=None, game_args=None, press=None,
            settle=DEFAULT_SETTLE_SECONDS, native_shot=False, keep_open=False):
    """Boot an image, let it settle, and capture a frame and/or its log.

    @param image Path to a disc image.
    @param shot Where to write the captured frame, or None to capture none.
    @param log Where to keep the emulator log, or None to discard it.
    @param renderer Backend name for `--renderer`, or None for the default.
    @param game_args Further engine launch arguments as one string, or None.
    @param press Key sequences to send once the title has settled, or None.
    @param settle Seconds to wait after the window appears before acting.
    @param native_shot Whether to ask the emulator for the frame itself, which
           yields the display output without window borders or its overlay, at
           the cost of depending on its screenshot hotkey.
    @param keep_open Whether to leave the emulator running afterwards.
    @return A dict describing what was captured.
    @raise CaptureError When the image, the emulator or a capture step failed.
    """
    rt = load_run_target()
    image = os.path.abspath(image)
    if not os.path.exists(image):
        raise CaptureError(f"no disc image at {image}. Build it first.")

    host = rt.host_kind()
    try:
        program = rt.resolve_launcher(rt.PCSX2, None, host)
    except rt.RunError as exc:
        raise CaptureError(str(exc)) from exc

    # A log is always taken, because it is how the snapshot directory is
    # discovered. It is placed beside the caller's own output rather than in
    # scratch space: the emulator is told where to write in Windows form and the
    # answer is read back in POSIX form, and a shell's virtual /tmp is not
    # guaranteed to be the same directory as the interpreter's.
    log_target = os.path.abspath(log) if log else os.path.join(
        os.path.dirname(os.path.abspath(shot)) if shot else os.getcwd(), ".emu_capture.log")
    os.makedirs(os.path.dirname(log_target), exist_ok=True)
    if os.path.exists(log_target):
        os.remove(log_target)

    headless = shot is None and not press
    cmd = build_command(
        program,
        rt.host_path(image, host),
        logfile=rt.host_path(log_target, host),
        gameargs=build_gameargs(renderer, game_args),
        headless=headless,
    )

    print(f"=== Capturing {os.path.basename(image)} ===")
    if renderer:
        print(f"  renderer: {renderer}")
    if log:
        print(f"  log:      {log_target}")

    started = time.time()
    process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    result = {"log": log_target, "shot": None, "returncode": None}

    try:
        if headless:
            try:
                process.wait(timeout=settle)
            except subprocess.TimeoutExpired:
                pass
            return result

        keys = list(press or [])
        snaps_root = None
        if native_shot:
            keys.append(DEFAULT_SNAPSHOT_KEY)

        helper = ["-ProcessName", PCSX2_PROCESS, "-SettleSeconds", str(settle)]
        if keys:
            helper += ["-Press"] + keys
        if shot and not native_shot:
            os.makedirs(os.path.dirname(os.path.abspath(shot)), exist_ok=True)
            helper += ["-Out", rt.host_path(os.path.abspath(shot), host)]

        detail = run_window_helper(helper, host)
        if detail:
            print(f"  {detail}")

        if shot and native_shot:
            log_text = ""
            if os.path.exists(log_target):
                with open(log_target, "r", encoding="utf-8", errors="replace") as fh:
                    log_text = fh.read()
            reported = snapshot_dir(log_text)
            if not reported:
                raise CaptureError(
                    "the emulator did not report a snapshot directory; "
                    "re-run without --native-shot to capture the window instead."
                )
            snaps_root = local_path(reported, host)
            produced = newest_file(snaps_root, ".png", started)
            if not produced:
                raise CaptureError(
                    f"no snapshot appeared under {snaps_root}; the screenshot hotkey may be "
                    f"rebound. Re-run without --native-shot to capture the window instead."
                )
            os.makedirs(os.path.dirname(os.path.abspath(shot)), exist_ok=True)
            shutil.copyfile(produced, shot)

        if shot:
            result["shot"] = os.path.abspath(shot)
            print(f"  shot:     {result['shot']}")
        return result
    finally:
        if not keep_open:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
            result["returncode"] = process.returncode
        if not log:
            try:
                os.remove(log_target)
            except OSError:
                pass
            result["log"] = None


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Boot a PS2 disc image under the emulator and capture a frame and its log.",
        epilog="Frame capture and input need the Windows desktop; --log alone works anywhere.",
    )
    parser.add_argument("image", help="disc image, e.g. dist/ps2pal/engine.iso")
    parser.add_argument("--shot", help="write a captured frame here")
    parser.add_argument("--log", help="keep the emulator log here")
    parser.add_argument("--renderer", help="engine backend to select, e.g. giftag or ps2gl")
    parser.add_argument("--game-args", help="further engine launch arguments, quoted")
    parser.add_argument("--press", action="append", default=[],
                        help="key to send once settled, in SendKeys form; repeatable")
    parser.add_argument("--settle", type=int, default=DEFAULT_SETTLE_SECONDS,
                        help=f"seconds to wait before capturing (default {DEFAULT_SETTLE_SECONDS})")
    parser.add_argument("--native-shot", action="store_true",
                        help="ask the emulator for the frame instead of reading the window, "
                             "giving display output with no borders or overlay")
    parser.add_argument("--keep-open", action="store_true", help="leave the emulator running")
    args = parser.parse_args(argv)

    if not args.shot and not args.log:
        parser.error("nothing to capture: pass --shot, --log, or both.")

    try:
        capture(
            args.image,
            shot=args.shot,
            log=args.log,
            renderer=args.renderer,
            game_args=args.game_args,
            press=args.press,
            settle=args.settle,
            native_shot=args.native_shot,
            keep_open=args.keep_open,
        )
    except CaptureError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
