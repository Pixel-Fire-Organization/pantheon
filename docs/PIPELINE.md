# Build pipeline

Turning sources and source art into something runnable is five distinct stages.
They are separate because they have different inputs, different costs and
different reasons to re-run: changing a texture should not relink the engine, and
changing engine code should not re-encode every texture.

## Stages

| Stage | Input | Output |
|---|---|---|
| 1. Compile engine | Engine sources, for one platform | Engine library |
| 2. Compile game | Game sources and generated code | Executable |
| 3. **Cook assets** | Source art, models, maps + that platform cook list | Engine-native files, per platform |
| 4. **Package resources** | Cooked files, per platform | One master container, per platform |
| 5. Make distribution | Executable, containers + that platform title metadata | A runnable directory or installable package, per platform |

Stages 1 and 2 are per platform because the code is. Stages 3 and 4 are **also**
per platform for assets, because what a platform wants cooked differs — see
below. Stage 5 assembles the two halves.

**Stage 5 may need title metadata**, where the platform distributes an installable
package rather than a directory. That metadata — display name, identifier, icon,
store-front layout, achievements — describes the *game*, not the hardware, so it
is declared under `game/config/platform/<name>/` and validated before it is
used, in the same way stage 4 is gated on validating the cooked tree. A
platform that ships a plain directory declares none of it. See
[vita/PACKAGING.md](vita/PACKAGING.md), [psp/PACKAGING.md](psp/PACKAGING.md) and
[nx/PACKAGING.md](nx/PACKAGING.md) for the current instances.

**Worlds are cooked per platform too, but only for their textures.** A
compiled world's geometry (sectors, entities) has no platform-varying
encoding — the same triangles ship everywhere. Its brush materials do,
though: a level pins every one of its textures for its whole lifetime, so the
per-platform dimension cap that keeps that affordable (`cooklist.json`'s
`level_textures`) is enforced by the level compiler itself, and the resulting
`.ps2l`/`.PS2R` archive is therefore a per-platform artefact like any other
cooked asset, living under `dist/cooked/<platform>/levels/`. That per-level
`.PS2R` is a stage 3 artefact only, standalone and inspectable
(`tools/dump_level.py`) but never shipped as its own file: stage 4 reads every
level archive's entries back out and folds them, unchanged, into the one
master archive alongside that platform's rassets
(`tools/pack_master_archive.py`) — see
[subsystems/ARCHIVE.md](subsystems/ARCHIVE.md) for why everything ships in one
container rather than one per level.

**Source art and world sources share one tree, distinguished by a descriptor.**
Maps, their textures and their models are all authored under `assets/`, since a
texture used in a level and a texture packaged as its own cooked resource are
the same kind of file. A texture or model gains a JSON descriptor beside it only
when it should *also* exist as a standalone archive resource; one with no
descriptor is level-only content, read directly by the level compiler and never
seen by the cook stage. See `assets/README.md`.

## Artefacts

```
dist/
  cooked/
    <platform>/        stage 3 output: engine-native files, not yet packed
  <platform>/          stage 5 output: a self-contained runnable directory,
                       or the installable package the platform distributes
```

A distribution directory is self-contained: it can be copied elsewhere and run,
with no reference back into the build tree or into another platform directory.
This is what makes it impossible to launch one platform build with another
platform assets. Where the platform distributes a package rather than a
directory, the same property holds of the package — it carries its own assets, so
it cannot be installed against another platform content.

## Cooking is per platform

**The cook has two source trees.** Game-owned content and engine-owned content
are cooked together into one tree, because a resource key is a resource key and
the runtime cannot tell which side authored an asset. The engine owns very
little — its default interface font is the whole of it today — but it owns that
much because the interface has to be able to draw before a game supplies
anything. Engine-owned keys carry a prefix so the two cannot collide.

The right encoding for an asset is a hardware question. A platform with a small
dedicated video memory region wants a palettised texture that fits its budget; a
platform with an ordinary graphics processor wants something it can upload
directly, with no expansion at load time. Cooking once and sharing the result
means one of the two is always doing unnecessary work, and neither can express
its own budget.

So cooked output is keyed by platform, and **the cooked files and the containers
built from them differ between platforms by design**. Comparing them across
platforms proves nothing; comparing a platform against its own previous build is
the meaningful check.

## Cook lists

Each platform carries a cook list, stored with that platform in the engine
alongside its constants and its build rules. Keeping it there is what preserves
the property that adding a platform is adding one directory.

A cook list declares, per asset class:

- the target encoding and its parameters,
- any budget ceiling cooking must respect,
- and which asset classes this platform skips entirely.

Per-asset metadata says **what** an asset is — its type, its source, its
dependencies. The cook list says **how this platform wants it baked**, and
overrides the per-asset default where they disagree. Neither replaces the other:
one is authored with the content, the other with the platform.

Cook lists are validated against a schema, so a malformed one fails at
configure time rather than producing subtly wrong output.

## Validation

Packaging is gated on validation, so a bad cook cannot reach a container. The
validator checks cooked output against the cook list that produced it:

- magic and version,
- dependencies that name a missing asset,
- an encoding the cook list did not ask for,
- textures over the platform budget or maximum dimensions,
- world sectors over the format maximum,
- duplicate resource keys.

It is also runnable on its own, so continuous integration can check a tree
without building one.

## Inspection

Read-only tools report what a cooked file or a container actually contains —
headers, entry tables, offsets, sizes, alignment and padding, dependency lists.
These exist because a format is only debuggable if its contents can be seen
without running the engine, and because a validator failure is far more useful
next to a dump of the thing that failed.

## Formats

The artefacts of stages 3 and 4 are specified in
[formats/ASSET_FORMAT.md](formats/ASSET_FORMAT.md),
[formats/ARCHIVE_FORMAT.md](formats/ARCHIVE_FORMAT.md) and
[formats/LEVEL_FORMAT.md](formats/LEVEL_FORMAT.md). Those layouts are contracts
between the tools and the runtime, and both sides change together.
