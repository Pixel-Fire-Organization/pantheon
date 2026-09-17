#include <cstdio>

#include "core/EngineDebug.h"
#include "core/EngineInput.h"
#include "debug/TestbedScene.h"
#include "platform/Platform.h"
#include "ui/EngineUi.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;

    const GamepadButton kDisplayOrder[] = {
        GamepadButton::L1,     GamepadButton::L2,       GamepadButton::R1,       GamepadButton::R2,        GamepadButton::L3,       GamepadButton::R3,     GamepadButton::Select, GamepadButton::Start,
        GamepadButton::DPadUp, GamepadButton::DPadDown, GamepadButton::DPadLeft, GamepadButton::DPadRight, GamepadButton::Triangle, GamepadButton::Circle, GamepadButton::Cross,  GamepadButton::Square,
    };

    const int kDisplayCount = static_cast<int>(sizeof(kDisplayOrder) / sizeof(kDisplayOrder[0]));

    /// Show one intent as the buttons this platform answered with, and how much
    /// of it is currently held. Not "held: yes/no": the chord that opens the
    /// testbed cannot be held while looking at this.
    /// @param platform The running platform.
    /// @param chord Which intent to show.
    /// @param label Its name.
    void Intent(const Platform& platform, DebugChord chord, const char* label)
    {
        char text[64];
        Engine_Debug_DescribeChord(chord, text, sizeof(text));
        Ui_Header(label);
        Ui_LabelValue("BUTTONS", text);

        const uint16_t mask = platform.GetDebugChord(chord);
        snprintf(text, sizeof(text), "%04X", static_cast<unsigned>(mask));
        Ui_LabelValue("MASK", text);

        if (mask == 0u)
        {
            Ui_LabelColored("UNAVAILABLE HERE", UiColor::TextWarn);
            return;
        }

        int total = 0;
        int down = 0;
        for (int i = 0; i < kDisplayCount; ++i)
        {
            if ((mask & static_cast<uint16_t>(kDisplayOrder[i])) == 0u)
                continue;
            ++total;
            if (IsGamePadButtonPressed(0, kDisplayOrder[i]))
                ++down;
        }
        snprintf(text, sizeof(text), "%d OF %d", down, total);
        Ui_LabelValue("HELD", text);
    }
} // namespace

void Scene_Chords_Init() {}

void Scene_Chords_Update(float dt)
{
    (void)dt;

    const Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    const int width = (screenW - PANEL_MARGIN * 2 - COLUMN_GAP) / 2;
    const int height = screenH - PANEL_MARGIN * 2;

    Ui_BeginPanel("DEBUG CHORDS", PANEL_MARGIN, PANEL_MARGIN, width, height);
    Intent(*platform, DebugChord::PerfSnapshot, "PERF SNAPSHOT");
    Ui_Spacing(4);
    Intent(*platform, DebugChord::OverlayToggle, "OVERLAY TOGGLE");
    Ui_Spacing(4);
    Intent(*platform, DebugChord::DebugMenu, "DEBUG MENU");
    Ui_EndPanel();

    Ui_BeginPanel("LIVE BUTTONS", PANEL_MARGIN + width + COLUMN_GAP, PANEL_MARGIN, width, height);
    for (int i = 0; i < kDisplayCount; ++i)
        Ui_LabelValue(Platform_GamepadButtonName(kDisplayOrder[i]), IsGamePadButtonPressed(0, kDisplayOrder[i]) ? "DOWN" : "-");
    Ui_EndPanel();
}
