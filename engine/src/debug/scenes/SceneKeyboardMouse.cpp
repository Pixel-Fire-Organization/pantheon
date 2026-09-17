#include <cstdio>

#include "core/EngineInput.h"
#include "debug/TestbedScene.h"
#include "platform/Platform.h"
#include "ui/EngineUi.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;
    const int HELD_KEYS_SHOWN = 8;

    const char* KeyName(KeyboardKey key)
    {
        const uint16_t index = static_cast<uint16_t>(key);
        static char name[8];

        if (key >= KeyboardKey::A && key <= KeyboardKey::Z)
        {
            name[0] = static_cast<char>('A' + (index - static_cast<uint16_t>(KeyboardKey::A)));
            name[1] = '\0';
            return name;
        }
        if (key >= KeyboardKey::Num0 && key <= KeyboardKey::Num9)
        {
            name[0] = static_cast<char>('0' + (index - static_cast<uint16_t>(KeyboardKey::Num0)));
            name[1] = '\0';
            return name;
        }

        switch (key)
        {
        case KeyboardKey::Space:
            return "SPACE";
        case KeyboardKey::Enter:
            return "ENTER";
        case KeyboardKey::Escape:
            return "ESC";
        case KeyboardKey::Tab:
            return "TAB";
        case KeyboardKey::Backspace:
            return "BKSP";
        case KeyboardKey::Left:
            return "LEFT";
        case KeyboardKey::Right:
            return "RIGHT";
        case KeyboardKey::Up:
            return "UP";
        case KeyboardKey::Down:
            return "DOWN";
        case KeyboardKey::LeftShift:
            return "LSHIFT";
        case KeyboardKey::RightShift:
            return "RSHIFT";
        case KeyboardKey::LeftControl:
            return "LCTRL";
        case KeyboardKey::RightControl:
            return "RCTRL";
        default:
            break;
        }

        snprintf(name, sizeof(name), "K%u", static_cast<unsigned>(index));
        return name;
    }
} // namespace

void Scene_KeyboardMouse_Init() {}

void Scene_KeyboardMouse_Update(float dt)
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

    Ui_BeginPanel("KEYBOARD", PANEL_MARGIN, PANEL_MARGIN, width, height);
    if (!platform->HasCapability(PlatformCapability::Keyboard))
    {
        Testbed_DrawUnavailable("KEYBOARD");
    }
    else
    {
        int held = 0;
        for (uint16_t k = 1; k < static_cast<uint16_t>(KeyboardKey::Count) && held < HELD_KEYS_SHOWN; ++k)
        {
            const KeyboardKey key = static_cast<KeyboardKey>(k);
            if (!IsKeyDown(key))
                continue;
            Ui_LabelValue(KeyName(key), "DOWN");
            ++held;
        }
        if (held == 0)
            Ui_LabelColored("NO KEYS HELD", UiColor::TextDim);

        Ui_Separator();
        Ui_Label("HOLD KEYS TO SEE THEM");
        Ui_Label("TAP FAST TO TEST EDGES");
        Ui_Label("UNFOCUS TO TEST CLEARING");
    }
    Ui_EndPanel();

    Ui_BeginPanel("MOUSE", PANEL_MARGIN + width + COLUMN_GAP, PANEL_MARGIN, width, height);
    if (!platform->HasCapability(PlatformCapability::Mouse))
    {
        Testbed_DrawUnavailable("MOUSE");
        Ui_EndPanel();
        return;
    }

    const Vector2 position = GetMousePosition();
    const Vector2 delta = GetMouseDelta();

    char text[48];
    snprintf(text, sizeof(text), "%.0f %.0f", static_cast<double>(position.x), static_cast<double>(position.y));
    Ui_LabelValue("POSITION", text);
    snprintf(text, sizeof(text), "%+.0f %+.0f", static_cast<double>(delta.x), static_cast<double>(delta.y));
    Ui_LabelValue("DELTA", text);
    snprintf(text, sizeof(text), "%+.1f", static_cast<double>(GetMouseWheelDelta()));
    Ui_LabelValue("WHEEL", text);
    Ui_Separator();
    Ui_Header("BUTTONS");
    Ui_LabelValue("LEFT", IsMouseButtonDown(MouseButton::Left) ? "DOWN" : "-");
    Ui_LabelValue("RIGHT", IsMouseButtonDown(MouseButton::Right) ? "DOWN" : "-");
    Ui_LabelValue("MIDDLE", IsMouseButtonDown(MouseButton::Middle) ? "DOWN" : "-");
    Ui_EndPanel();
}
