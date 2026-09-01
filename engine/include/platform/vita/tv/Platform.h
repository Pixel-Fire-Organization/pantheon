#pragma once

#include "../Platform.h"

/// PlayStation TV, the set-top variant of the Vita family.
class VitaTvPlatform final : public VitaPlatform
{
public:
    PlatformId GetId() const override { return PlatformId::VitaTv; }
    const char* GetName() const override { return "vitatv"; }

    /// @param chord Which debug action to query.
    /// @return The full-pad mask; this variant is driven by a wireless
    ///         controller that has both shoulder rows and both stick clicks.
    uint16_t GetDebugChord(DebugChord chord) const override
    {
        switch (chord)
        {
        case DebugChord::PerfSnapshot:
            return static_cast<uint16_t>(GamepadButton::L1) | static_cast<uint16_t>(GamepadButton::L2) | static_cast<uint16_t>(GamepadButton::R1) | static_cast<uint16_t>(GamepadButton::R2);
        case DebugChord::OverlayToggle:
            return static_cast<uint16_t>(GamepadButton::L1) | static_cast<uint16_t>(GamepadButton::L2) | static_cast<uint16_t>(GamepadButton::L3) | static_cast<uint16_t>(GamepadButton::R3);
        case DebugChord::DebugMenu:
            return static_cast<uint16_t>(GamepadButton::Select) | static_cast<uint16_t>(GamepadButton::Start);
        case DebugChord::Count:
            break;
        }
        return 0;
    }

protected:
    bool HasTouchSurfaces() const override { return false; }

    uint16_t TranslatePadButtons(uint16_t raw) const override { return raw; }
};
