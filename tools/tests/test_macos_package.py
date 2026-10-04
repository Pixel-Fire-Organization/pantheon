"""Tests for the macOS application bundle builder.

The identity tests run anywhere. The tests that assemble and sign a bundle need Apple's own tools - lipo,
otool, codesign and a C compiler - and are skipped where those are absent, which is every host but a Mac.
"""

import importlib.util
import json
import pathlib
import plistlib
import shutil
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"


def _load(name, relpath):
    sys.path.insert(0, str(TOOLS))
    spec = importlib.util.spec_from_file_location(name, TOOLS / relpath)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


macos_package = _load("macos_package", "macos_package.py")

DECLARATION = {"developer": "Pixel Fire", "name": "Pantheon", "version": "01.00", "ids": {}}

needs_apple_tools = pytest.mark.skipif(
    sys.platform != "darwin" or not all(shutil.which(tool) for tool in ("lipo", "otool", "codesign", "clang")),
    reason="assembling a signed bundle needs Apple's command line tools",
)


def test_bundle_identifier_is_reverse_dns_from_developer_and_title():
    assert macos_package.bundle_identifier(DECLARATION) == "com.pixel-fire.pantheon"


def test_bundle_identifier_survives_punctuation_and_case():
    declaration = dict(DECLARATION, developer="  Acme & Sons, Ltd. ", name="Big Game!! 2")
    assert macos_package.bundle_identifier(declaration) == "com.acme-sons-ltd.big-game-2"


def test_bundle_identifier_refuses_a_name_with_nothing_to_derive_from():
    with pytest.raises(macos_package.MacosPackageError, match="letter or digit"):
        macos_package.bundle_identifier(dict(DECLARATION, name="!!!"))


@pytest.mark.parametrize("declared,expected", [("01.00", "1.0"), ("02.15", "2.15"), ("10.07", "10.7")])
def test_bundle_version_is_dotted_integers(declared, expected):
    assert macos_package.bundle_version(dict(DECLARATION, version=declared)) == expected


def test_bundle_name_is_the_title_unless_overridden():
    assert macos_package.bundle_name(DECLARATION) == "Pantheon"
    assert macos_package.bundle_name(DECLARATION, "primitives") == "primitives"


def test_info_plist_carries_the_identity_and_the_minimum_system():
    plist = macos_package.info_plist(DECLARATION, "Pantheon", "game")
    assert plist["CFBundleExecutable"] == "game"
    assert plist["CFBundleIdentifier"] == "com.pixel-fire.pantheon"
    assert plist["CFBundleShortVersionString"] == plist["CFBundleVersion"] == "1.0"
    assert plist["LSMinimumSystemVersion"] == "11.0"
    assert plist["CFBundlePackageType"] == "APPL"
    assert plistlib.loads(plistlib.dumps(plist)) == plist


def test_the_shipped_title_declaration_yields_a_bundle():
    title = _load("title", "title.py")
    declaration = title.load(str(ROOT / "game" / "config" / "title.json"))
    assert macos_package.bundle_name(declaration)
    assert macos_package.bundle_identifier(declaration).startswith("com.")


def test_main_prints_the_bundle_name(capsys):
    assert macos_package.main(["--print-bundle-name"]) == 0
    assert capsys.readouterr().out.strip() == json.loads((ROOT / "game" / "config" / "title.json").read_text())["name"]


def test_main_prints_the_override_name(capsys):
    assert macos_package.main(["--print-bundle-name", "--name", "example"]) == 0
    assert capsys.readouterr().out.strip() == "example"


def test_a_bundle_path_must_end_in_app(tmp_path):
    with pytest.raises(macos_package.MacosPackageError, match=r"\.app"):
        macos_package.build_bundle(str(tmp_path / "Pantheon"), "x", "y", DECLARATION, archive="z")


def test_a_missing_input_is_named(tmp_path):
    with pytest.raises(macos_package.MacosPackageError, match="executable"):
        macos_package.build_bundle(str(tmp_path / "A.app"), str(tmp_path / "nope"), "y", DECLARATION, archive="z")


def test_an_archive_or_assets_directory_is_required(tmp_path):
    exe = tmp_path / "game"
    exe.write_bytes(b"x")
    with pytest.raises(macos_package.MacosPackageError, match="--archive or --rassets"):
        macos_package.build_bundle(str(tmp_path / "A.app"), str(exe), str(exe), DECLARATION)


def _compile(tmp_path, name, source, archs, extra=()):
    src = tmp_path / (name + ".c")
    src.write_text(source)
    out = tmp_path / name
    argv = ["clang", "-o", str(out), str(src)]
    for arch in archs:
        argv += ["-arch", arch]
    argv += list(extra)
    subprocess.run(argv, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return out


def _fixture(tmp_path, exe_archs, dylib_archs, with_rpath=True):
    exe = _compile(tmp_path, "game", "int main(void){return 0;}", exe_archs, ["-Wl,-rpath,@executable_path/../Frameworks"] if with_rpath else [])
    dylib = _compile(tmp_path, "libwgpu_native.dylib", "int answer(void){return 42;}", dylib_archs, ["-dynamiclib"])
    archive = tmp_path / "RASSETS.PS2R"
    archive.write_bytes(b"archive")
    shaders = tmp_path / "shaders"
    (shaders / "legacy").mkdir(parents=True)
    (shaders / "pbr.wgsl").write_text("@vertex fn vs() {}")
    (shaders / "legacy" / "pbr.vert.glsl").write_text("void main(){}")
    return exe, dylib, archive, shaders


@needs_apple_tools
def test_assembles_a_valid_signed_bundle(tmp_path):
    exe, dylib, archive, shaders = _fixture(tmp_path, ["x86_64"], ["x86_64"])
    bundle = tmp_path / "Pantheon.app"
    macos_package.build_bundle(str(bundle), str(exe), str(dylib), DECLARATION, shaders=str(shaders), archive=str(archive))

    contents = bundle / "Contents"
    assert (contents / "MacOS" / "game").is_file()
    assert (contents / "Frameworks" / "libwgpu_native.dylib").is_file()
    assert (contents / "Resources" / "RASSETS.PS2R").read_bytes() == b"archive"
    assert (contents / "Resources" / "shaders" / "pbr.wgsl").is_file()
    assert (contents / "Resources" / "shaders" / "legacy" / "pbr.vert.glsl").is_file()
    assert plistlib.loads((contents / "Info.plist").read_bytes())["CFBundleIdentifier"] == "com.pixel-fire.pantheon"
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


@needs_apple_tools
def test_a_second_assembly_replaces_the_first(tmp_path):
    exe, dylib, archive, shaders = _fixture(tmp_path, ["x86_64"], ["x86_64"])
    bundle = tmp_path / "Pantheon.app"
    macos_package.build_bundle(str(bundle), str(exe), str(dylib), DECLARATION, archive=str(archive))
    (bundle / "Contents" / "Resources" / "stale.txt").write_text("old")
    macos_package.build_bundle(str(bundle), str(exe), str(dylib), DECLARATION, archive=str(archive))
    assert not (bundle / "Contents" / "Resources" / "stale.txt").exists()


@needs_apple_tools
def test_refuses_a_library_missing_an_architecture_the_executable_carries(tmp_path):
    exe, dylib, archive, shaders = _fixture(tmp_path, ["x86_64", "arm64"], ["x86_64"])
    with pytest.raises(macos_package.MacosPackageError, match="arm64"):
        macos_package.build_bundle(str(tmp_path / "A.app"), str(exe), str(dylib), DECLARATION, archive=str(archive))
    assert not (tmp_path / "A.app").exists()


@needs_apple_tools
def test_refuses_an_executable_that_cannot_find_its_library(tmp_path):
    exe, dylib, archive, shaders = _fixture(tmp_path, ["x86_64"], ["x86_64"], with_rpath=False)
    with pytest.raises(macos_package.MacosPackageError, match="run-path"):
        macos_package.build_bundle(str(tmp_path / "A.app"), str(exe), str(dylib), DECLARATION, archive=str(archive))


@needs_apple_tools
def test_universal_executable_and_library_are_accepted(tmp_path):
    exe, dylib, archive, shaders = _fixture(tmp_path, ["x86_64", "arm64"], ["x86_64", "arm64"])
    bundle = tmp_path / "U.app"
    macos_package.build_bundle(str(bundle), str(exe), str(dylib), DECLARATION, archive=str(archive))
    assert macos_package.architectures(str(bundle / "Contents" / "MacOS" / "game")) == {"x86_64", "arm64"}
