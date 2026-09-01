#pragma once

#include <cstdint>

#include "PlatformConstants.h"

/// One screen-space, axis-aligned quad of the interface.
///
/// Positions and sizes are whole pixels in framebuffer space. Texture
/// coordinates are normalised across the full unsigned range and are only read
/// when `texture` is non-zero; a zero texture means a solid colour fill.
///
/// `texture` is a *backend* handle, not a resource handle: the interface
/// resolves its atlas once per frame, so translating a batch performs no
/// resource lookups.
struct UiQuad
{
    uint32_t texture;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint16_t u0;
    uint16_t v0;
    uint16_t u1;
    uint16_t v1;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

/// The interface submitted for one frame: quads in draw order, back to front.
///
/// Filled by the UI subsystem during the update phase and handed to the active
/// renderer once, so a backend translates a batch rather than servicing calls.
class UI
{
  public:
    /// Drop every quad and the overflow count, ready for a new frame.
    void Reset();

    /// Append one quad in draw order.
    /// @param quad The quad to append.
    /// @return False when the frame budget is already full, which also counts
    ///         the quad as dropped.
    bool Add(const UiQuad& quad);

    /// @return The quads for this frame, in draw order.
    const UiQuad* Quads() const { return m_quads; }

    /// @return How many entries of Quads() are valid.
    uint32_t Count() const { return m_count; }

    /// @return How many quads were refused this frame because the budget was
    ///         full. Reported once per frame, never once per quad.
    uint32_t Dropped() const { return m_dropped; }

    /// @return The per-frame quad budget this platform allows.
    static uint32_t Capacity() { return UI_MAX_QUADS; }

  private:
    UiQuad m_quads[UI_MAX_QUADS];
    uint32_t m_count = 0;
    uint32_t m_dropped = 0;
};
