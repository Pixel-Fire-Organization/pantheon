# C Expert Instructions — PS2 Engine

## Standard

- **C99** (`-std=c99`). No GNU extensions. No VLAs.

## 1. General Principles

- **Design**: Encapsulation (hide state in `.c`), Abstraction, DRY, and KISS.
- **Comments**: Comment the **function signature only** — a doc comment on the declaration giving the summary,
  parameters and return value, so an editor can show it to a caller. No inline commentary or rationale; that belongs
  in `docs/`. See the Comments section of `cpp-expert.instructions.md` for the full rule and an example.
- **Error Handling**: Use **enums** for descriptive error codes or `bool` for simple success/fail. Avoid raw integers.
  Use `Engine_Panic` only for unrecoverable hardware/memory states.

## 2. Naming Conventions

| Element                 | Convention             | Example                    |
|:------------------------|:-----------------------|:---------------------------|
| **Files**               | `PascalCase.c`         | `EngineMemory.c`           |
| **Functions**           | `PascalCase_SnakeCase` | `Engine_LoadToSlot()`      |
| **Function Variables**  | `camelCase`            | `uint32_t slotIndex;`      |
| **Static/Globals**      | `PascalCase`           | `static int SlotCount;`    |
| **Macros/Consts**       | `UPPER_SNAKE_CASE`     | `MAX_SLOT_COUNT`           |
| **Types (Struct/Enum)** | `PascalCase`           | `MemoryArena`, `ArenaType` |
| **Members**             | `camelCase`            | `arena->usedSize`          |

## 3. Types & Constants

- **Fixed-width**: **ALWAYS** use `<stdint.h>` types (`uint32_t`, `int16_t`, `uint8_t`, etc.). Never plain `int`,
  `long`, or `unsigned`.
- **Booleans**: Use `<stdbool.h>` (`bool`, `true`, `false`).
- **Constants**: Format constants go next to the struct they describe (`EngineArchive.h`, `EngineLevelFormat.h`,
  ...); platform constants go in `engine/include/platform/<name>/PlatformConstants*.h`, reached via
  `#include "PlatformConstants.h"`. See the Constants standard in `.github/copilot-instructions.md`.
- **Magic Numbers**: Prohibited. Use descriptive macros or enums.
- **Bitwise**: Use enums or macros for bitmasks.
- **Enums**: Always `typedef enum { ... } TypeName;` — prefix all values with the type name to avoid pollution:
  `ARENA_SCRIPT`, `ARENA_CONFIG`, not just `SCRIPT`, `CONFIG`.

## 4. Memory & Performance (Embedded PS2)

- **No Dynamic Allocation**: `malloc`, `free`, `realloc`, `calloc` are **FORBIDDEN**. Use the Engine's Arena or Pool
  systems.
- **Alignment**: PS2 DMA requires **16-byte alignment** (Quadwords).
    - Use `__attribute__((aligned(16)))` for buffers sent to GS/VIF.
    - Slots in `EngineMemory.c` are 16KB aligned for safety.
- **Pointers**: Avoid raw pointer arithmetic where possible. Use `Engine_GetSlot` or `Engine_LoadToSlot`.
- **Volatile**: Use `volatile` when accessing hardware registers or memory shared with DMA/Interrupts.
- **Packing**: Arrange struct members from largest to smallest to minimize padding. Use `__attribute__((packed))` for
  hardware-mapped structures (GS/VIF packets).
- **Unions**: Use sparingly for type-punning or memory optimization.
- **Strings**: Use safe versions: `strncpy`, `snprintf`, `strncat`. **NEVER** use `strcpy` or `sprintf`.
- **Functions**: Use `static` for internal helper functions to keep them local to the translation unit.
- **Header Guards**: Use `#ifndef HEADER_NAME_H` / `#define HEADER_NAME_H` style. No `#pragma once` in pure C headers.
- **Designated Initializers**: Valid and encouraged in C99 (`.field = value`). Forbidden in C++11 — use plain member
  assignment instead.
- **Clang-Format**: The repo `.clang-format` at root is authoritative. **ALWAYS** follow it. Never run it over
  `external/`. Wrap data tables and mesh arrays with `// clang-format off` / `// clang-format on`.
