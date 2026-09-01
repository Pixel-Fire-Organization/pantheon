# Vita Packaging

The Vita deliverable is a `.vpk` — a container holding the signed executable, the
title metadata, the store-front presentation, the asset archive and the compiled
worlds. One is produced per variant, into `dist/vita/` and `dist/vitatv/`.

Unlike the PS2 disc, a Vita package carries **per-title metadata**: a title
identifier, a display name, an icon, a store-front layout and optionally a trophy
set. None of that is a build setting, so none of it lives in CMake. It is declared
in `game/config/platform/vita/package.json`, validated against
`tools/schemas/package.schema.json`, and consumed by `tools/vita_package.py`.

---

## Why the config lives under `game/`, and the schema does not

The cook list under `engine/config/<name>/` answers a hardware question — how
this machine wants textures baked. A title identifier and an icon answer a
question about **the game**, and would be wrong to inherit from the engine. A
second game built on this engine ships its own identity while sharing the cook
list unchanged.

This is a new convention, and it is deliberately narrow:
`game/config/platform/<name>/` holds title metadata for one platform, nothing
else. It sits under `game/config/` alongside every other declarative file this
project owns — achievements, theme, title — rather than under `game/platform/`
directly, for the same reason those do: one place a person looks for "what does
this project declare," not one place per kind of declaration.

The schema that validates it is not part of that declaration, though, and does
not live beside it: `tools/schemas/package.schema.json`, alongside every other
`.schema.json` this project has, since a schema is read by the cook system that
enforces it, not authored by the game the way the data it validates is. The
declaration's own `$schema` field still points at it, by relative path, purely
so an editor can offer completion — nothing here reads that field back.

---

## Why the toolchain macros are not used

The VitaSDK ships `vita_create_self` and `vita_create_vpk`, and they are the
documented path. This project calls the underlying tools directly instead, for one
concrete reason:

> Both macros accumulate their arguments into **cache** variables and append on
> every call. A configure that builds two variants calls each macro twice, and the
> second call inherits the first variant title identifier and its entire file
> list.

Both also attach their targets to `ALL`, while every executable here is
`EXCLUDE_FROM_ALL` and driven through the `dist` target.

So the platform fragment invokes `vita-elf-create`, `vita-make-fself`,
`vita-mksfoex` and `vita-pack-vpk` itself, exactly as the PS2 fragment invokes
`mkisofs` itself. Each invocation gets its own argument list and nothing is shared
between variants. This costs roughly twenty lines and buys a two-variant build
that is correct.

---

## `package.json`

The title's identity is **not** written here. It is declared once for every
platform and folded into this config when it is read, so the identity the console
shows and the identity a save is filed under cannot disagree. A variant may still
override the display name, which is how the television variant is named.

| Field | Meaning |
|---|---|
| `title.id` | *Declared centrally.* Nine characters, `XXXXYYYYY`. Also names the writable data directory |
| `title.name` | *Declared centrally.* The display name under the bubble |
| `title.version` | *Declared centrally.* `##.##` |
| `self.safe` | Restricts the executable to the ordinary user API. Clear it only if the title genuinely needs more |
| `self.compress` | Compress the executable inside the package |
| `self.extended_memory` | Request the larger main-memory allowance. Off by default |
| `sfo.category` | `gd` for an application |
| `sfo.parental_level` | 0–11 |
| `sfo.extra` | Raw metadata key-values, passed through unchanged |
| `livearea.*` | Paths to the presentation images, and the layout style |
| `trophies.*` | See [Trophies](#trophies) below |
| `files[]` | Extra `{src, dst}` pairs staged into the package |
| `variants.vitatv` | A deep-merge override applied only to the set-top build |

`variants` exists so the set-top build can carry its own display name and metadata
without duplicating the whole file. Anything not overridden is inherited.

---

## LiveArea assets

These sizes are **exact**. The console rejects the install with error
`0x8010113D` when one is wrong, and that error names nothing — which is why
validation runs first and fails with the file and the expected size instead.

| File | Size | Colour | Alpha |
|---|---|---|---|
| `sce_sys/icon0.png` | 128 x 128 | 8-bit indexed | **Not allowed** |
| `sce_sys/pic0.png` | 960 x 544 | 256-colour indexed | Not allowed |
| `sce_sys/livearea/contents/bg0.png` | 840 x 500 | 8-bit indexed | Not allowed |
| `sce_sys/livearea/contents/startup.png` | 280 x 158 | 8-bit indexed | **Allowed** |
| `sce_sys/livearea/contents/template.xml` | | | Layout |

`pic0.png` is optional. The others are required when a store-front is declared.

`startup.png` is the only layer that may be transparent. Supplying an RGB image
where an indexed one is required is the most common failure, and it is silent
until install.

`template.xml` is generated from `livearea.style` when the config does not supply
one. Two styles are supported: `a1` centres the startup image, `psmobile`
right-aligns it. A hand-written template is used verbatim.

---

## Trophies

**Disabled, and the runtime integration removed.** `trophies.enabled` is `false`
in `game/config/platform/vita/package.json`, `VitaPlatform::GetAchievements()`
returns null, and the code that called `sceNpTrophy*`
(`engine/src/platform/vita/Trophy.cpp`) no longer exists — see
[ACHIEVEMENT.md](../subsystems/ACHIEVEMENT.md)'s off-the-shelf evaluation for
why: the console's own trophy system was the original implementation and does
not work for a title the platform holder did not sign, which this section's
own registration findings below do not change. The engine's own cross-platform
achievement system, declared once in `game/config/achievements.json`, is what
actually ships and is unaffected by any of this — it has no dependency on the
native trophy service. The rest of this
section is kept as a record of how the packaging and registration mechanism
works, for if a signed build ever re-enables it; none of it describes
currently-running code.

**Trophies on unsigned software require a plugin the player installs**, because
the console verifies two signatures this project cannot produce: one over the
title communication identifier, and one over the trophy pack. The `NoTrpDrm`
plugin disables both checks for the running title.

Consequences, all of which belong in front of anyone enabling this:

- A player without the plugin sees a title that runs normally and awards nothing.
- Unlocks are recorded **locally** and are visible in the console trophy
  application. They never reach the online service and never appear on a profile.
- The communication identifier is declared, not allocated. It must match the one
  baked into the trophy pack.

Two ways to supply the pack, and both are first-class:

- **`trophies.trp`** — a path to a pack built elsewhere. Staged as-is.
- **`trophies.list`** — trophy definitions, from which the build generates the
  configuration files, the pack, and a header of trophy identifiers for the game
  to use so no trophy number is ever written by hand.

The generated header lands in the build tree, the same way the entity definitions
are generated. Runtime behaviour is [ACHIEVEMENT.md](../subsystems/ACHIEVEMENT.md);
the container layout is [TROPHY_PACK.md](../formats/TROPHY_PACK.md).

**A pack is generated automatically** when `trophies.enabled` is set: the build
writes the configuration files, the container and the identifier header, and
stages the container at `sce_sys/trophy/<communication id>/TROPHY.TRP`. `VITA_TRP_MAGIC` overrides
the container identifier and is not normally needed; see
[../formats/TROPHY_PACK.md](../formats/TROPHY_PACK.md).

**A trophy set is registered when the game asks, not when the title is
installed.** Installing a title delivers the pack; it does not register the set.
The game requests that once, at start, through a system dialog, and the console
installs the set from the pack the title carries.

**The call that does it does not belong to the trophy module.** That module
exports fifteen functions and none of them registers a set, which is true and
also misleading: the setup dialog lives with the other system dialogs, in the
common dialog module. Searching the trophy module alone concludes that the
platform cannot register a set at run time, which is wrong and costs a great
deal of time, because the symptom it produces is indistinguishable from a
malformed pack.

Attempting registration is not required to succeed. A set already installed
needs nothing, so a refusal is recorded and the state read that follows stays
the test of whether trophies can be recorded.

**The plugin is firmware-specific, and identifies the firmware by the shell
rather than by its version.** It patches the system shell at fixed addresses,
which differ in every build, and works out which set to use from the identity of
the shell module itself. That is deliberate and it matters here: the reported
system software version can be spoofed, and on a console where it is, the
version is not evidence about the plugin either way.

It also means a plugin that does not recognise the shell **fails to load at
all** rather than loading and misbehaving. So a title that gets as far as
binding to its communication identifier has already proved the plugin is loaded
and matched. Suspecting the firmware after that point is wasted effort.

The version is logged at trophy bring-up anyway, because it belongs in a fault
report, but it is logged as what the system reports rather than as what is
running.

**No set produced by this build has ever registered on hardware.** What was
tried, and what each attempt ruled out, is in
[../formats/TROPHY_PACK.md](../formats/TROPHY_PACK.md). The engine treats this as
a platform that answers "no" — the game runs, unlocks are refused, and the player
is told once. That path is exercised on every platform without achievements, so
it is not a special case.

The console says when nothing is registered. Errors `0x80551610` and `0x80551611`
are "not registered" and "already registered"; a code in that group coming back
from reading trophy state means the console recognises the identifier and holds
no set behind it, and every later call is refused against nothing. Reinstalling
does not fix it, because installation was never what registered the set.

**A title that ships no trophy data declares none.** The count the engine
reports is what was packaged, so it does not drop to zero merely because a
console refused, and a packaging mistake stays distinguishable from a missing
plugin. Where trophies are packaged but the console refuses them, the engine
shows the player a dismissible notice at boot; see
[../subsystems/DEBUG.md](../subsystems/DEBUG.md).

---

## Package layout

```
eboot.bin                                  the executable
sce_sys/param.sfo                          title metadata
sce_sys/icon0.png                          bubble icon
sce_sys/pic0.png                           optional
sce_sys/livearea/contents/bg0.png
sce_sys/livearea/contents/startup.png
sce_sys/livearea/contents/template.xml
sce_sys/trophy/<NPWR#####_00>/TROPHY.TRP   when trophies are enabled
RASSETS.PS2R                               the master archive - every rasset and every compiled world
```

The archive is inside the package rather than beside it, because the
application mount is the only read-only root the title has and a distribution
must be installable as one file.

---

## Build order

Packaging is gated on validation, so a bad config cannot reach a container — the
same rule the cooked-asset pipeline follows.

1. **Validate** the config, every referenced file, and every image size.
2. **Emit** the generated pieces: the layout template if absent, the trophy
   configuration and pack, the trophy identifier header, and the argument lists
   the fragment needs.
3. **Convert** the linked executable to the console executable format.
4. **Generate** the title metadata from the config.
5. **Pack** everything, plus the asset archive and worlds, into the `.vpk`.

Steps 3 to 5 run once per variant with independent argument lists.

---

## Adding a file to the package

Add a `{src, dst}` pair to `files[]` in the config. Do not add it to the CMake
fragment — the fragment deliberately names no content, so that what ships is
described in one readable place.
