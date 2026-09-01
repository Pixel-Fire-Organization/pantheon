# Examples

Standalone binaries under `examples/`, each exercising one engine capability
(or a small composite of them) in total isolation — its own process, its own
memory, its own runtime-reset boundary, so a bug in one can never be a side
effect of another. This is the isolation Testbed cannot offer: its scenes
share one process with nineteen others and the game itself. See
[subsystems/TESTBED.md](subsystems/TESTBED.md) for the split of
responsibility between the two, and
[guidelines/NEW_EXAMPLE.md](guidelines/NEW_EXAMPLE.md) before adding one.

Each example builds for every active platform, the same mechanism `game/`
already uses, and lands under `examples/dist/<name>/<platform>/`. On nx that
folder also holds `<name>.nro`, the example packaged into its own container with
its assets inside, because an nx executable cannot read a loose archive beside
it; see [nx/PACKAGING.md](nx/PACKAGING.md#examples).

## What's here

| Example | Shows | Scenes (cycle with `CycleNext`/`CyclePrev`) |
|---|---|---|
| `primitives` | Renderer primitive submission, texturing, and the draw-list ceiling | Coloured, Textured, Draw load |
| `ui_gallery` | Every interface widget, live theme editing, and the interface's quad budget | Gallery, Style, Budget |

Run one directly — no debug-menu chord, no other example, no game process
involved. That standalone-ness is the entire point.

## What's not here yet

[backlog/testbed_examples.md](backlog/testbed_examples.md) tracks the rest of
the planned examples and the rest of the Testbed scenes still to migrate or
rework, with the reasoning behind each recommendation, so a future session
doesn't have to re-derive it.

Running these binaries under each emulator from a script, with assertions
instead of a person reading the log, is designed but not built:
[backlog/automation_hooks.md](backlog/automation_hooks.md).

## Regressions

[fixed_issues/issues.json](fixed_issues/issues.json) logs engine defects
found and fixed while building an example — check it first if something an
example already covers starts misbehaving again.
