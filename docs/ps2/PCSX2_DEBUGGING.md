# PCSX2 Debugging with Symbol Files

## Overview

When building in **debug** mode, the build system produces a structured `dist/` output
with everything needed for PCSX2 inspection and debugging.

## dist/ Output Layout

### Debug build
```
dist/
  engine.iso          ← bootable PS2 disc image
  iso_contents/       ← the exact files that went into the ISO (for inspection)
  main.elf            ← the linked executable with full DWARF debug info
  main.sym            ← plain-text symbol table for PCSX2's debugger
```

### Release build
```
dist/
  engine.iso
  main.elf
```

## How the .sym File Works

After the ELF is linked, `ee-nm` (PS2 binutils) reads the ELF's `.symtab` and produces
a text file in PCSX2's expected format:

```
<8-digit-hex-address> <symbol_name>
```

Only code and data symbols are included (object types `T t B b D d R r W w`).

## Prerequisites

| Tool | Required | Notes |
|------|----------|-------|
| `ee-nm` | Yes | Part of PS2 binutils, same `bin/` dir as `ee-gcc` |
| `PS2DEV` env var | Yes | Must be set before CMake (enforced by `toolchains/ps2dev.cmake`) |

> `ee-nm` is resolved automatically from the compiler path — no separate `PS2DEV`
> lookup. GCC embeds DWARF debug info into the ELF; `ee-nm` extracts the symbol table
> from it into the plain-text format PCSX2 expects.

## Generated Files

| File | Description |
|------|-------------|
| `dist/<platform>/main.sym` | Symbol table, loaded by PCSX2 |
| `dist/iso_contents/` | Mirror of the ISO's filesystem for inspection (debug only) |
| `tools/gen_sym.cmake` | CMake script that runs `ee-nm` and filters its output |

## Loading Symbols in PCSX2

**Option A — from `.sym` file (ISO workflow):**
1. Boot `dist/<platform>/engine.iso` in PCSX2.
2. Open the debugger (*Debug → Open Debugger*).
3. *Symbols → Load symbols…* → select `dist/<platform>/main.sym`.

**Option B — from ELF directly (ELF workflow):**
1. Load `dist/<platform>/main.elf` directly in PCSX2 (*File → Run ELF…*).
2. PCSX2 reads the `.symtab` automatically — no `.sym` file needed.

## Build Commands

```bash
# Debug build — produces iso, iso_contents/, elf, sym
python3 ./tools/build.py debug pal

# Release build — produces iso, elf only
python3 ./tools/build.py release pal
```

## Implementation Details

Three ordered `POST_BUILD` steps on the ELF target, all guarded by
`DEBUG AND CMAKE_SYSTEM_NAME STREQUAL "Generic"`:

| Step               | Always / Debug only | What it does                                                              |
|--------------------|---------------------|---------------------------------------------------------------------------|
| ① `gen_sym.cmake`  | Debug only          | Runs `ee-nm`, writes `dist/<platform>/main.sym`                           |
| ② `mkisofs`        | Always              | Stages `assets/` → `build/iso_root/`, writes `dist/<platform>/engine.iso` |
| ③ `copy_directory` | Debug only          | Copies `build/iso_root/` → `dist/iso_contents/`                           |

Step ③ runs after step ② so the staging directory is already fully populated.
`tools/gen_sym.cmake` runs `ee-nm --numeric-sort`, then filters the output with
CMake string/regex commands — no shell, no `awk`, no generated temp files.

