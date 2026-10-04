#!/usr/bin/env python3
"""macOS application bundle builder.

Assembles the one thing a Mac user runs: <Title>.app, carrying the universal
executable, the wgpu-native library, the master archive and the loose shaders the
renderers read at startup. Its identity - name, version, bundle identifier - is
folded in from game/config/title.json, the same declaration every other platform's
package is built from, so what the Dock shows and what a save is filed under cannot
disagree.

The bundle is signed ad hoc. Apple Silicon refuses to run an unsigned binary, and
copying or merging code invalidates whatever signature it had, so every piece is
signed again after assembly. This is not a distribution signature: a bundle that
arrives over a network will still be quarantined until it is notarised.

Usage:
    python3 tools/macos_package.py --print-bundle-name
    python3 tools/macos_package.py --exe dist/macos/game --dylib libwgpu_native.dylib \\
        --rassets dist/cooked/macos/rassets --levels dist/cooked/macos/levels \\
        --shaders dist/cooked/macos/shaders --bundle dist/macos/Pantheon.app
"""

import argparse
import os
import plistlib
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import title as title_declaration

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TITLE = os.path.join(PROJECT_ROOT, "game", "config", "title.json")

MINIMUM_SYSTEM_VERSION = "11.0"
ARCHIVE_NAME = "RASSETS.PS2R"
DYLIB_NAME = "libwgpu_native.dylib"
EXECUTABLE_RPATH = "@executable_path/../Frameworks"


class MacosPackageError(Exception):
    """A bundle this build refuses to produce, naming what is wrong with its inputs."""


def bundle_name(declaration, override=None):
    """@return The bundle's name, without the .app suffix."""
    return override or declaration["name"]


def bundle_identifier(declaration):
    """@return A reverse-DNS identifier derived from the developer and the title."""

    def slug(text):
        return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")

    developer = slug(declaration["developer"])
    name = slug(declaration["name"])
    if not developer or not name:
        raise MacosPackageError("developer '{}' and name '{}' must each contain a letter or digit".format(declaration["developer"], declaration["name"]))
    return "com.{}.{}".format(developer, name)


def bundle_version(declaration):
    """@return The title's ##.## version as the dotted integers a bundle expects."""
    return ".".join(str(int(part)) for part in declaration["version"].split("."))


def info_plist(declaration, name, executable):
    """@return The Info.plist contents as a dictionary."""
    version = bundle_version(declaration)
    return {
        "CFBundleName": name,
        "CFBundleDisplayName": name,
        "CFBundleIdentifier": bundle_identifier(declaration),
        "CFBundleExecutable": executable,
        "CFBundlePackageType": "APPL",
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "CFBundleSupportedPlatforms": ["MacOSX"],
        "LSMinimumSystemVersion": MINIMUM_SYSTEM_VERSION,
        "LSApplicationCategoryType": "public.app-category.games",
        "NSPrincipalClass": "NSApplication",
        "NSHighResolutionCapable": True,
    }


def _run(argv):
    """Run a command given as an argument vector, never a shell string."""
    result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    if result.returncode != 0:
        raise MacosPackageError("{} failed ({}): {}".format(argv[0], result.returncode, result.stderr.strip()))
    return result.stdout


def architectures(path):
    """@return The set of architectures a Mach-O file carries."""
    return set(_run(["lipo", "-archs", path]).split())


def has_rpath(path, rpath):
    """@return True when the Mach-O file lists this run-path entry."""
    lines = _run(["otool", "-l", path]).splitlines()
    for index, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH" and index + 2 < len(lines):
            if lines[index + 2].strip().startswith("path " + rpath + " "):
                return True
    return False


def sign(path):
    """Ad-hoc sign one piece of code."""
    _run(["codesign", "--force", "--sign", "-", "--timestamp=none", path])


def _require_file(label, path):
    if not path or not os.path.isfile(path):
        raise MacosPackageError("{} '{}' is not a file".format(label, path))


def _require_directory(label, path):
    if not path or not os.path.isdir(path):
        raise MacosPackageError("{} '{}' is not a directory".format(label, path))


def check_binaries(exe, dylib):
    """Refuse a bundle whose executable could not load its library.

    @raise MacosPackageError When the library lacks an architecture the executable carries,
        or the executable has no run-path entry pointing at the bundle's Frameworks directory.
    """
    exe_archs = architectures(exe)
    dylib_archs = architectures(dylib)
    missing = sorted(exe_archs - dylib_archs)
    if missing:
        raise MacosPackageError(
            "the executable carries {} but {} lacks {}; the bundle would fail to launch on that architecture".format(
                ", ".join(sorted(exe_archs)), os.path.basename(dylib), ", ".join(missing)))
    if not has_rpath(exe, EXECUTABLE_RPATH):
        raise MacosPackageError("the executable has no run-path entry '{}', so it cannot find {} inside the bundle".format(EXECUTABLE_RPATH, os.path.basename(dylib)))


def _pack_archive(rassets, levels, destination):
    argv = [sys.executable, os.path.join(PROJECT_ROOT, "tools", "pack_master_archive.py"), "--rassets", rassets, "--prefix", "RASSETS"]
    if levels:
        argv += ["--levels", levels]
    argv += ["--dst", destination]
    _run(argv)


def build_bundle(bundle, exe, dylib, declaration, shaders=None, rassets=None, levels=None, archive=None, name=None, do_sign=True):
    """Assemble the application bundle at `bundle`, replacing any bundle already there.

    @param bundle Destination path, ending in .app.
    @param exe The universal executable.
    @param dylib The universal wgpu-native library.
    @param declaration The validated title declaration.
    @param shaders Cooked shader directory, copied to Resources/shaders.
    @param rassets Cooked asset directory to pack into the master archive, with levels.
    @param archive A master archive already packed, copied as is. Used instead of rassets.
    @param name Bundle name override, for an example that is not the title.
    @param do_sign False skips code signing, for a host without the tools.
    @raise MacosPackageError On any missing input or inconsistent binary.
    """
    if not bundle.endswith(".app"):
        raise MacosPackageError("bundle path '{}' must end in .app".format(bundle))
    _require_file("executable", exe)
    _require_file("library", dylib)
    if archive:
        _require_file("archive", archive)
    elif rassets:
        _require_directory("rassets", rassets)
    else:
        raise MacosPackageError("either --archive or --rassets is required")
    if shaders:
        _require_directory("shaders", shaders)

    if do_sign:
        check_binaries(exe, dylib)

    executable = os.path.basename(exe)
    contents = os.path.join(bundle, "Contents")
    if os.path.isdir(bundle):
        shutil.rmtree(bundle)

    macos_dir = os.path.join(contents, "MacOS")
    frameworks_dir = os.path.join(contents, "Frameworks")
    resources_dir = os.path.join(contents, "Resources")
    for directory in (macos_dir, frameworks_dir, resources_dir):
        os.makedirs(directory)

    shutil.copy2(exe, os.path.join(macos_dir, executable))
    shutil.copy2(dylib, os.path.join(frameworks_dir, DYLIB_NAME))

    if archive:
        shutil.copy2(archive, os.path.join(resources_dir, ARCHIVE_NAME))
    else:
        _pack_archive(rassets, levels if levels and os.path.isdir(levels) else None, os.path.join(resources_dir, ARCHIVE_NAME))

    if shaders:
        shutil.copytree(shaders, os.path.join(resources_dir, "shaders"))

    with open(os.path.join(contents, "Info.plist"), "wb") as handle:
        plistlib.dump(info_plist(declaration, bundle_name(declaration, name), executable), handle)

    if do_sign:
        sign(os.path.join(frameworks_dir, DYLIB_NAME))
        sign(bundle)
    return bundle


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build the macOS application bundle")
    parser.add_argument("--title", default=DEFAULT_TITLE, help="title declaration (default game/config/title.json)")
    parser.add_argument("--print-bundle-name", action="store_true", help="print the bundle's name and exit")
    parser.add_argument("--exe", help="universal executable")
    parser.add_argument("--dylib", help="universal libwgpu_native.dylib")
    parser.add_argument("--rassets", help="cooked asset directory to pack")
    parser.add_argument("--levels", help="compiled level directory to pack with the assets")
    parser.add_argument("--archive", help="a master archive already packed")
    parser.add_argument("--shaders", help="cooked shader directory")
    parser.add_argument("--bundle", help="destination, ending in .app")
    parser.add_argument("--name", help="bundle name when it is not the title's")
    parser.add_argument("--no-sign", action="store_true", help="skip code signing")
    args = parser.parse_args(argv)

    try:
        declaration = title_declaration.load(args.title)
        if args.print_bundle_name:
            print(bundle_name(declaration, args.name))
            return 0
        if not (args.exe and args.dylib and args.bundle):
            parser.error("--exe, --dylib and --bundle are required")
        bundle = build_bundle(args.bundle, args.exe, args.dylib, declaration, shaders=args.shaders, rassets=args.rassets, levels=args.levels, archive=args.archive, name=args.name,
                              do_sign=not args.no_sign)
    except (title_declaration.TitleError, MacosPackageError) as error:
        print("macos_package: {}".format(error), file=sys.stderr)
        return 1

    print("macos_package: {}".format(bundle))
    return 0


if __name__ == "__main__":
    sys.exit(main())
