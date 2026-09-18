#!/usr/bin/env python3
"""Build the engine and game for one or more platforms.

    python3 tools/build.py [debug|release] [pal|ntsc] [--platforms PS2PAL,PS2NTSC]
                           [--container auto|always|never]

The positional region argument is kept for backwards compatibility: `pal` means
`--platforms PS2PAL`. With neither, every platform the active toolchain can
build is built, each into its own dist/<platform>/ bundle.

A toolchain with a pinned container image is built inside it when the host has
no install of its own (`auto`), always (`always`), or never (`never`).
"""
import argparse
import hashlib
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

# Which toolchain file each platform needs. A configure uses one toolchain, so
# platforms are grouped by it and built one group at a time.
TOOLCHAINS = {
    "PS2PAL": "toolchains/ps2dev.cmake",
    "PS2NTSC": "toolchains/ps2dev.cmake",
    "WIN32": "toolchains/mingw-w64.cmake",
    "VITA": "toolchains/vitasdk.cmake",
    "VITATV": "toolchains/vitasdk.cmake",
    "PSP": "toolchains/pspdev.cmake",
    "NX": "toolchains/devkita64.cmake",
}

REGION_ALIAS = {"pal": "PS2PAL", "ntsc": "PS2NTSC"}

# Toolchains that also ship as a pinned container image, for hosts that cannot
# install them. The host install is recognised by an environment variable naming
# its root and a file inside it; the local image is tagged with a hash of its
# Dockerfile, so editing the Dockerfile builds a new image instead of reusing a
# stale one.
CONTAINERS = {
    "toolchains/devkita64.cmake": {
        "dockerfile": "tools/docker/devkita64/Dockerfile",
        "image": "pantheon-devkita64",
        "root_env": "DEVKITPRO",
        "root_default": "/opt/devkitpro",
        "probe": "cmake/Switch.cmake",
    },
}


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Build Pantheon")
    p.add_argument("build_type", nargs="?", default="debug", choices=["debug", "release"])
    p.add_argument("region", nargs="?", default=None, type=str.lower, choices=["pal", "ntsc"],
                   help="Shorthand for --platforms PS2PAL / PS2NTSC")
    p.add_argument("--platforms", default=None,
                   help="Comma-separated platforms (default: everything the toolchain supports)")
    p.add_argument("--container", default="auto", choices=["auto", "always", "never"],
                   help="Build toolchains that have a container image inside it: when the host lacks "
                        "the toolchain (auto), always, or never")
    return p.parse_args(argv)


def resolve_platforms(args):
    if args.platforms:
        return [p.strip().upper() for p in args.platforms.split(",") if p.strip()]
    if args.region:
        return [REGION_ALIAS[args.region]]
    return []  # let CMake use its default list, filtered by the toolchain


def group_by_toolchain(platforms):
    """One configure per toolchain; unknown platforms are reported, not guessed."""
    if not platforms:
        return {TOOLCHAINS["PS2PAL"]: []}  # default list, PS2 toolchain

    groups = {}
    for p in platforms:
        if p not in TOOLCHAINS:
            sys.exit(f"Unknown platform '{p}'. Known: {', '.join(sorted(TOOLCHAINS))}")
        groups.setdefault(TOOLCHAINS[p], []).append(p)
    return groups


def run(cmd, cwd):
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def host_has_toolchain(spec, environ=None):
    """@return Whether the host carries its own install of a containerised toolchain."""
    environ = os.environ if environ is None else environ
    toolchain_root = environ.get(spec["root_env"]) or spec["root_default"]
    return os.path.exists(os.path.join(toolchain_root, spec["probe"]))


def use_container(toolchain, mode, environ=None):
    """@return The container spec to build this toolchain in, or None to build on the host.
    @raise SystemExit When a container is demanded for a toolchain that has none."""
    spec = CONTAINERS.get(toolchain)
    if mode == "never":
        return None
    if spec is None:
        if mode == "always":
            sys.exit(f"{toolchain} has no container image; build it on the host with --container never")
        return None
    if mode == "always":
        return spec
    return None if host_has_toolchain(spec, environ) else spec


def image_tag(root, spec):
    """@return The local image tag: the spec name and the first 12 hex digits of the Dockerfile hash."""
    digest = hashlib.sha256((Path(root) / spec["dockerfile"]).read_bytes()).hexdigest()
    return f"{spec['image']}:{digest[:12]}"


def container_command(root, tag, commands, uid, gid):
    """@return The docker invocation that runs the commands, in order, inside the image.

    The checkout is mounted at the path it has on the host, so paths cached in
    the build tree mean the same thing inside and outside the container, and the
    container runs as the host user, so everything it writes stays owned by them.
    """
    root = str(root)
    script = " && ".join(" ".join(shlex.quote(part) for part in command) for command in commands)
    return ["docker", "run", "--rm",
            "-v", f"{root}:{root}", "-w", root,
            "-u", f"{uid}:{gid}", "-e", "HOME=/tmp",
            tag, "bash", "-c", script]


def ensure_image(root, spec):
    """Build the image for a spec unless this exact Dockerfile has been built before.
    @return The image tag."""
    if not shutil.which("docker"):
        sys.exit("This toolchain is built in a container when the host has none, and docker is not installed.\n"
                 "Install Docker Engine (see docs/nx/BUILD.md), or install the toolchain and pass --container never.")
    tag = image_tag(root, spec)
    present = subprocess.run(["docker", "image", "inspect", tag], cwd=root,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if not present:
        dockerfile = Path(root) / spec["dockerfile"]
        print(f"=== Building container image {tag} ===")
        run(["docker", "build", "-t", tag, "-f", str(dockerfile), str(dockerfile.parent)], root)
    return tag


def build_native(root, args, groups):
    debug_flag = "ON" if args.build_type == "debug" else "OFF"

    for toolchain, platforms in groups.items():
        tag = Path(toolchain).stem.replace("-", "")
        build_dir = f"build/{tag}-{args.build_type}"

        configure = ["cmake", f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
                     f"-DDEBUG={debug_flag}", "-B", build_dir]
        if platforms:
            configure.append("-DPLATFORMS_TO_SUPPORT=" + ";".join(platforms))
        build = ["cmake", "--build", build_dir, "--target", "dist"]

        spec = use_container(toolchain, args.container)
        if spec:
            image = ensure_image(root, spec)
            print(f"=== Configuring and building {platforms or 'all supported platforms'} ({toolchain}) in {image} ===")
            run(container_command(root, image, [configure, build], os.getuid(), os.getgid()), root)
            continue

        print(f"=== Configuring {platforms or 'all supported platforms'} ({toolchain}) ===")
        run(configure, root)
        print(f"=== Building ({args.build_type}) ===")
        run(build, root)


def main():
    args = parse_args()
    root = Path(__file__).resolve().parent.parent
    groups = group_by_toolchain(resolve_platforms(args))

    is_windows = sys.platform == "win32"
    is_wsl = False
    if os.path.exists("/proc/version"):
        with open("/proc/version", "r") as f:
            is_wsl = "microsoft" in f.read().lower()

    print("=== Initialising git submodules ===")

    if is_windows and not is_wsl:
        # The PS2 toolchain and the MinGW cross-compiler both live in WSL, so a
        # Windows host re-invokes the whole build there - one code path for both.
        distro = os.environ.get("PS2_WSL_DISTRO", "Ubuntu")
        print(f"=== Starting build in WSL ({distro}) ===")

        forwarded = [args.build_type]
        if args.region:
            forwarded.append(args.region)
        if args.platforms:
            forwarded += ["--platforms", args.platforms]
        if args.container != "auto":
            forwarded += ["--container", args.container]

        # Every interpolated value is shell-quoted: a checkout path or an
        # argument containing a quote would otherwise end the string it sits in
        # and the rest would be read as shell. The substitution that converts
        # the path is quoted too, or a path with a space is split into two
        # arguments to cd.
        quoted_root = shlex.quote(str(root))
        quoted_args = " ".join(shlex.quote(a) for a in forwarded)
        script = (
            f'cd "$(wslpath -u {quoted_root})" && '
            "git submodule update --init --recursive && "
            f"python3 ./tools/build.py {quoted_args}"
        )
        try:
            subprocess.run(["wsl", "-d", distro, "bash", "-lc", script], check=True)
        except subprocess.CalledProcessError as e:
            print("=== Build Failed ===")
            sys.exit(e.returncode)
        print("=== Build Completed Successfully ===")
        return

    try:
        subprocess.run(["git", "submodule", "update", "--init", "--recursive"], cwd=root, check=True)
        build_native(root, args, groups)
    except subprocess.CalledProcessError as e:
        print("=== Build Failed ===")
        sys.exit(e.returncode)

    print("=== Build Completed Successfully ===")


if __name__ == "__main__":
    main()
