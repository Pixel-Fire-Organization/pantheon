#pragma once

#include <cstddef>
#include <cstdint>

// --- PSFN on-disc format (written by tools/ps2lib/font.py) ----------------
// FORMAT CONSTANTS - mirrored by the cooker, identical on every platform.
#define FONT_MAGIC 0x4E465350 /* "PSFN" in little-endian */
#define FONT_VERSION 2
#define FONT_HEADER_SIZE 28
#define FONT_GLYPH_SIZE 12

// Version 2 reclaimed the header's whiteU/whiteV texel fields -- the solid-fill
// path they served stopped sampling the atlas -- as cellCount and cellOffset, a
// second, optional table of the same 12-byte record after the glyph table:
// icons and controller glyphs, addressed by an engine-side enumerator rather
// than a codepoint. A version-1 payload is refused, never migrated.

#define FONT_FLAG_MONOSPACED 0x0001

/// Where one glyph sits in the atlas, and how the pen moves over it.
///
/// Edges are in texels; bearings and the advance are in pixels at scale 1.
struct FontGlyph
{
    uint16_t u;
    uint16_t v;
    uint8_t w;
    uint8_t h;
    int8_t bearingX;
    int8_t bearingY;
    uint8_t advance;
    uint8_t reserved[3];
};

/// A cooked font: its metrics, and the resource handle of its atlas.
///
/// The atlas is a separate texture asset named as this font's dependency, so it
/// is budgeted, uploaded and released by the ordinary texture path.
struct Font
{
    const FontGlyph* glyphs;
    const FontGlyph* cells;
    int32_t atlasResourceId;
    uint16_t glyphCount;
    uint16_t firstCode;
    uint16_t missingIndex;
    uint16_t atlasWidth;
    uint16_t atlasHeight;
    uint16_t cellCount;
    uint8_t lineHeight;
    uint8_t baseline;
    uint8_t spaceAdvance;
    uint8_t flags;
};

/// Decode a cooked font payload into an engine-native font.
///
/// The glyph table is copied into its own platform allocation, so the payload
/// buffer may be released as soon as this returns.
/// @param data The payload, immediately after the asset header.
/// @param size Payload length in bytes.
/// @param atlasResourceId Resource handle of the atlas dependency.
/// @param outFont Filled on success, untouched on failure.
/// @return False when the payload is not a font this build can read; the reason
///         is reported.
bool Font_LoadBaked(const void* data, size_t size, int32_t atlasResourceId, Font* outFont);

/// Release everything Font_LoadBaked allocated and zero the struct.
/// @param font The font to release; a null or already-released font is ignored.
void Font_FreeBaked(Font* font);

/// @param font The font to read.
/// @param codepoint The character wanted.
/// @return Its glyph, or the font's substitute glyph when it has no such
///         codepoint. Never null for a loaded font.
const FontGlyph* Font_GetGlyph(const Font* font, uint32_t codepoint);

/// @param font The font to read.
/// @param index Zero-based cell index.
/// @return The cell, or null when the font carries no cell at that index --
///         unlike a codepoint, a missing icon is an expected case with its own
///         fallback, not a substitute glyph.
const FontGlyph* Font_GetCell(const Font* font, uint8_t index);
