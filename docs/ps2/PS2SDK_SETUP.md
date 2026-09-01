# PS2 Toolchain Setup (WSL)

This guide walks through installing the `ps2dev` toolchain inside **WSL2** (Windows Subsystem for Linux), which is the recommended environment for building this engine on Windows.

---

## Prerequisites

You need **WSL2** with an Ubuntu distribution installed. Open PowerShell and run:

```powershell
wsl --install -d Ubuntu
```

Once inside WSL, install the required build dependencies:

```bash
sudo apt update
sudo apt install -y build-essential cmake git make patch wget texinfo flex bison gettext \
    libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev genisoimage python3 python3-pil
```

> `genisoimage` provides the `mkisofs` utility used by the engine's build system to generate bootable `.iso` files.
> `python3-pil` (Pillow) is used by `tools/cook_assets.py` to decode source images when cooking textures. Required for TEXTURE assets.

---

## Installing the PS2 Toolchain

Clone the official `ps2dev` meta-repository and run the automated build:

```bash
git clone https://github.com/ps2dev/ps2dev.git /tmp/ps2dev-build
cd /tmp/ps2dev-build
sudo mkdir -p /usr/local/ps2dev
sudo chown "$USER":"$USER" /usr/local/ps2dev
./build-all.sh
```

This compiles the entire cross-compilation toolchain (EE, IOP, DVP compilers, PS2SDK, and ports). It can take 30+ minutes depending on hardware.

---

## Environment Variables

Add the following to the **end** of your `~/.bashrc`:

```bash
export PS2DEV=/usr/local/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export PATH=$PATH:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin
```

Then reload:

```bash
source ~/.bashrc
```

> **Important**: These variables **must** use `export`. A plain assignment (e.g., `PS2DEV=/usr/local/ps2dev` without `export`) will not propagate to child processes, and CLion's WSL-based CMake invocation will fail with `PS2DEV environment variable is not set!`.

---

## Verification

Confirm the toolchain is installed and on `PATH`:

```bash
echo $PS2DEV
# Expected: /usr/local/ps2dev

ee-gcc --version
# Expected: mips64r5900el-ps2-elf-gcc (GCC) ...
```

Confirm the SDK directory structure:

```bash
ls $PS2SDK/ee/include
# Should list: kernel.h, tamtypes.h, etc.
```

---

## Cloning the Engine

After the toolchain is ready, clone the engine and initialize submodules:

```bash
git clone <repository-url> ps2-engine
cd ps2-engine
git submodule update --init --recursive
```

---

## Next Steps

- **Build the engine**: See the root [README.md](../../README.md) for build instructions.
- **CLion IDE setup**: See [CLION_SETUP.md](../CLION_SETUP.md) for configuring CLion with the WSL toolchain.
