#include "graphics/FontFormat.h"

#include <cstdint>
#include <cstring>

#include "../../include/core/EngineDebug.h"
#include "../../include/core/EngineMemory.h"

namespace
{
    uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

    uint32_t ReadU32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }
} // namespace

bool Font_LoadBaked(const void* data, size_t size, int32_t atlasResourceId, Font* outFont)
{
    if (!data || !outFont || size < FONT_HEADER_SIZE)
    {
        Engine_LogError("Font: payload is shorter than a header (%u bytes)", static_cast<unsigned>(size));
        return false;
    }

    const uint8_t* base = static_cast<const uint8_t*>(data);
    if (ReadU32(base) != FONT_MAGIC)
    {
        Engine_LogError("Font: bad magic; this is not a cooked font");
        return false;
    }

    const uint16_t version = ReadU16(base + 4);
    if (version != FONT_VERSION)
    {
        Engine_LogError("Font: layout version %u, this build reads %u; re-cook the font", version, static_cast<unsigned>(FONT_VERSION));
        return false;
    }

    const uint16_t flags = ReadU16(base + 6);
    const uint16_t atlasWidth = ReadU16(base + 8);
    const uint16_t atlasHeight = ReadU16(base + 10);
    const uint16_t lineHeight = ReadU16(base + 12);
    const uint16_t baseline = ReadU16(base + 14);
    const uint16_t spaceAdvance = ReadU16(base + 16);
    const uint16_t firstCode = ReadU16(base + 18);
    const uint16_t glyphCount = ReadU16(base + 20);
    const uint16_t missingIndex = ReadU16(base + 22);
    const uint16_t cellCount = ReadU16(base + 24);
    const uint16_t cellOffset = ReadU16(base + 26);

    const size_t glyphTableEnd = static_cast<size_t>(FONT_HEADER_SIZE) + static_cast<size_t>(glyphCount) * FONT_GLYPH_SIZE;
    const size_t expected = glyphTableEnd + static_cast<size_t>(cellCount) * FONT_GLYPH_SIZE;
    if (glyphCount == 0 || size < expected)
    {
        Engine_LogError("Font: %u glyphs and %u cell(s) need %u bytes, payload is %u", glyphCount, cellCount, static_cast<unsigned>(expected), static_cast<unsigned>(size));
        return false;
    }
    if (cellOffset != glyphTableEnd)
    {
        Engine_LogError("Font: cell table declared at %u, the glyph table ends at %u", cellOffset, static_cast<unsigned>(glyphTableEnd));
        return false;
    }
    if (missingIndex >= glyphCount)
    {
        Engine_LogError("Font: substitute glyph %u is outside the %u-glyph table", missingIndex, glyphCount);
        return false;
    }
    if (lineHeight == 0 || atlasWidth == 0 || atlasHeight == 0)
    {
        Engine_LogError("Font: line height %u and atlas %ux%u must all be non-zero", lineHeight, atlasWidth, atlasHeight);
        return false;
    }
    // These three are read as 16-bit and kept as 8-bit. Check them at the width
    // they are kept, or a line height of 256 passes the non-zero check above and
    // is stored as a zero-height line.
    if (lineHeight > UINT8_MAX || baseline > UINT8_MAX || spaceAdvance > UINT8_MAX)
    {
        Engine_LogError("Font: line height %u, baseline %u and space advance %u must each fit a byte", lineHeight, baseline, spaceAdvance);
        return false;
    }

    FontGlyph* glyphs = static_cast<FontGlyph*>(Engine_PlatformAlloc(static_cast<size_t>(glyphCount) * sizeof(FontGlyph), 16));
    if (!glyphs)
    {
        Engine_LogError("Font: out of memory for a %u-glyph table", glyphCount);
        return false;
    }

    FontGlyph* cells = nullptr;
    if (cellCount > 0)
    {
        cells = static_cast<FontGlyph*>(Engine_PlatformAlloc(static_cast<size_t>(cellCount) * sizeof(FontGlyph), 16));
        if (!cells)
        {
            Engine_LogError("Font: out of memory for a %u-cell table", cellCount);
            Engine_PlatformFree(glyphs);
            return false;
        }
    }

    const uint8_t* g = base + FONT_HEADER_SIZE;
    for (uint16_t i = 0; i < glyphCount; ++i, g += FONT_GLYPH_SIZE)
    {
        FontGlyph& out = glyphs[i];
        out.u = ReadU16(g);
        out.v = ReadU16(g + 2);
        out.w = g[4];
        out.h = g[5];
        out.bearingX = static_cast<int8_t>(g[6]);
        out.bearingY = static_cast<int8_t>(g[7]);
        out.advance = g[8];
        out.reserved[0] = 0;
        out.reserved[1] = 0;
        out.reserved[2] = 0;

        if (static_cast<uint32_t>(out.u) + out.w > atlasWidth || static_cast<uint32_t>(out.v) + out.h > atlasHeight)
        {
            Engine_LogError("Font: glyph %u falls outside the %ux%u atlas", static_cast<unsigned>(firstCode + i), atlasWidth, atlasHeight);
            Engine_PlatformFree(glyphs);
            Engine_PlatformFree(cells);
            return false;
        }
    }

    const uint8_t* c = base + cellOffset;
    for (uint16_t i = 0; i < cellCount; ++i, c += FONT_GLYPH_SIZE)
    {
        FontGlyph& out = cells[i];
        out.u = ReadU16(c);
        out.v = ReadU16(c + 2);
        out.w = c[4];
        out.h = c[5];
        out.bearingX = static_cast<int8_t>(c[6]);
        out.bearingY = static_cast<int8_t>(c[7]);
        out.advance = c[8];
        out.reserved[0] = 0;
        out.reserved[1] = 0;
        out.reserved[2] = 0;

        if (static_cast<uint32_t>(out.u) + out.w > atlasWidth || static_cast<uint32_t>(out.v) + out.h > atlasHeight)
        {
            Engine_LogError("Font: cell %u falls outside the %ux%u atlas", static_cast<unsigned>(i), atlasWidth, atlasHeight);
            Engine_PlatformFree(glyphs);
            Engine_PlatformFree(cells);
            return false;
        }
    }

    outFont->glyphs = glyphs;
    outFont->cells = cells;
    outFont->atlasResourceId = atlasResourceId;
    outFont->glyphCount = glyphCount;
    outFont->firstCode = firstCode;
    outFont->missingIndex = missingIndex;
    outFont->atlasWidth = atlasWidth;
    outFont->atlasHeight = atlasHeight;
    outFont->cellCount = cellCount;
    outFont->lineHeight = static_cast<uint8_t>(lineHeight);
    outFont->baseline = static_cast<uint8_t>(baseline);
    outFont->spaceAdvance = static_cast<uint8_t>(spaceAdvance);
    outFont->flags = static_cast<uint8_t>(flags);
    return true;
}

void Font_FreeBaked(Font* font)
{
    if (!font)
        return;
    if (font->glyphs)
        Engine_PlatformFree(const_cast<FontGlyph*>(font->glyphs));
    if (font->cells)
        Engine_PlatformFree(const_cast<FontGlyph*>(font->cells));
    std::memset(font, 0, sizeof(*font));
    font->atlasResourceId = -1;
}

const FontGlyph* Font_GetGlyph(const Font* font, uint32_t codepoint)
{
    if (!font || !font->glyphs || font->glyphCount == 0)
        return nullptr;
    if (codepoint < font->firstCode)
        return &font->glyphs[font->missingIndex];
    const uint32_t index = codepoint - font->firstCode;
    if (index >= font->glyphCount)
        return &font->glyphs[font->missingIndex];
    return &font->glyphs[index];
}

const FontGlyph* Font_GetCell(const Font* font, uint8_t index)
{
    if (!font || !font->cells || index >= font->cellCount)
        return nullptr;
    return &font->cells[index];
}
