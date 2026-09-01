# PS2 texture budget

Reference for how much video memory a texture costs on this platform, and what
therefore fits. The runtime rules are in
[RESOURCE.md](../subsystems/RESOURCE.md); this is the arithmetic behind the
budget it enforces.

## Pages, not bytes

Video memory is allocated in **pages**. A page is 8 KB regardless of pixel
format, but its *dimensions* depend on the storage mode:

| Storage mode | Page dimensions | Bits per pixel |
|---|---|---|
| 32-bit colour | 64 x 32 | 32 |
| 16-bit colour | 64 x 64 | 16 |
| 8-bit palettised | 128 x 64 | 8 |

**Every mip level rounds up to whole pages**, and a palettised texture costs one
additional page for its colour table. This rounding is why a texture cost cannot
be computed by multiplying width by height by depth — a 65-pixel-wide 32-bit
texture occupies the same width in pages as a 128-pixel one.

The engine asks the platform for a texture cost in bytes and converts from pages,
so the budget comparison means the same thing here as on a platform with an
ordinary graphics processor.

## Budget

| | |
|---|---|
| Total texture pages | 264 |
| Level allowance | 200, leaving headroom for game and interface textures |
| Maximum per texture | 64 pages |
| Maximum dimensions | 512 x 512 |

The per-texture maximum is a hardware consequence, not a policy: it matches the
largest contiguous slot the memory layout registers.

## Cost examples, 32-bit colour, no mips

| Size | Pages | Notes |
|---|---|---|
| 64 x 32 | 1 | Exactly one page |
| 128 x 128 | 8 | |
| 256 x 256 | 32 | |
| 512 x 256 | 64 | At the per-texture maximum |
| 512 x 512 | 128 | **Rejected** — over the per-texture maximum |

Palettised textures are dramatically cheaper for the same dimensions, since a
page covers four times the area, at the cost of a colour table page. This is why
the platform cook list prefers palettised encoding here and a directly-uploadable
encoding on desktop — see [PIPELINE.md](../PIPELINE.md).

## Memory layout

The renderer partitions video memory at startup: two frame buffers and a depth
buffer sized to the active region, then a bank of texture slots whose total page
count matches the budget above. The region determines the frame buffer height, so
the split differs between the two variants while the texture budget does not.

## Over-budget behaviour

A texture that exceeds the per-texture maximum, or that would push the total over
budget, is **rejected at load with an actionable error** naming the asset and the
overage. It is not downscaled and not silently dropped.

This is worth stating because the underlying library behaves differently: left to
itself it silently evicts the least recently used texture slot when it runs out,
which produces textures that vanish and reappear with no diagnostic. The engine
enforces the budget above that so the failure is visible and attributable. See
[PS2GL.md](renderers/PS2GL.md).
