# Format — Cooked font (`.ps2a`, font payload)

A font's metrics: how large its glyphs are, where each one sits in its atlas,
and how far the pen moves after drawing it. **The atlas itself is not in this
payload.** It is an ordinary cooked texture, named as this asset's dependency.

The enclosing container is [ASSET_FORMAT.md](ASSET_FORMAT.md); the atlas payload
is [TIM2_TEXTURE.md](TIM2_TEXTURE.md); the consumer is
[subsystems/UI.md](../subsystems/UI.md).

Little-endian throughout.

## Why the atlas is a dependency rather than part of the payload

An atlas is a texture, and the pipeline already knows how to bake, budget,
validate, upload, reference-count and release a texture. Embedding the pixels
here would mean a second implementation of every one of those, differing subtly
from the first.

Declaring it as a dependency reuses all of it: dependencies are loaded before
the asset that names them and are reference-counted, so a font is never reported
ready before its atlas is; the atlas is charged against the platform's texture
budget and refused with the ordinary actionable message if it does not fit; and
the existing validation and inspection tools understand it without change.

It also puts the platform variation where the pipeline already expects it.
**This payload is identical on every platform** — the first cooked payload that
is — because glyph metrics are not a hardware question. Everything that varies
by hardware varies in the atlas.

## Layout

```
+-----------------------------+
| font header (28 bytes)      |
+-----------------------------+
| glyph table                 |  glyphCount entries, 12 bytes each
+-----------------------------+
| cell table                  |  cellCount entries, 12 bytes each -- optional
+-----------------------------+
```

### Font header

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0 | `magic` | 4 | `"PSFN"` |
| 4 | `version` | 2 | layout version; a mismatch is refused, never migrated |
| 6 | `flags` | 2 | bit 0 set when every advance is equal |
| 8 | `atlasWidth` | 2 | texels; must match the atlas dependency |
| 10 | `atlasHeight` | 2 | texels; must match the atlas dependency |
| 12 | `lineHeight` | 2 | pixels from one baseline to the next |
| 14 | `baseline` | 2 | pixels from the top of the line box to the baseline |
| 16 | `spaceAdvance` | 2 | pixels the pen moves for a space |
| 18 | `firstCode` | 2 | codepoint of the first glyph in the table |
| 20 | `glyphCount` | 2 | entries following |
| 22 | `missingIndex` | 2 | glyph drawn for a codepoint outside the table |
| 24 | `cellCount` | 2 | cell entries following the glyph table; zero is valid |
| 26 | `cellOffset` | 2 | byte offset of the cell table from the start of the payload |

`cellOffset` is redundant with `glyphCount` -- it always equals the byte just
past the glyph table -- and is declared anyway rather than derived, the same
reasoning `atlasWidth`/`atlasHeight` duplicate the atlas dependency's own
dimensions: a reader that trusts a derived value catches nothing when the
writer's derivation and its own disagree, so both sides state it and a reader
refuses a mismatch instead of silently reading from the wrong offset.

### Glyph entry, and the cell entry that shares its layout

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0 | `u` | 2 | atlas column of the cell's left edge, in texels |
| 2 | `v` | 2 | atlas row of the cell's top edge, in texels |
| 4 | `w` | 1 | cell width in texels |
| 5 | `h` | 1 | cell height in texels |
| 6 | `bearingX` | 1, signed | pixels from the pen to the cell's left edge |
| 7 | `bearingY` | 1, signed | pixels from the top of the line box to the cell's top edge |
| 8 | `advance` | 1 | pixels the pen moves after this cell |
| 9 | `reserved` | 3 | zero |

Twelve bytes rather than ten so that entries stay four-byte aligned. The
constrained platform loads the two-byte fields directly out of the mapped
payload, and an unaligned load there is a fault, not a slow path.

A cell table entry uses the identical twelve-byte layout, addressed by an
engine-side enumerator rather than a codepoint: icons and controller glyphs,
alongside the glyph set in the same atlas. It is deliberately a second table
rather than an extension of the glyph one, because the two are looked up two
different ways -- a codepoint by subtraction from `firstCode`, a cell by a
zero-based index the caller already has -- and folding them into one table
would force one of those lookups to search.

The cell table is genuinely optional: a font with `cellCount` zero is exactly
as valid as one with some, and every icon this omits falls back to a short
piece of text at the call site, the same way an unavailable font falls back to
the built-in one. A game is never blocked on authoring icon art before its text
can draw.

## The codepoint range is dense

Glyphs cover a contiguous run of codepoints starting at `firstCode`, and a
codepoint is looked up by subtraction. There is no sparse table and no search.

This is a deliberate trade: the interface's own contract limits coverage to
printable ASCII, so a dense table is small, and a lookup that is one subtraction
and one bounds check costs nothing per glyph on the slowest platform. A sparse
table would buy coverage the interface does not offer, at the price of a search
per glyph in the hottest loop the interface has.

A codepoint outside the range draws the glyph at `missingIndex` — visibly wrong
rather than invisibly absent, so missing coverage is found while authoring
rather than reported by a player.

## The atlas

The atlas dependency carries **coverage only**: how much of each texel the glyph
covers, and nothing about its colour. Colour comes from the drawing call, which
is what lets one font serve every colour role and every theme.

**A solid fill is never drawn from the atlas.** An earlier version of this
format reserved a fully-opaque texel so untextured fills could share a texture
with glyphs, on the theory that it kept the batch to one run. It does the
opposite on a fill-rate-bound GPU: a solid fill is the majority of the pixels
the interface draws, and sampling a texture for every one of them costs a
texture fetch to save a state change at a row boundary that is cheap by
comparison. Solid fills are untextured; see [subsystems/UI.md](../subsystems/UI.md).

How glyph and cell coverage is encoded is a platform question, answered by the platform's
cook list:

- Where video memory is scarce, an **indexed** atlas whose colour table is a
  ramp from transparent to opaque white. One byte per texel, and the ramp gives
  antialiased edges for free.
- Elsewhere, a **32-bit** atlas that is white throughout with coverage in the
  alpha channel.

Both are the same image. The distinction is where the coverage is stored, not
what it means.

**The atlas must be power-of-two on every platform**, not only on the one whose
hardware requires it. A font cooked for a permissive platform would otherwise be
silently unusable on the strict one, and the cook stage is where that should be
found.

**The atlas must be sampled without interpolation**, which it requests through
the filter field of its own payload. A pixel font magnified to an integer scale
is exact; the same font linearly filtered is blurred at every scale.

## Constraints

- `atlasWidth` and `atlasHeight` must equal the dependency's actual dimensions.
  They are duplicated here so that metrics can be validated without decoding an
  image, and a disagreement is a cook-time error.
- Every glyph and every cell must lie entirely within the atlas.
- `lineHeight`, `baseline` and `spaceAdvance` are declared 16-bit but kept as
  single bytes, so each must be at most 255 and `lineHeight` must be non-zero.
  The range is checked at the width the value is **kept**, not the width it is
  read at: a `lineHeight` of 256 passes a non-zero check and is then stored as a
  zero-height line, which collapses every row sized from it. A header that
  disagrees with what the runtime can hold is refused, not truncated.
- `cellOffset` must equal the byte just past the glyph table; a payload that
  declares otherwise is refused rather than trusted.
- A glyph advance of zero is refused, except the first glyph in the table --
  almost always an authoring mistake, producing a line of glyphs stacked on one
  another. A **cell** advance of zero is not refused: a cell is never stacked in
  a run of its own the way glyphs are, so the same value is unremarkable there.
- **No kerning table.** Advances are per glyph.
- **No colour, no outline, no shadow, no signed-distance encoding.** A shadow is
  drawn by the caller as a second, offset draw.
- **No checksum**, consistent with every other cooked payload: a truncated file
  is caught by size mismatch. The metrics are parsed rather than mapped straight
  into live state, so parsing catches malformed content as a side effect. The
  theme payload, which is *not* parsed, carries a checksum for exactly that
  reason — see [THEME_FORMAT.md](THEME_FORMAT.md).

## Version history

- **1** — the original layout: a glyph table, and a header field pair naming a
  reserved fully-opaque texel for solid fills to sample.
- **2** — replaced that field pair, now unused, with `cellCount` and
  `cellOffset` naming an optional second table: icons and controller glyphs. A
  version-1 payload is refused, never migrated.
