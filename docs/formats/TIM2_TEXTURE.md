# Format — TIM2 texture payload (`.tm2`)

The payload a cooked texture asset carries. TIM2 is Sony's own PS2 texture
container; the engine writes a deliberately small subset of it, and every
platform reads that same subset — the non-PS2 backends expand it to a directly
uploadable form as they upload.

The enclosing `.ps2a` container is [ASSET_FORMAT.md](ASSET_FORMAT.md). Runtime
behaviour is [subsystems/RESOURCE.md](../subsystems/RESOURCE.md); the per-platform
choice of encoding is [PIPELINE.md](../PIPELINE.md).

Little-endian throughout.

## Layout

```
+-----------------------------+  offset 0
| file header (16 bytes)      |
+-----------------------------+  offset 16
| picture header (48 bytes)   |
+-----------------------------+  offset 64
| image data                  |  mip levels, largest first
+-----------------------------+
| colour table                |  present only for indexed images
+-----------------------------+
```

Exactly **one picture per file**. The format permits several; nothing in this
engine writes or reads a second, and a file declaring more is malformed rather
than partially supported.

### File header

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0 | `magic` | 4 | `"TIM2"` |
| 4 | `formatVersion` | 1 | 4 |
| 5 | `formatId` | 1 | 0 |
| 6 | `pictureCount` | 2 | 1 |
| 8 | `pad` | 8 | zero |

### Picture header

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0 | `totalSize` | 4 | header + image + colour table |
| 4 | `clutSize` | 4 | colour table bytes; zero when there is none |
| 8 | `imageSize` | 4 | image bytes, all mip levels and their padding |
| 12 | `headerSize` | 2 | 48 |
| 14 | `clutColors` | 2 | entries in the colour table |
| 16 | `pictFormat` | 1 | 0 |
| 17 | `mipmapCount` | 1 | levels present, at least 1 |
| 18 | `clutType` | 1 | 0 none, 3 linear 32-bit table |
| 19 | `imageType` | 1 | see below |
| 20 | `imageWidth` | 2 | texels, level 0 |
| 22 | `imageHeight` | 2 | texels, level 0 |
| 24 | `GsTex0` | 8 | hardware register image; zero means "let the backend decide" |
| 32 | `GsTex1` | 8 | hardware register image; carries the filter hint |
| 40 | `GsRegs` | 4 | zero |
| 44 | `GsTexClut` | 4 | zero |

### Image types

| Value | Name | Texel |
|---|---|---|
| 1 | `rgba16` | 16-bit, five bits per colour and one of alpha |
| 3 | `rgba32` | 32 bits, one byte per channel, red first |
| 5 | `pal8` | one byte, an index into the colour table |

An indexed image serves two quite different purposes, distinguished by how its
colour table is built rather than by the format: an ordinary palettised texture,
and a **coverage** image whose table is a ramp from transparent to opaque white
and whose index is therefore an alpha value. The second is what a glyph atlas
uses, and it round-trips exactly through the alpha rescale described below, so
one payload is correct whether a backend samples it natively or expands it.

## Mip levels

Levels are stored largest first, contiguously, **each padded to a 16-byte
boundary**. A reader walks them by computing each level's dimensions rather than
by reading a table, so the padding rule is part of the contract: get it wrong
and every level after the first is misaligned.

The chain stops before any level would fall below eight texels on either axis.
Very small textures therefore have exactly one level, which is not an error.

## Colour tables

Present only for indexed images. Entries are 32 bits, one byte per channel with
red first, stored linearly — not in the hardware's interleaved order, which the
backend applies on upload if it needs to.

**Alpha is in the console's range, where fully opaque is 128 rather than 255.**
This is the single most surprising thing in the format and the one most likely
to be got wrong: a table written with 255 for opaque is over-bright on the
console and, on every other platform, is rescaled on upload from a value that
was already wrong. The non-PS2 backends rescale the console range to the full
range as they expand.

Two conventions are written by the cook stage, and they are not
interchangeable:

- **Ordinary indexed textures** quantise into all 256 entries and are opaque.
- **Cutout textures** reserve entry 0 as fully transparent and quantise into
  entries 1 to 255. Impostor and billboard atlases depend on this; using an
  ordinary table for one makes its transparent regions draw as whatever colour
  landed in entry 0.

## Filtering

`GsTex1` carries the magnification and minification filter the asset wants. A
zero field means the backend chooses, which is what every texture written before
this field was used contains — so zero must keep meaning "backend default"
forever, and must never be reinterpreted as a specific filter.

An explicit choice is therefore **marked** rather than encoded as a value: bit 63
is set to say the filter fields are authoritative, and the hardware's own
magnification and minification fields then hold the choice. Bit 63 is unused by
the register, so a general reader of this format ignores the marker and sees a
sensible register image either way.

The distinction matters for exactly one class of content today: a pixel font
atlas magnified to an integer scale must be sampled without interpolation, or
every glyph is blurred. Photographic content wants the opposite.

## What a reader must establish

Every offset below is a number the file chose, and the pixel pointer a reader
hands to a backend is computed from them, so each is checked against the
payload before the pointer exists. A payload failing any check is refused whole
and reported naming the field.

- The blob holds a file header and one picture header, the magic matches, and
  the picture count is not zero.
- The image type is one of the three above; anything else is refused rather
  than guessed at.
- The mip count does not exceed the engine's per-texture ceiling, and an
  indexed image declares a colour table of at least 256 entries.
- `headerSize` is at least the picture header's own size, and the image data —
  the picture base plus `headerSize`, extended by `imageSize` — lies inside the
  blob. That sum is computed at a width that cannot wrap a 32-bit `size_t`: an
  `imageSize` near the 32-bit maximum wraps the sum on three of the four
  platforms and would pass the comparison trivially.
- The image data starts on a 16-byte boundary. Every backend consumes the
  pixels in place — one as a DMA source that requires it, another through a
  16-bit view that traps without it — so an odd `headerSize` is a crash on
  hardware, not a slightly wrong texture.
- Each mip level, walked largest-first under the padding rule above and
  accumulated at the same widened width, ends inside `imageSize`.
- For an indexed image, the colour table that follows the image data lies
  inside the blob.

A zero width or height passes this list. It is the backend that refuses to
upload an empty texture, and the load then fails the way any refused upload
does; no backend divides by a dimension before that refusal.

## Constraints

- **Power-of-two dimensions** for anything the console will sample, because the
  hardware's texture register stores dimensions as exponents. The cook stage
  enforces it rather than leaving it to be discovered as corruption at runtime.
- Per-texture and total size ceilings are a platform question and live in that
  platform's cook list, not here.
- **No checksum.** A truncated payload is detected by size mismatch, not by
  content verification.
- The engine writes a subset. Fields it always writes as zero are not
  "reserved" in the format's own terms — a general TIM2 reader would find
  meaning in some of them — they are simply unused here, and a reader in this
  engine may assume the subset.
