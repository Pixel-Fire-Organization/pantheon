# ISO Generation

The build system assembles a bootable PS2 disc image (`.iso`) from the compiled ELF, a generated `SYSTEM.CNF`, and the
contents of `assets/` — minus any entries on the **ISO content blacklist**.

---

## How It Works

The per-platform `iso-<platform>` CMake target (`iso-ps2pal`, `iso-ps2ntsc`) performs the following steps in order:

1. **Wipe** the staging directory (`<build>/iso_root/`) to eliminate stale files from previous builds.
2. **Copy** `assets/` into the staging directory, honouring the [ISO Content Blacklist](#iso-content-blacklist).
3. **Copy** the compiled ELF into the staging directory as `${APP_SERIAL}` (see [USERETAILNAME](#useretailname-option)
   below).
4. **Copy** the auto-generated `SYSTEM.CNF` into the staging directory.
5. **Run** `mkisofs` / `genisoimage` to produce `dist/<APP_ISO_NAME>.iso`.

> **Requirement**: `genisoimage` (which provides `mkisofs`) must be installed and on `PATH`.  
> If it is not found, the target still exists as a no-op so `python3 tools/build.py` does not fail.

---

## `USERETAILNAME` Option

Defined in the root `CMakeLists.txt`:

```cmake
option(USERETAILNAME "Changes the executable's name to the retail scheme." OFF)
```

| Value             | `APP_SERIAL` (ELF name on disc)                    | Volume label |
|:------------------|:---------------------------------------------------|:-------------|
| `OFF` *(default)* | `main.elf`                                         | *(not set)*  |
| `ON`              | `SLHB_000.00` *(or custom via `-DAPP_SERIAL=...`)* | `SLHB-00000` |

When **OFF**, the ELF is placed on the disc as `main.elf` and `SYSTEM.CNF` boots it by that name.  
When **ON**, the ELF is renamed to the PS2 serial format (`XXYY_NNN.NN`). This is required for OPL and retail disc
managers that parse the `BOOT2` field expecting a proper serial.

Enable it at configure time:

```sh
cmake -DUSERETAILNAME=ON -DAPP_SERIAL=SLES_508.77 ...
```

---

## `SYSTEM.CNF`

`SYSTEM.CNF` is **not** a static file. It is generated dynamically by `CMakeLists.txt` at configure time so that the
`BOOT2` path always reflects the current value of `APP_SERIAL` (e.g. `BOOT2 = cdrom0:\SLHB_000.00;1` when
`USERETAILNAME=ON`, or `BOOT2 = cdrom0:\main.elf;1` by default).

**The boot path needs its separator.** The console's loader takes
`cdrom0:\NAME;1` — device, backslash, file, version. Written without the
backslash the loader reports that it could not open the file and then jumps
through a null pointer, so the failure presents as a corrupt executable rather
than as a malformed path. Nothing in the image is wrong when this happens; only
the one line naming it is.

---

## Adding Assets to the Disc

Place any file that must be present verbatim on the PS2 disc directly under:

```
assets/
```

(not inside `assets/maps/`, `assets/textures/`, or `assets/models/` — those three
are consumed by the level and cook pipelines instead). All other contents are
automatically staged into the ISO **unless** they are listed in the
[ISO Content Blacklist](#iso-content-blacklist).

> **Note**: `assets/textures/` and `assets/models/` hold raw asset sources, some
> paired with a JSON descriptor (see `assets/README.md`). Descriptor pairs are
> cooked to `.ps2a` by `cook-<platform>` and written to
> `dist/cooked/<platform>/rassets/`. Only `rassets/` needs to be on disc;
> `textures/` and `models/` are excluded by the blacklist, and `maps/` is level
> source consumed by `compile-levels-<platform>` into `dist/cooked/<platform>/levels/`, staged separately.

---

## ISO Content Blacklist

The blacklist prevents specific files or directories inside `assets/` from being included in the disc image. It is
defined near the top of `engine/config/ps2/platform.cmake`:

```cmake
# ─────────────────────────────────────────────────────────────────────────────
# ISO Content Blacklist
# Add folder or file names (relative to assets/) that should NOT be included
# on the disc image.  maps/, textures/ and models/ hold source content — only
# the compiled containers produced by the cook and level stages are needed at
# runtime. engine/ holds the built-in font, a cook_assets.py input only.
# ─────────────────────────────────────────────────────────────────────────────
set(ISO_CONTENT_BLACKLIST "maps;textures;models;engine")
```

### Rules

| Rule                                             | Detail                                                                                                               |
|:-------------------------------------------------|:---------------------------------------------------------------------------------------------------------------------|
| **Name-based matching**                          | Each entry is matched against the file or directory name (not the full path).                                        |
| **Case-sensitive**                               | Matching follows the filesystem on the build host (case-sensitive on Linux/WSL).                                     |
| **Applies at staging time**                      | Exclusion happens when files are copied into `iso_root/`, before `mkisofs` runs.                                     |
| **Does not affect cooking or level compilation** | The `cook-<platform>` and `compile-levels-<platform>` targets always read from `assets/` directly, regardless of the blacklist. |

### Adding an Entry

Append the name to the list in `engine/config/ps2/platform.cmake`:

```cmake
set(ISO_CONTENT_BLACKLIST "maps;textures;models;engine;DEBUG_LOGS") # DEBUG_LOGS: not needed at runtime
```

No other files need to be modified. The next build will exclude the new entry from the disc image.

---

## Implementation Detail

The copy-with-exclusion step is handled by `tools/copy_iso_files.cmake`. It receives `SRC`, `DST`, and `BLACKLIST` as
CMake `-D` variables and uses CMake's built-in `file(COPY ... PATTERN ... EXCLUDE)` to apply the blacklist:

```cmake
# tools/copy_iso_files.cmake
set(_exclude_args)
foreach (_item IN LISTS BLACKLIST)
    list(APPEND _exclude_args PATTERN "${_item}" EXCLUDE)
endforeach ()

file(COPY "${SRC}/" DESTINATION "${DST}" ${_exclude_args})
```

This keeps the logic self-contained and avoids any dependency on shell utilities.

---

## Debug Builds — `dist/iso_contents/`

When building with `DEBUG=ON` targeting PS2 (`CMAKE_SYSTEM_NAME STREQUAL "Generic"`), the staged ISO directory is
additionally copied to `dist/iso_contents/` after the ISO is generated. This lets you inspect exactly what ended up on
the disc without mounting the image.


