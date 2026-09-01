#pragma once
#include <cstddef>
#include <cstdint>

#include "Types.h"

// ---------------------------------------------------------------------------
// TIM2 (.tm2) — PlayStation 2 native texture container.
//
// Supported (matching what pack_assets.py bakes): single-picture images in
//   32-bit A8B8G8R8 (imageType 0x03 → PixelFormat::RGBA32),
//   16-bit A1B5G5R5 (imageType 0x01 → PixelFormat::RGBA16), or
//   8-bit indexed  (imageType 0x05 → PixelFormat::PAL8) with a 256-entry
//                  A8B8G8R8 CLUT stored after the image payload.
// Mipmaps are supported: levels 0..N are stored contiguously largest-first,
// each 16-byte aligned; per-level offsets are derived from the level-0 size.
// Multi-picture TIM2 files are rejected.
//
// The parser is zero-copy: all pointers point INTO the supplied blob, which
// must remain valid until UploadTexture has copied the data to the device.
// ---------------------------------------------------------------------------

struct Tim2Image
{
    const void* pixels; // level-0 pixel payload (== levelPtr[0])
    const void* levelPtr[TEX_MAX_MIP_LEVELS]; // per-level pointers into the blob
    uint8_t mipCount; // 1..TEX_MAX_MIP_LEVELS
    const void* clut; // 256 x u32 A8B8G8R8, null unless PAL8
    int width; // level-0 width
    int height; // level-0 height
    PixelFormat format;
    TextureFilter filter; // Linear unless the asset asked for something else
    uint32_t imageSize; // bytes of level-0 pixel payload
};

// Parse a TIM2 blob resident in main RAM. Returns true on success.
bool Tim2_Parse(const void* data, size_t size, Tim2Image* out);
