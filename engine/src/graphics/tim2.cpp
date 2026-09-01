#include "../include/graphics/tim2.h"

#include <cstring>

#include "../../include/core/EngineDebug.h"

namespace
{
    // Alignment-safe little-endian reads (the blob comes straight off disc and its
    // multi-byte fields are not guaranteed to sit on aligned addresses).
    uint16_t ReadU16(const uint8_t* p)
    {
        uint16_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    uint32_t ReadU32(const uint8_t* p)
    {
        uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    // File header layout (16 bytes):
    //   [0..3]  magic "TIM2"   [4] formatVersion   [5] formatId
    //   [6..7]  pictureCount   [8..15] padding
    constexpr uint32_t TIM2_MAGIC = 0x324D4954u; // 'T','I','M','2' little-endian
    constexpr size_t TIM2_FILE_HEADER_SIZE = 16;

    // Picture header layout (48 bytes / 0x30) — see tim2.h. We only read what we
    // need: sizes, dimensions and the image color type.
    constexpr size_t TIM2_PIC_HEADER_SIZE = 0x30;

    // GsTex1 carries the sampling filter. Zero has always meant "backend
    // default", and every texture cooked before the field was used holds zero,
    // so an explicit choice is marked rather than encoded as a value.
    constexpr uint64_t TIM2_TEX1_EXPLICIT = 1ull << 63;
    constexpr uint64_t TIM2_TEX1_MMAG_SHIFT = 8;

    uint64_t ReadU64(const uint8_t* p)
    {
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i)
            v = (v << 8) | p[i];
        return v;
    }

    TextureFilter Tim2FilterFromTex1(uint64_t tex1)
    {
        if (!(tex1 & TIM2_TEX1_EXPLICIT))
            return TextureFilter::Linear;
        return ((tex1 >> TIM2_TEX1_MMAG_SHIFT) & 1ull) ? TextureFilter::Linear : TextureFilter::Nearest;
    }

    // TIM2 imageType values we accept.
    constexpr uint8_t TIM2_IMGTYPE_RGBA16 = 0x01; // A1B5G5R5
    constexpr uint8_t TIM2_IMGTYPE_RGBA32 = 0x03; // A8B8G8R8
    constexpr uint8_t TIM2_IMGTYPE_IDTEX8 = 0x05; // 8-bit indexed + CLUT

    inline uint64_t Align16(uint64_t n) { return (n + 15u) & ~static_cast<uint64_t>(15u); }

    inline bool IsAligned16(uint64_t n) { return (n & 15u) == 0u; }
} // namespace

bool Tim2_Parse(const void* data, size_t size, Tim2Image* out)
{
    if (!data || !out || size < TIM2_FILE_HEADER_SIZE)
    {
        Engine_LogError("TIM2: null/short blob (%zu bytes).", size);
        return false;
    }

    const uint8_t* base = static_cast<const uint8_t*>(data);

    if (ReadU32(base) != TIM2_MAGIC)
    {
        Engine_LogError("TIM2: bad magic.");
        return false;
    }

    const uint8_t formatId = base[5];
    const uint16_t pictureCount = ReadU16(base + 6);
    if (pictureCount == 0)
    {
        Engine_LogError("TIM2: zero pictures.");
        return false;
    }

    // pack_assets.py always writes formatId 0 (16-byte alignment → picture header
    // immediately after the 16-byte file header). formatId 1 aligns to 128.
    const size_t picBase = (formatId == 0) ? TIM2_FILE_HEADER_SIZE : 128;
    if (size < picBase + TIM2_PIC_HEADER_SIZE)
    {
        Engine_LogError("TIM2: truncated picture header.");
        return false;
    }

    const uint8_t* pic = base + picBase;
    const uint32_t clutSize = ReadU32(pic + 4);
    const uint32_t imageSize = ReadU32(pic + 8);
    const uint16_t headerSize = ReadU16(pic + 12);
    const uint8_t mipmapCount = pic[17];
    const uint8_t imageType = pic[19];
    const uint16_t imageWidth = ReadU16(pic + 20);
    const uint16_t imageHeight = ReadU16(pic + 22);

    PixelFormat fmt;
    uint32_t bpp; // bytes per texel for the image levels
    switch (imageType)
    {
    case TIM2_IMGTYPE_RGBA32:
        fmt = PixelFormat::RGBA32;
        bpp = 4;
        break;
    case TIM2_IMGTYPE_RGBA16:
        fmt = PixelFormat::RGBA16;
        bpp = 2;
        break;
    case TIM2_IMGTYPE_IDTEX8:
        fmt = PixelFormat::PAL8;
        bpp = 1;
        break;
    default:
        Engine_LogError("TIM2: unsupported imageType 0x%02X.", imageType);
        return false;
    }

    const uint8_t mipCount = (mipmapCount == 0) ? 1 : mipmapCount;
    if (mipCount > TEX_MAX_MIP_LEVELS)
    {
        Engine_LogError("TIM2: too many mip levels (%u > %d).", mipCount, TEX_MAX_MIP_LEVELS);
        return false;
    }
    if (fmt == PixelFormat::PAL8 && clutSize < 256u * 4u)
    {
        Engine_LogError("TIM2: PAL8 image missing CLUT (clutSize=%u).", clutSize);
        return false;
    }

    // Image data starts headerSize bytes into the picture block. Levels are
    // stored contiguously largest-first, each 16-byte aligned (see pack_assets.py).
    // Offsets are widened before they are summed: imageSize is a 32-bit field
    // and the sum wraps a 32-bit size_t, which would pass the bound trivially.
    const uint64_t imgOffset = static_cast<uint64_t>(picBase) + headerSize;
    if (headerSize < TIM2_PIC_HEADER_SIZE || imgOffset + imageSize > size)
    {
        Engine_LogError("TIM2: image payload out of bounds (hdr=%u, size=%u).", headerSize, imageSize);
        return false;
    }
    if (!IsAligned16(imgOffset))
    {
        Engine_LogError("TIM2: picture header is %u bytes, which leaves the pixels unaligned.", headerSize);
        return false;
    }

    std::memset(out->levelPtr, 0, sizeof(out->levelPtr));
    uint64_t off = 0;
    for (uint8_t lvl = 0; lvl < mipCount; ++lvl)
    {
        const uint32_t w = (imageWidth >> lvl) ? static_cast<uint32_t>(imageWidth >> lvl) : 1u;
        const uint32_t h = (imageHeight >> lvl) ? static_cast<uint32_t>(imageHeight >> lvl) : 1u;
        const uint64_t levelBytes = static_cast<uint64_t>(w) * h * bpp;
        if (off + levelBytes > imageSize)
        {
            Engine_LogError("TIM2: mip level %u out of bounds.", lvl);
            return false;
        }
        out->levelPtr[lvl] = base + imgOffset + off;
        off += Align16(levelBytes);
    }

    // CLUT (PAL8) follows the image payload.
    out->clut = nullptr;
    if (fmt == PixelFormat::PAL8)
    {
        const uint64_t clutOffset = imgOffset + imageSize;
        if (clutOffset + 256u * 4u > size)
        {
            Engine_LogError("TIM2: CLUT out of bounds.");
            return false;
        }
        out->clut = base + clutOffset;
    }

    out->pixels = out->levelPtr[0];
    out->mipCount = mipCount;
    out->width = imageWidth;
    out->height = imageHeight;
    out->format = fmt;
    out->filter = Tim2FilterFromTex1(ReadU64(pic + 32));
    out->imageSize = imageSize;
    return true;
}
