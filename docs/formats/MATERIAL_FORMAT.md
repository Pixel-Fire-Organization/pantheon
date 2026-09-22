# Format — Material payload (`.mtl2`)

The payload a cooked `RES_MATERIAL` asset carries. A material names a shader
type plus a fixed, generic set of parameter slots whose *meaning* is defined
by that shader type — the same "small fixed enum that grows by appending a
value" idiom `PlatformCapability`/`RendererId` already use. This is what lets
a future shader type (for example, water) add its own parameters without a
format change: it defines its own mapping over the same slot arrays, rather
than the struct growing a field per shader type that has ever existed.

The enclosing `.ps2a` container is [ASSET_FORMAT.md](ASSET_FORMAT.md).
Runtime behaviour is [subsystems/RESOURCE.md](../subsystems/RESOURCE.md); how
a material is authored and cooked is [PIPELINE.md](PIPELINE.md) and
[ASSET_AUTHORING.md](../ASSET_AUTHORING.md).

Little-endian throughout. Unlike a texture or a model, a material's payload
is exactly one fixed-size header — there is no variable-length trailer.

## Layout

| Offset | Field | Width | Meaning |
|---|---|---|---|
| 0 | `magic` | 4 | `"MTL2"` |
| 4 | `version` | 4 | 1 |
| 8 | `shaderType` | 1 | Which slot mapping applies (see below) |
| 9 | `flags` | 1 | `AlphaMask` (bit 0), `AlphaBlend` (bit 1), `DoubleSided` (bit 2) |
| 10 | `reserved` | 2 | zero |
| 12 | `floatParams` | 8 × 4 | Meaning defined per `shaderType` |
| 44 | `colorParams` | 4 × 16 | Four RGBA quadruples; meaning defined per `shaderType` |
| 108 | `textureRefs` | 4 × 4 | Index into this asset's own dependency list; `0xFFFFFFFF` = none |

Total size: 124 bytes.

Every `textureRefs` slot mirrors a baked model's `diffuseTexRef` convention
exactly: an index into the *owning asset's own* dependency list (the same
`deps[]` every `.ps2a` already carries), resolved to a resource handle at
load time. A material's texture dependencies stream in asynchronously, the
same as any other dependency — the material asset itself is not held back
waiting for them.

## Shader types and their slot mapping

`shaderType` is a small enum, identical to every other fixed-enumeration
platform key in this engine: it grows by appending a value, and a build's
`-Wswitch` catches a missing case rather than silently doing nothing for an
unhandled one.

### `PbrStandard` (0)

Metallic-roughness PBR, the only shader type today:

| Slot | Meaning |
|---|---|
| `floatParams[0]` | `metallicFactor` |
| `floatParams[1]` | `roughnessFactor` |
| `floatParams[2]` | `normalScale` |
| `floatParams[3]` | `alphaCutoff` (meaningful only when `AlphaMask` is set) |
| `floatParams[4..7]` | reserved for this shader type's own future growth |
| `colorParams[0]` | `baseColorFactor`, RGBA |
| `colorParams[1]` | `emissiveFactor`, RGB (alpha unused) |
| `colorParams[2..3]` | reserved |
| `textureRefs[0]` | albedo map |
| `textureRefs[1]` | tangent-space normal map |
| `textureRefs[2]` | packed occlusion(R)/roughness(G)/metallic(B) map |
| `textureRefs[3]` | reserved |

A texture slot left at `0xFFFFFFFF` means that map is absent; shading falls
back to the corresponding factor alone (a material with no albedo map is
simply flat-shaded in `baseColorFactor`). Normal mapping is reconstructed
from screen-space derivatives at shading time, not from a precomputed vertex
tangent — no vertex attribute or on-disc geometry format carries one.

A future `Water` shader type (the motivating example — waves, distortion,
foam, refraction) would define its own mapping over these same twelve
float/colour slots and four texture slots: a new enum value, a new
name-to-slot table in `tools/cook_assets.py`, and new engine shading code.
Nothing here changes size or layout.

## What a reader must establish

- The blob is at least 124 bytes.
- `magic` matches and `version` is one this build knows.
- `shaderType` is a value this build's shading code actually handles;
  an unknown value refuses the whole material rather than guessing a
  fallback shader.
- Every `floatParams` and `colorParams` entry is finite. A payload with a
  NaN or infinite value is refused whole, not clamped or zeroed — the same
  "refuse whole, never repair" rule every other reader in this engine
  follows.
- A `textureRefs` entry either equals the "none" sentinel or is within the
  owning asset's declared dependency count; an out-of-range index is a
  content error the cook stage (`tools/validate_cooked.py`) is expected to
  have already caught, and the runtime refuses rather than indexing past the
  dependency list.

## Materials are shared between models and levels

A `RES_MATERIAL` asset is referenced identically from both a baked model's
`materialIndex` (an index into the model's own dependency list, each entry
naming a material) and a level's `MATL` chunk (`LevelMaterialEntry.assetKey`,
see [LEVEL_FORMAT.md](LEVEL_FORMAT.md)). The same painted texture used on a
placed prop and a brush wall can be the exact same cooked material — one
asset, one cooked file, two consumers — rather than the two carrying separate,
duplicated material data the way they did before this format existed.

## Static vs. dynamic lighting

A material describes *how* a surface responds to light; it says nothing about
which lights are present. That split is:

- **Static** geometry (level sectors): the level compiler evaluates every
  placed light (any entity composed with `LightComponent` — see
  [LEVEL_FORMAT.md](LEVEL_FORMAT.md)) against each vertex, including a
  shadow-ray occlusion test, and bakes the result into that vertex's colour
  at compile time. Zero runtime cost, identical on every backend.
- **Dynamic** geometry (models): lit live every frame from a fixed-slot light
  list the game sets at runtime (`Renderer::SetLight3D`, mirroring the
  camera-slot model) — see [subsystems/RENDERER.md](../subsystems/RENDERER.md).

The two compose by addition: a dynamic light can also illuminate static
geometry in real time on top of its baked base.
