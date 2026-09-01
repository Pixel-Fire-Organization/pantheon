# Format — Cooked asset (`.ps2a`)

One cooked asset: a fixed-size header followed by a payload. Produced by the cook
stage of the build pipeline, read by the resource subsystem.

Little-endian. The header size is fixed and is part of the contract — it is
computed from the maximum dependency count and the maximum path length, both of
which are format constants and **identical on every platform**. A platform that
chose its own path length would produce assets no other platform could parse.

Runtime behaviour is in [subsystems/RESOURCE.md](../subsystems/RESOURCE.md);
production is in [PIPELINE.md](../PIPELINE.md).

## Layout

```
+-----------------------------+  offset 0
| header (fixed size)         |
+-----------------------------+
| payload                     |  encoding depends on type
+-----------------------------+
```

| Field | Width | Meaning |
|---|---|---|
| `magic` | 4 | `"PS2A"` little-endian |
| `type` | 4 | asset category |
| `depCount` | 1 | number of declared dependencies |
| `reserved` | 3 | padding, zero |
| `ext` | 16 | source file extension, for diagnostics only |
| `deps` | maxDeps x maxPath | dependency keys, each NUL-terminated |
| `dataSize` | 4 | payload size in bytes |

## Asset types

| Type | Payload | Status |
|---|---|---|
| Texture | Platform texture encoding chosen by that platform cook list | Supported |
| Model | Baked geometry: separated, unindexed vertex arrays | Supported |
| Sound | — | **Not implemented on any platform** |
| Font | Glyph metrics; the atlas is a separate texture dependency | Supported |
| Theme | An interface theme, as a memory image | Supported |

Sound is enumerated but unimplemented engine-wide. A request for it fails
immediately rather than returning a handle that never becomes ready.

The font payload is described by [FONT_FORMAT.md](FONT_FORMAT.md) and the theme
payload by [THEME_FORMAT.md](THEME_FORMAT.md). Both are **identical on every
platform** — glyph metrics and interface colours are not hardware questions.
What varies by hardware varies in a font's atlas, which is an ordinary texture
and follows the texture rules below.

## Dependencies

An asset names the assets it needs. They are loaded first and reference-counted,
so an asset is never reported ready before everything it references is. This is
what makes one load request sufficient for a whole object graph, and it is why a
model does not need its textures listed by the caller.

Dependency paths are stored as canonical keys — see
[ARCHIVE_FORMAT.md](ARCHIVE_FORMAT.md) — so a baked dependency and a device path
resolve to the same asset.

## What a reader must establish

The header and every payload are bytes read off disc, so the numbers in them
are checked before anything is sized, cast or copied from them. A payload that
fails any check is refused whole and reported naming what failed; nothing is
clamped into range.

For the container:

- The blob is at least a header long, the magic matches, and `type` names a
  category this build knows.
- `ext` and every dependency key are terminated by the reader before they are
  read as strings, and `depCount` is bounded by the format's maximum before the
  dependency table is walked.
- The payload is at least `dataSize` long.

For the model payload (baked geometry):

- The mesh and material tables fit the blob, and the counts they are sized from
  are bounded before that product is formed.
- Every mesh has at least one vertex and a position array, and each of its
  position, normal and texture-coordinate arrays spans `vertexCount` elements
  inside the blob. Each offset-plus-length sum is computed at a width that
  cannot wrap a 32-bit `size_t`: both terms come straight from the file, and a
  count near 2^28 wraps the product long before it exceeds the blob.
- A mesh's vertex count is recorded only after its arrays have been bounded, so
  a refused mesh leaves no count behind for a release path to trust.
- A material index outside the material table resolves to the first material.
  It is the one field a reader repairs rather than refuses, because it selects
  a texture rather than an address.

The texture payload has its own list in [TIM2_TEXTURE.md](TIM2_TEXTURE.md); the
font and theme payloads carry theirs in [FONT_FORMAT.md](FONT_FORMAT.md) and
[THEME_FORMAT.md](THEME_FORMAT.md).

The header's size is a multiple of the strictest transfer alignment any
platform requires, and that is checked when the engine is built: the payload
starts immediately after it, so the header's size is what carries the read
buffer's alignment through to pixels or geometry a transfer path consumes in
place.

## Platform-varying payloads

**The header is identical across platforms; the payload is not.** Texture
encoding is chosen per platform by that platform cook list, because the right
encoding is a hardware question: a platform with a small dedicated video memory
region wants a palettised format, while a platform with an ordinary GPU wants a
directly-uploadable one and no runtime expansion.

Consequently cooked assets and the archives built from them **differ between
platforms by design**, and comparing them across platforms proves nothing. A
per-platform comparison against a previous build of the same platform is the
meaningful check.

## Constraints

- Dependency count and path length are fixed by the format; exceeding either is a
  cook-time error.
- The header is a fixed size regardless of how many dependencies are used, so
  every asset pays for the maximum. This is deliberate: a fixed header means the
  payload offset is known without parsing.
- No checksum, with one exception. A truncated payload is detected by size
  mismatch, not by content verification, because every payload is parsed and a
  parse rejects nonsense as it reads. The theme payload is copied into live
  state rather than parsed and therefore carries one of its own.
- `ext` is diagnostic only. Nothing dispatches on it; `type` is authoritative.
