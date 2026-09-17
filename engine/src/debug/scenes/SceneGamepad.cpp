#include <cstdio>

#include "core/EngineInput.h"
#include "debug/TestbedScene.h"
#include "platform/Platform.h"
#include "ui/EngineUi.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;

    const GamepadButton kButtons[] = {
        GamepadButton::Select, GamepadButton::L3, GamepadButton::R3, GamepadButton::Start, GamepadButton::DPadUp,   GamepadButton::DPadRight, GamepadButton::DPadDown, GamepadButton::DPadLeft,
        GamepadButton::L2,     GamepadButton::R2, GamepadButton::L1, GamepadButton::R1,    GamepadButton::Triangle, GamepadButton::Circle,    GamepadButton::Cross,    GamepadButton::Square,
    };

    const int kButtonCount = static_cast<int>(sizeof(kButtons) / sizeof(kButtons[0]));

    uint8_t s_Port = 0;

    void AxisRow(const char* label, Vector2 value)
    {
        char text[48];
        snprintf(text, sizeof(text), "%+.2f %+.2f", static_cast<double>(value.x), static_cast<double>(value.y));
        Ui_LabelValue(label, text);
    }
} // namespace

void Scene_Gamepad_Init() { s_Port = 0; }

void Scene_Gamepad_Update(float dt)
{
    (void)dt;

    const Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    const uint8_t ports = static_cast<uint8_t>(platform->GetConstant(PlatformConstant::MaxGamepadPorts));
    if (WasGamePadButtonPressed(0, GamepadButton::R1) && ports > 0)
        s_Port = static_cast<uint8_t>((s_Port + 1) % ports);
    if (WasGamePadButtonPressed(0, GamepadButton::L1) && ports > 0)
        s_Port = static_cast<uint8_t>((s_Port + ports - 1) % ports);

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    const int width = (screenW - PANEL_MARGIN * 2 - COLUMN_GAP) / 2;
    const int height = screenH - PANEL_MARGIN * 2;

    char title[48];
    snprintf(title, sizeof(title), "PAD %u OF %u", static_cast<unsigned>(s_Port), static_cast<unsigned>(ports));
    Ui_BeginPanel(title, PANEL_MARGIN, PANEL_MARGIN, width, height);

    if (!platform->HasCapability(PlatformCapability::Gamepad))
    {
        Testbed_DrawUnavailable("GAMEPAD");
        Ui_EndPanel();
        return;
    }

    Ui_LabelValue("CONNECTED", IsGamePadInitialized(s_Port) ? "YES" : "NO");

    char mask[32];
    uint16_t bits = 0;
    for (int i = 0; i < kButtonCount; ++i)
    {
        if (IsGamePadButtonPressed(s_Port, kButtons[i]))
            bits = static_cast<uint16_t>(bits | static_cast<uint16_t>(kButtons[i]));
    }
    snprintf(mask, sizeof(mask), "%04X", static_cast<unsigned>(bits));
    Ui_LabelValue("MASK", mask);
    Ui_Separator();
    Ui_Header("STICKS AND TRIGGERS");
    AxisRow("LEFT", GetGamePadAxis(s_Port, GamepadStick::Left));
    AxisRow("RIGHT", GetGamePadAxis(s_Port, GamepadStick::Right));

    char trigger[48];
    snprintf(trigger, sizeof(trigger), "%.2f %.2f", static_cast<double>(GetGamePadTrigger(s_Port, GamepadTrigger::Left)), static_cast<double>(GetGamePadTrigger(s_Port, GamepadTrigger::Right)));
    Ui_LabelValue("TRIGGERS", trigger);
    Ui_LabelValue("ANALOG", platform->HasCapability(PlatformCapability::AnalogTriggers) ? "YES" : "DIGITAL ONLY");
    Ui_Label("L1 R1 CHANGE PORT");
    Ui_EndPanel();

    Ui_BeginPanel("BUTTONS", PANEL_MARGIN * 2 + width - PANEL_MARGIN + COLUMN_GAP, PANEL_MARGIN, width, height);
    for (int i = 0; i < kButtonCount; ++i)
    {
        const bool down = IsGamePadButtonPressed(s_Port, kButtons[i]);
        Ui_LabelValue(Platform_GamepadButtonName(kButtons[i]), down ? "DOWN" : "-");
    }
    Ui_EndPanel();
}
