#pragma once

#include "../Platform.h"

// ---------------------------------------------------------------------------
// PlayStation 2, PAL region (50 Hz). Concrete and final: it supplies identity
// and nothing else. Every behaviour lives in Ps2Platform, and the region's
// screen size and frame budget come from this directory's PlatformConstants.h,
// resolved at compile time - PAL and NTSC ship as separate binaries.
// ---------------------------------------------------------------------------
class Ps2PalPlatform final : public Ps2Platform
{
public:
    PlatformId GetId() const override { return PlatformId::Ps2Pal; }
    const char* GetName() const override { return "ps2pal"; }
};
