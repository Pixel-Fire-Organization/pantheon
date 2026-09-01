#pragma once

#include <cstddef>
#include <cstdint>

#include "graphics/Types.h"

/// Rescale a cooked alpha byte to full range.
/// @param a Alpha as stored, where 0x80 means fully opaque.
/// @return Alpha in 0..255.
inline uint8_t Gfx_ExpandPs2Alpha(uint8_t a) { return (a >= 0x80u) ? 0xFFu : static_cast<uint8_t>(a * 2u); }

/// Expand level 0 of a cooked texture into tightly packed RGBA8.
/// @param upload Source texture; RGBA32, RGBA16 and PAL8 are handled.
/// @param dst Receives width * height * 4 bytes.
/// @param dstBytes Capacity of dst; a short buffer is refused, not overrun.
/// @return False, having written nothing, when the upload is malformed.
bool Gfx_ExpandToRgba8(const TextureUpload& upload, uint8_t* dst, size_t dstBytes);
