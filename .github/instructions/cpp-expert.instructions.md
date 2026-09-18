# C++ Expert Instructions — Pantheon

## Standard

- **C++11** (`-std=c++11`). No exceptions (`-fno-exceptions`). No RTTI (`-fno-rtti`).
- No STL containers (`std::vector`, `std::string`, etc.) — they use heap and throw.
- `<cstdint>`, `<cstddef>`, `<cmath>`, `<cstring>` from the C++ standard library are permitted.

## Naming

| Element          | Convention              | Example                        |
|------------------|-------------------------|--------------------------------|
| Files            | `PascalCase.cpp` / `.h` | `RaylibRenderer.cpp`           |
| Classes          | `PascalCase`            | `RaylibRenderer`, `DrawLists`  |
| Methods          | `PascalCase_SnakeCase`  | `Engine_GetRenderer()`         |
| Local variables  | `camelCase`             | `uint32_t vertexCount;`        |
| Private members  | `m_camelCase`           | `m_drawLists`, `m_initialized` |
| Static/globals   | `PascalCase`            | `static int SlotCount;`        |
| Macros/constants | `UPPER_SNAKE_CASE`      | `GFX_MAX_DRAW_LIST_LENGTH`     |

## Enums

- Always use `enum class` — never plain `enum`. Scoped enums prevent namespace pollution and implicit int conversions.
  ```cpp
  // BAD
  enum Primitive3D { Cube, Sphere };

  // GOOD
  enum class Primitive3D : uint8_t { Cube, Sphere };
  ```
- Always specify an explicit underlying type (e.g. `: uint8_t`, `: uint32_t`) to control size and ABI.

## Class Design

- Abstract base classes (interfaces) are **header-only** — no `.cpp` file.
    - `virtual ~Base() = default;` in the header is sufficient.
    - Each public interface needs to be set with `= 0` for interface classes.
- Use `= delete` for copy/move constructors and assignment operators on non-copyable types.
- Always use `final` on leaf classes to help the compiler devirtualize.
- No `new`/`delete` — objects are either stack-allocated, arena-allocated, or static.
    - Exception: each platform's `Platform.cpp` uses `new` for the single `Renderer` instance it
      constructs (lifetime = process), released through the same platform's `DestroyRenderer`.

## Allocator pairing

Memory is returned to the allocator it came from. These are **not** interchangeable — on Win32
aligned allocations come from a separate heap, and releasing one with `free()` is undefined
behaviour:

| Allocated with | Released with |
| :--- | :--- |
| `Engine_PlatformAlloc` / the platform memory contract | `Engine_PlatformFree` |
| `malloc` / `calloc` | `free` |
| An arena slot | Nothing — the arena owns it; reset the slot or segment |

Prefer `PlatformArray<T>` (`EngineMemory.h`): it carries the matching release with the pointer, so
the pairing cannot be got wrong. Use the raw pair only where the pointer must stay a plain view —
`Mesh`, for instance, is consumed by renderers and may point either at a platform allocation or
straight into an arena slot.

## Smart pointers

`std::unique_ptr` is permitted for genuinely owning pointers. It is header-only, allocates nothing
itself, needs no RTTI and does not throw, so it is compatible with `-fno-exceptions -fno-rtti` and
with the EE toolchain.

- **Use it** for owning platform allocations (via `PlatformArray<T>`) and for a single owned object
  whose lifetime is a scope or a member.
- **Do not use it** for arena or pool allocations — the arena *is* the ownership model, and a
  smart pointer there implies an independent free that must never happen — nor for non-owning views,
  nor in a per-frame hot path.
- `std::shared_ptr` stays banned: it heap-allocates a control block and atomically refcounts.

## Exhaustive switches over enum keys

A switch that must answer for every enumerator - `GetConstant`, `HasCapability`, and any other keyed
accessor a platform implements - carries **no `default:` label**. List `Count` explicitly and put the
fallback after the switch. `-Wswitch` (in `-Wall`, with `-Werror`) then refuses to compile a platform
that forgets a key.

This is not style. A `default:` turned a missing `MaxTextureBytes` case into a silent `0` at runtime,
which rejected every texture load on that platform with a nonsense message. The compiler can catch
the whole class; let it.

Switches that legitimately reject unknown values - `CreateRenderer` on an unsupported id, for
instance - keep their `default:`.

## Keyed accessors never guess

A platform accessor that cannot answer **panics naming the key**; it never returns a placeholder. `0` is a
legitimate value for several platform constants, so a guessed zero cannot be told apart from a real one — that
ambiguity is what made a missing `MaxTextureBytes` case reject every texture load on one platform.

This is why the return type is a plain `uint32_t` rather than an optional or a two-state union: every platform must
answer every key, and the compiler now enforces that, so an "empty" result is unreachable by construction. Making
callers unwrap it would invite `valueOr(0)` at each of them and reintroduce exactly the bug the panic prevents. If a
value ever becomes genuinely optional per platform, add a separate `TryGet...` for that key rather than weakening
the common path — and check whether `HasCapability` already covers the question.

## Panics never return

`Engine_Panic` and `Platform::Panic` are `[[noreturn]]`. A panic is a graceful crash, not an error
path a caller continues from — never write recovery code after one.

## Overloads vs Default Parameters

- **Prefer overloads to default parameters** for virtual methods and public API.
- Only one overload is the canonical one, so it contains the full implementation. Others call it with sensible default
  values.

## `auto`

- Use `auto` only when the type is **not** already written on the right-hand side.
    - ✅ `auto windowReadyBase = GetTime();` — type not obvious, saves repetition
    - ❌ `auto tex = static_cast<const Texture2D*>(ptr);` — defeats readability, write the type
- Never use `auto` for function parameters or return types.

## Casts

- Use `static_cast<T>` for safe numeric conversions.
- Use `reinterpret_cast<T>` only for hardware/DMA pointer aliasing.
- Never use C-style casts `(T)value` in new C++ code.

## Memory — CRITICAL for PS2

- Same rules as C: no `malloc`/`free`/`new`/`delete` in subsystem code.
- `__attribute__((aligned(16)))` on any buffer sent to GS/VIF/DMA.
- Struct members: **largest to smallest** to minimize padding.
- No `std::string` — use `char[]` with `snprintf`/`strncpy`.

## Designated Initializers

- **Forbidden in C++11.** Use plain member assignment instead:
  ```cpp
  // BAD  (C++20 / C99 only)
  Camera3D cam = { .position = {0,0,0}, .fovy = 45 };

  // GOOD (C++11)
  Camera3D cam;
  cam.position   = Vector3{0.0f, 0.0f, 0.0f};
  cam.fovy       = 45.0f;
  ```

## Clang-Format

The repo `.clang-format` is authoritative. **ALWAYS** follow it.

- **Never** run clang-format over `external/`.

## Graphics Subsystem Rules

- `rlBegin` / `rlEnd` must wrap the **entire batch** for a primitive type — one pair per frame.
    - Never call `rlPushMatrix`/`rlPopMatrix` inside a `rlBegin`/`rlEnd` block.
- Per-object transforms are applied **on the CPU** (build rotation matrix once, apply to all vertices).
- `BeginMode3D` / `EndMode3D` / `BeginMode2D` / `EndMode2D` are owned exclusively by `DrawLists::Render()`.
    - No other code may call these directly.
- The render order each frame is: Skybox → BeginMode3D → Primitives → Models → EndMode3D → BeginMode2D → UI → EndMode2D.

## Pure Virtual / Abstract Classes

- A class is abstract if it has at least one pure virtual method.
- The destructor of an abstract class should be `virtual ~Base() = default;` in the header.
- **No `.cpp` file** for pure-abstract classes — all method bodies are either `= 0` or inline in the header.

## Constants

- Format constants (anything an on-disc layout or a `tools/` script depends on) live in the header declaring the
  matching struct: `EngineArchive.h`, `EngineResource.h`, `EngineLevelFormat.h`, `EngineIO.h`.
- Platform constants live in `engine/include/platform/<name>/PlatformConstants*.h`; shared code includes
  `"PlatformConstants.h"` and CMake resolves it to the selected platform.
- Values shared code must read at runtime come from `Platform::GetConstant(PlatformConstant)` — an `enum class` key,
  never a string. The same applies to the other keyed platform accessors (capabilities, input devices).
- See the Constants standard in `.github/copilot-instructions.md` for the format-vs-platform test.

## Comments

**Comment the function signature, and nothing else.**

A declaration carries a doc comment so an editor can show a caller the summary,
the parameters and the return value without opening the file. That is the only
comment a source file should contain.

```cpp
/// Reserve the arenas and the main pool.
/// @param outMap Filled with the resulting memory map on success.
/// @return False when the map exceeds the platform budget, or reservation failed.
bool Reserve(EngineMemoryMap* outMap) override;
```

- **Doc comments go on declarations**, in the header. A definition in a `.cpp`
  repeats nothing.
- **No inline commentary.** No "why" essays, no rationale, no hardware quirks, no
  notes on what a line avoids. All of that belongs in `docs/` — see the
  Documentation section of `.github/copilot-instructions.md`. A reader who needs
  the reasoning reads the spec; a reader who needs the call signature hovers it.
- **No section-header blocks** and no file-banner essays. A short file-level doc
  comment naming what the file contains is fine; a paragraph is not.
- **Never comment why a block was removed.**

If a line is confusing enough to want a comment, the fix is a better name or a
smaller function. If the reasoning genuinely matters, it goes in the spec.