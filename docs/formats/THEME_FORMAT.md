# Format — Cooked theme (`.ps2a`, theme payload)

A complete interface theme: every colour role and every metric, in the exact
memory layout the interface holds them in, so that loading one is a copy rather
than a parse.

The enclosing container is [ASSET_FORMAT.md](ASSET_FORMAT.md); the consumer and
its guarantees are [subsystems/UI.md](../subsystems/UI.md).

Little-endian throughout.

## The payload is a memory image

The style block is stored exactly as the interface holds it. A game switching
themes does no decoding and no per-field assignment: the loader validates, then
copies the block into place in one step.

That is the point of the format, and it has three consequences that are part of
the contract rather than incidental.

**The style block's layout is an on-disc contract.** It is therefore identical
on every platform, its fields have explicit widths, and it may not be reordered
or extended without a version change. The build asserts the compiled size
against the value this format declares, and the tooling asserts the same value
from its own side, so the two cannot drift silently.

**Little-endian is assumed.** Every platform this engine targets is
little-endian, so a copied image is correct on all of them. This is a
requirement of the format and not a coincidence to be rediscovered: a
big-endian target would need a byte-swapping loader, and this is the sentence
that says so.

**There are no pointers in the image.** Anything a theme refers to by name —
which font each text role uses — lives in a separate table after the block, as
fixed-width keys the loader resolves. A pointer in a copied image would be a
pointer into the address space that wrote the file.

## Layout

```
+-----------------------------+
| theme header (16 bytes)     |
+-----------------------------+
| style block                 |  copied verbatim; size declared in the header
+-----------------------------+
| font references             |  fontCount entries
+-----------------------------+
```

### Theme header

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0 | `magic` | 4 | `"PSTH"` |
| 4 | `version` | 2 | layout version of the style block |
| 6 | `styleBytes` | 2 | size of the style block |
| 8 | `checksum` | 4 | over the style block and the font references |
| 12 | `fontCount` | 1 | font references following |
| 13 | `reserved` | 3 | zero |

### Font reference

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0 | `role` | 1 | which text role this font serves |
| 1 | `key` | 48 | asset key of the font, NUL-terminated and NUL-padded |

A fixed-width key rather than a length-prefixed one, because the block is
copied: a variable-length field would make the payload's shape depend on its
content, and the size check that guards the copy could no longer be a single
comparison.

### Style block

Colours for every role in the interface's role list, in enumeration order, then
the timing metrics, then the pixel metrics. The role list is not restated here:
it lives with the interface, and the cook stage reads its view of it out of the
interface's own enumeration, so a role added in one place and forgotten in the
other fails the build rather than producing a theme with a missing colour.

| | Width | |
|---|---|---|
| colours | 4 each | one byte per channel, alpha last, in role order |
| repeat delay | 4 | seconds before a held direction begins repeating |
| repeat interval | 4 | seconds between repeats |
| pixel metrics | 2 each, signed | padding, spacing, scales, widths and heights |
| reserved | 2 | zero |

Times are in **seconds, never frames**. Two of this engine's platform variants
differ only in refresh rate, so a frame-counted repeat would run measurably
faster on one of them.

**A theme is colours and metrics together.** Every field above — colours and
pixel metrics alike — may differ from one theme to the next: a theme built for a
smaller or poorer display can declare a larger text scale and wider borders, not
only different colours. That authoring choice is resolved before cooking, in the
declaration the cook stage reads; the payload itself is unaffected, since it has
always held one complete style per theme.

### Version history

- **1** — the original layout: 17 colour roles, 10 pixel metrics, 4 reserved
  bytes.
- **2** — added the `TextDisabled` colour role and three pixel metrics
  (`menuBarHeight`, `caretWidth`, `iconSpacing`), spending the format's reserved
  bytes down to 2. A version-1 payload is refused, never migrated, per the
  version-mismatch rule below.

## Why this payload has a checksum when others do not

Every other cooked payload is parsed, and parsing validates as a side effect: a
count that does not match, an offset past the end, a type field that names
nothing all surface while reading. This payload is **not parsed** — it is copied
into live engine state — so nothing about it is checked incidentally. The
checksum replaces the validation that reading would otherwise have performed.

It is a non-cryptographic hash over the bytes after the header. Its job is to
catch a truncated or corrupted file, not a hostile one.

## Validation, in order

A loader performs all of these **before writing anything**, and abandons the
load at the first failure:

1. **Identity** — the magic.
2. **Version** — must equal the version the running engine was built against. A
   mismatch is refused and reported naming both versions. There is no migration:
   a theme is a handful of numbers and re-cooking it is cheaper than carrying
   code to translate between layouts forever.
3. **Size** — the declared style size must equal the compiled one, and the
   payload must be exactly as long as the header says.
4. **Integrity** — the checksum.
5. **Font roles** — every reference must name a role the running engine has.
6. **Sanity** — the metrics must be within their permitted ranges. A checksum
   proves a file arrived as it was written; it says nothing about whether it was
   written sensibly. A text scale of zero divides by zero in every routine that
   fits text to a width, and a negative padding inverts every row rectangle it
   is used to compute.

**A refused theme changes nothing.** Validation happens against a staging copy
and the live theme is replaced in one assignment, so there is no state in which
half a theme has been applied. The failure is reported naming the check that
failed and the value that failed it, and the interface carries on with the theme
it already had.

**That includes the font references.** They are part of the theme, not a side
effect of reading it: decoding stages them alongside the style block into the
same staged object, and both reach the interface in the same commit. Writing
them as they were read would install a refused theme's font keys — and those
keys are what the interface next asks the resource manager to load — and would
also install them for a theme that was only ever decoded to be inspected, which
is what the Testbed's asset browser does every time someone previews one. The
font role table is validated whole before any of it is staged, so a theme naming
a role this build does not have stages nothing either.

## Constraints

- **Themes are data, not code.** A refused theme is a content error, reported
  and survived; it is never a reason to stop.
- A theme names fonts but does not contain one. A named font that is absent
  leaves that role on the interface's built-in font, reported once.
- **The built-in themes are not files.** They are compiled in, because the
  interface must be able to draw before any filesystem exists, and because they
  are what a refused load falls back to.
- Metric values may be tuned per platform at cook time — the same theme is not
  equally legible at 640 pixels wide and at 1280 — but the **layout** never
  varies by platform. This is independent of a theme declaring its own metrics
  (above): one is the author choosing that a theme should look different, the
  other is the cook stage adapting one theme's declared values to a screen.
