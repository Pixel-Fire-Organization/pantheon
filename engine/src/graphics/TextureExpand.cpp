#include "graphics/TextureExpand.h"

bool Gfx_ExpandToRgba8(const TextureUpload& upload, uint8_t* dst, size_t dstBytes)
{
    if (!dst || upload.width <= 0 || upload.height <= 0 || !upload.levelPtr[0])
        return false;

    const size_t texels = static_cast<size_t>(upload.width) * static_cast<size_t>(upload.height);
    if (dstBytes < texels * 4u)
        return false;

    if (upload.format == PixelFormat::RGBA32)
    {
        const uint8_t* src = static_cast<const uint8_t*>(upload.levelPtr[0]);
        for (size_t i = 0; i < texels; ++i)
        {
            dst[i * 4 + 0] = src[i * 4 + 0];
            dst[i * 4 + 1] = src[i * 4 + 1];
            dst[i * 4 + 2] = src[i * 4 + 2];
            dst[i * 4 + 3] = Gfx_ExpandPs2Alpha(src[i * 4 + 3]);
        }
        return true;
    }

    if (upload.format == PixelFormat::RGBA16)
    {
        const uint16_t* src = static_cast<const uint16_t*>(upload.levelPtr[0]);
        for (size_t i = 0; i < texels; ++i)
        {
            const uint16_t p = src[i];
            dst[i * 4 + 0] = static_cast<uint8_t>(((p >> 0) & 0x1Fu) * 255u / 31u);
            dst[i * 4 + 1] = static_cast<uint8_t>(((p >> 5) & 0x1Fu) * 255u / 31u);
            dst[i * 4 + 2] = static_cast<uint8_t>(((p >> 10) & 0x1Fu) * 255u / 31u);
            dst[i * 4 + 3] = (p & 0x8000u) ? 0xFFu : 0x00u;
        }
        return true;
    }

    const uint8_t* idx = static_cast<const uint8_t*>(upload.levelPtr[0]);
    const uint8_t* clut = static_cast<const uint8_t*>(upload.clut);
    for (size_t i = 0; i < texels; ++i)
    {
        if (!clut)
        {
            dst[i * 4 + 0] = dst[i * 4 + 1] = dst[i * 4 + 2] = dst[i * 4 + 3] = 0xFFu;
            continue;
        }
        const uint8_t* e = clut + static_cast<size_t>(idx[i]) * 4u;
        dst[i * 4 + 0] = e[0];
        dst[i * 4 + 1] = e[1];
        dst[i * 4 + 2] = e[2];
        dst[i * 4 + 3] = Gfx_ExpandPs2Alpha(e[3]);
    }
    return true;
}
