# Command Line

`../engine/include/core/CommandLine.h` parses `argv` once at startup. The engine consumes `--platform`, `--renderer`
and `--help` through it; everything else it parses is retained and reachable by any subsystem or by game code.

Storage is a fixed static table of pointers **into the original `argv`** — no heap, no STL, no copying, and
`argv` is never mutated. It therefore has to outlive the parse, which it does: it belongs to `main()`.

---

## Grammar

| Form | Meaning |
| :--- | :--- |
| `--name value` | Option with a value |
| `--name=value` | Same, value inside the token |
| `--flag` | Flag; reads back as `"1"`, so `GetBool`/`GetInt` need no special case |
| `-n value`, `-n=value`, `-n` | Short form; parsed as an option named `n` |
| `--` | Terminator — everything after it is positional |
| anything else | Positional |

`argv[0]` is always positional 0. That matters on PS2: the boot path (`cdrom0:\MAIN.ELF;1`, `host:main.elf`)
flows through untouched and is what the platform derives its resource token from.

A bare `-` and a negative number are values, not options, so `--offset -5` keeps the `-5`.

Overflow (`CMD_MAX_OPTIONS`, `CMD_MAX_POSITIONALS`) is an actionable error, never a silent truncation.

---

## Engine options

| Option | Argument | Notes |
| :--- | :--- | :--- |
| `--renderer` | `giftag`, `ps2gl`, `opengl`, `webgpu`, `gxm`, `vitagl`, `gu`, `pspgl`, `deko3d`, `null` | The genuinely runtime-selectable axis. Unsupported names log the compiled-in list and fall back to the platform default. |
| `--platform` | a platform name | Validation / self-identification. A bundle ships exactly one platform, so a mismatch logs what the build actually contains and continues. |
| `--help` | — | Prints options plus the compiled-in platform and renderer lists, then exits. |
| `--gl-version` | `<2.1\|3.3\|4.3\|4.6>` | Win32/OpenGL: pin the context version. Default: the highest the driver grants. |
| `--no-keyboard-pad` | — | Win32: disable the default keyboard->virtual-pad-0 map, for games that want raw keyboard only. |
| `--log-input` | — | Win32: log every key press and pad-button change, for tracing a binding that is not reaching game code. |

A renderer that fails to initialise is not fatal: the platform's `GetFallbackRenderer()` chain is walked
(`giftag → ps2gl → null`, `webgpu → opengl → null`, `gxm → vitagl → null`, `gu → pspgl → null`,
`deko3d → opengl → null`) until one starts.

### On PS2

Arguments come from the ELF launch arguments — ps2client, PCSX2, or an OPL/HDD launcher. A plain disc boot
supplies none, so every option falls back to its default and behaviour is unchanged.

```bash
main.elf --renderer ps2gl      # force the ps2gl backend
main.elf --renderer nonsense   # actionable error, then the default
main.elf --help
```

---

## Reading options

```cpp
bool        HasOption(const char* name) const;
const char* GetString(const char* name, const char* fallback) const;
int32_t     GetInt   (const char* name, int32_t fallback) const;
float       GetFloat (const char* name, float fallback) const;
bool        GetBool  (const char* name, bool fallback) const;   // 1/0, true/false, yes/no, on/off
int32_t     GetEnum  (const char* name, const CommandLineEnumEntry* table, uint32_t count, int32_t fallback) const;
```

`GetEnum` is what maps `--renderer giftag` onto a `RendererId`. On an unrecognised value it logs the option,
the value, and every accepted alternative — the engine's "crash loudly, say what is valid" contract.

## Game options

Unknown options are **retained, never rejected**, so a game can define its own flags without touching the
engine. Reach them from gameplay through `GameAPI.h`:

```cpp
if (game::HasArg("nosound")) { /* ... */ }
const char* level = game::GetArg("level", "TEST");
```
