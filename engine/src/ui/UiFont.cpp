#include "../../include/ui/EngineUi.h"

#include <cstring>

#include "UiInternal.h"
#include "graphics/Renderer.h"

namespace
{
    enum : int
    {
        FONT_FIRST = 32,
        FONT_LAST = 95
    };

    const unsigned char FONT5X7[][UI_FALLBACK_GLYPH_H] = {
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x20 ' '
        {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}, // 0x21 '!'
        {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x22 '"'
        {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}, // 0x23 '#'
        {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, // 0x24 '$'
        {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}, // 0x25 '%'
        {0x08, 0x14, 0x14, 0x08, 0x15, 0x12, 0x0D}, // 0x26 '&'
        {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x27 '\''
        {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, // 0x28 '('
        {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, // 0x29 ')'
        {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}, // 0x2A '*'
        {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, // 0x2B '+'
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}, // 0x2C ','
        {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, // 0x2D '-'
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}, // 0x2E '.'
        {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}, // 0x2F '/'
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0x30 '0'
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 0x31 '1'
        {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}, // 0x32 '2'
        {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, // 0x33 '3'
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 0x34 '4'
        {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 0x35 '5'
        {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 0x36 '6'
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 0x37 '7'
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 0x38 '8'
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}, // 0x39 '9'
        {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}, // 0x3A ':'
        {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x08}, // 0x3B ';'
        {0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01}, // 0x3C '<'
        {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}, // 0x3D '='
        {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}, // 0x3E '>'
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, // 0x3F '?'
        {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}, // 0x40 '@'
        {0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11}, // 0x41 'A'
        {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // 0x42 'B'
        {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // 0x43 'C'
        {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, // 0x44 'D'
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // 0x45 'E'
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // 0x46 'F'
        {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, // 0x47 'G'
        {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // 0x48 'H'
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, // 0x49 'I'
        {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}, // 0x4A 'J'
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // 0x4B 'K'
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // 0x4C 'L'
        {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // 0x4D 'M'
        {0x11, 0x19, 0x15, 0x15, 0x13, 0x11, 0x11}, // 0x4E 'N'
        {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 0x4F 'O'
        {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // 0x50 'P'
        {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // 0x51 'Q'
        {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // 0x52 'R'
        {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // 0x53 'S'
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // 0x54 'T'
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 0x55 'U'
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, // 0x56 'V'
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, // 0x57 'W'
        {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // 0x58 'X'
        {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // 0x59 'Y'
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, // 0x5A 'Z'
        {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}, // 0x5B '['
        {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01}, // 0x5C '\\'
        {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}, // 0x5D ']'
        {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}, // 0x5E '^'
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}, // 0x5F '_'
    };

    char Upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }

    const unsigned char* Glyph(char c)
    {
        int index = static_cast<int>(Upper(c));
        if (index < FONT_FIRST || index > FONT_LAST)
            index = FONT_FIRST;
        return FONT5X7[index - FONT_FIRST];
    }

    enum : int
    {
        GLYPH_MAX_SPANS_PER_ROW = (UI_FALLBACK_GLYPH_W + 1) / 2,
        GLYPH_MAX_RUNS = GLYPH_MAX_SPANS_PER_ROW * UI_FALLBACK_GLYPH_H
    };

    /// One rectangle of set pixels within a glyph cell, in cell coordinates.
    /// Edges are half-open, so a run covering only row 2 has y0 = 2, y1 = 3.
    struct GlyphRun
    {
        uint8_t x0;
        uint8_t x1;
        uint8_t y0;
        uint8_t y1;
    };

    int RowSpans(unsigned bits, uint8_t* starts, uint8_t* ends)
    {
        int spans = 0;
        int col = 0;
        while (col < UI_FALLBACK_GLYPH_W)
        {
            if (!((bits >> (UI_FALLBACK_GLYPH_W - 1 - col)) & 1u))
            {
                ++col;
                continue;
            }
            const int start = col;
            while (col < UI_FALLBACK_GLYPH_W && ((bits >> (UI_FALLBACK_GLYPH_W - 1 - col)) & 1u))
                ++col;
            starts[spans] = static_cast<uint8_t>(start);
            ends[spans] = static_cast<uint8_t>(col);
            ++spans;
        }
        return spans;
    }

    /// Decompose one glyph into rectangles, merging a span with the identical
    /// span on the row below it so a straight stroke costs one quad rather than
    /// one per row.
    /// @param rows The glyph's bit rows.
    /// @param out Receives the rectangles.
    /// @return How many rectangles were written.
    int GlyphRuns(const unsigned char* rows, GlyphRun* out)
    {
        GlyphRun open[GLYPH_MAX_SPANS_PER_ROW];
        int openCount = 0;
        int count = 0;

        for (int row = 0; row <= UI_FALLBACK_GLYPH_H; ++row)
        {
            uint8_t starts[GLYPH_MAX_SPANS_PER_ROW];
            uint8_t ends[GLYPH_MAX_SPANS_PER_ROW];
            const int spans = (row < UI_FALLBACK_GLYPH_H) ? RowSpans(rows[row], starts, ends) : 0;

            bool matched[GLYPH_MAX_SPANS_PER_ROW] = {false};
            GlyphRun stillOpen[GLYPH_MAX_SPANS_PER_ROW];
            int stillOpenCount = 0;

            for (int i = 0; i < openCount; ++i)
            {
                int found = -1;
                for (int j = 0; j < spans; ++j)
                {
                    if (!matched[j] && starts[j] == open[i].x0 && ends[j] == open[i].x1)
                    {
                        found = j;
                        break;
                    }
                }
                if (found >= 0)
                {
                    matched[found] = true;
                    open[i].y1 = static_cast<uint8_t>(row + 1);
                    stillOpen[stillOpenCount++] = open[i];
                }
                else
                {
                    out[count++] = open[i];
                }
            }

            for (int j = 0; j < spans; ++j)
            {
                if (matched[j])
                    continue;
                GlyphRun run;
                run.x0 = starts[j];
                run.x1 = ends[j];
                run.y0 = static_cast<uint8_t>(row);
                run.y1 = static_cast<uint8_t>(row + 1);
                stillOpen[stillOpenCount++] = run;
            }

            for (int i = 0; i < stillOpenCount; ++i)
                open[i] = stillOpen[i];
            openCount = stillOpenCount;
        }

        return count;
    }
} // namespace

int Ui_TextWidth(int scale, const char* text)
{
    if (!text || scale <= 0)
        return 0;

    const Font* font = UiInternal_CookedFont();
    if (!font)
        return static_cast<int>(std::strlen(text)) * UI_FALLBACK_GLYPH_ADVANCE * scale;

    int width = 0;
    for (const char* p = text; *p; ++p)
    {
        if (*p == ' ')
        {
            width += font->spaceAdvance;
            continue;
        }
        width += Font_GetGlyph(font, static_cast<unsigned char>(*p))->advance;
    }
    return width * scale;
}

int Ui_TextHeight(int scale)
{
    const Font* font = UiInternal_CookedFont();
    const int lineHeight = font ? static_cast<int>(font->lineHeight) : static_cast<int>(UI_FALLBACK_GLYPH_H);
    return lineHeight * scale;
}

int Ui_TextFit(int scale, const char* text, int maxWidth)
{
    if (!text || scale <= 0 || maxWidth <= 0)
        return 0;

    const Font* font = UiInternal_CookedFont();
    int width = 0;
    int fitted = 0;
    for (const char* p = text; *p; ++p, ++fitted)
    {
        int advance = UI_FALLBACK_GLYPH_ADVANCE;
        if (font)
            advance = (*p == ' ') ? font->spaceAdvance : Font_GetGlyph(font, static_cast<unsigned char>(*p))->advance;
        width += advance * scale;
        if (width > maxWidth)
            return fitted;
    }
    return fitted;
}

const char* Ui_TextFitTail(int scale, const char* text, int maxWidth)
{
    if (!text || scale <= 0)
        return text;
    if (maxWidth <= 0 || Ui_TextWidth(scale, text) <= maxWidth)
        return text;

    // Removing characters from the front only ever removes non-negative width,
    // so the fitted width is monotonic in the start offset and a binary search
    // finds the longest fitting tail in O(log n) measurements rather than O(n).
    const int length = static_cast<int>(std::strlen(text));
    int lo = 0;
    int hi = length;
    while (lo < hi)
    {
        const int mid = lo + (hi - lo) / 2;
        if (Ui_TextWidth(scale, text + mid) <= maxWidth)
            hi = mid;
        else
            lo = mid + 1;
    }
    return text + lo;
}

int Ui_MeasureTextQuads(int scale, const char* text)
{
    if (!text || scale <= 0)
        return 0;

    const Font* font = UiInternal_CookedFont();
    if (font)
    {
        int glyphs = 0;
        for (const char* p = text; *p; ++p)
        {
            if (*p != ' ' && Font_GetGlyph(font, static_cast<unsigned char>(*p))->w > 0)
                ++glyphs;
        }
        return glyphs;
    }

    GlyphRun runs[GLYPH_MAX_RUNS];
    int quads = 0;
    for (const char* p = text; *p; ++p)
    {
        if (*p == ' ')
            continue;
        quads += GlyphRuns(Glyph(*p), runs);
    }
    return quads;
}

namespace
{
    /// Draw a string from the cooked font: one textured quad per glyph, taken
    /// from the atlas.
    int DrawCooked(const Font* font, uint32_t atlas, int x, int y, int scale, const char* text, UiRgba color)
    {
        const uint32_t aw = font->atlasWidth;
        const uint32_t ah = font->atlasHeight;

        for (const char* p = text; *p; ++p)
        {
            if (*p == ' ')
            {
                x += font->spaceAdvance * scale;
                continue;
            }

            const FontGlyph* g = Font_GetGlyph(font, static_cast<unsigned char>(*p));
            if (g->w > 0 && g->h > 0)
            {
                const uint16_t u0 = static_cast<uint16_t>(static_cast<uint32_t>(g->u) * 65535u / aw);
                const uint16_t v0 = static_cast<uint16_t>(static_cast<uint32_t>(g->v) * 65535u / ah);
                const uint16_t u1 = static_cast<uint16_t>(static_cast<uint32_t>(g->u + g->w) * 65535u / aw);
                const uint16_t v1 = static_cast<uint16_t>(static_cast<uint32_t>(g->v + g->h) * 65535u / ah);
                UiInternal_PushTexturedQuad(x + g->bearingX * scale, y + g->bearingY * scale, g->w * scale, g->h * scale, atlas, u0, v0, u1, v1, color);
            }
            x += g->advance * scale;
        }
        return x;
    }
} // namespace

int UiFont_Draw(int x, int y, int scale, const char* text, UiRgba color)
{
    if (!text || scale <= 0)
        return x;

    const Font* font = UiInternal_CookedFont();
    if (font)
        return DrawCooked(font, UiInternal_AtlasTexture(), x, y, scale, text, color);

    GlyphRun runs[GLYPH_MAX_RUNS];
    for (const char* p = text; *p; ++p)
    {
        if (*p == ' ')
        {
            x += UI_FALLBACK_GLYPH_ADVANCE * scale;
            continue;
        }

        const int count = GlyphRuns(Glyph(*p), runs);
        for (int i = 0; i < count; ++i)
        {
            const GlyphRun& run = runs[i];
            UiInternal_PushRect(x + run.x0 * scale, y + run.y0 * scale, (run.x1 - run.x0) * scale, (run.y1 - run.y0) * scale, color);
        }
        x += UI_FALLBACK_GLYPH_ADVANCE * scale;
    }
    return x;
}

void Engine_DrawPanicText(Renderer* renderer, int x, int y, int scale, const char* text, UiRgba color)
{
    if (!renderer || !text || scale <= 0)
        return;

    GlyphRun runs[GLYPH_MAX_RUNS];
    for (const char* p = text; *p; ++p)
    {
        if (*p == ' ')
        {
            x += UI_FALLBACK_GLYPH_ADVANCE * scale;
            continue;
        }

        const int count = GlyphRuns(Glyph(*p), runs);
        for (int i = 0; i < count; ++i)
        {
            const GlyphRun& run = runs[i];
            Quad2D quad;
            quad.x = x + run.x0 * scale;
            quad.y = y + run.y0 * scale;
            quad.w = (run.x1 - run.x0) * scale;
            quad.h = (run.y1 - run.y0) * scale;
            quad.texture = 0;
            quad.u0 = 0;
            quad.v0 = 0;
            quad.u1 = 0;
            quad.v1 = 0;
            quad.r = color.r;
            quad.g = color.g;
            quad.b = color.b;
            quad.a = color.a;
            renderer->DrawQuad2D(quad);
        }
        x += UI_FALLBACK_GLYPH_ADVANCE * scale;
    }
}
