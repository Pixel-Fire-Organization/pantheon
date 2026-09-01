#pragma once

#include "../Platform.h"

/// PlayStation Vita, handheld variant.
class VitaHandheldPlatform final : public VitaPlatform
{
public:
    PlatformId GetId() const override { return PlatformId::Vita; }
    const char* GetName() const override { return "vita"; }

    /// @param chord Which debug action to query.
    /// @return A mask using only the buttons this handheld physically has.
    uint16_t GetDebugChord(DebugChord chord) const override
    {
        switch (chord)
        {
        case DebugChord::PerfSnapshot:
            return static_cast<uint16_t>(GamepadButton::L1) | static_cast<uint16_t>(GamepadButton::R1) | static_cast<uint16_t>(GamepadButton::Select);
        case DebugChord::OverlayToggle:
            return static_cast<uint16_t>(GamepadButton::L1) | static_cast<uint16_t>(GamepadButton::R1) | static_cast<uint16_t>(GamepadButton::Start);
        case DebugChord::DebugMenu:
            return static_cast<uint16_t>(GamepadButton::Select) | static_cast<uint16_t>(GamepadButton::Start);
        case DebugChord::Count:
            break;
        }
        return 0;
    }

protected:
    bool HasTouchSurfaces() const override { return true; }

    /// @param raw Sampler word: the two shoulders arrive on the trigger bits.
    /// @return The same presses reported as the primary shoulders.
    uint16_t TranslatePadButtons(uint16_t raw) const override
    {
        uint16_t out = raw;
        if (raw & static_cast<uint16_t>(GamepadButton::L2))
            out |= static_cast<uint16_t>(GamepadButton::L1);
        if (raw & static_cast<uint16_t>(GamepadButton::R2))
            out |= static_cast<uint16_t>(GamepadButton::R1);
        return static_cast<uint16_t>(out & ~(static_cast<uint16_t>(GamepadButton::L2) | static_cast<uint16_t>(GamepadButton::R2)));
    }
};
