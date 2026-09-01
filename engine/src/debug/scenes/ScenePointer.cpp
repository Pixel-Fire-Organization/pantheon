#include <cstdio>

#include "ui/EngineUi.h"
#include "debug/TestbedScene.h"
#include "platform/Platform.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;
    const int TRAIL_LENGTH = 64;

    float s_TrailX[TRAIL_LENGTH];
    float s_TrailY[TRAIL_LENGTH];
    int s_TrailNext = 0;
    int s_TrailUsed = 0;

    const char* SourceName(UiPointerSource source)
    {
        switch (source)
        {
        case UiPointerSource::None:
            return "NONE";
        case UiPointerSource::Stick:
            return "LEFT STICK";
        case UiPointerSource::Mouse:
            return "MOUSE";
        case UiPointerSource::Touch:
            return "FRONT TOUCH";
        case UiPointerSource::Count:
            break;
        }
        return "?";
    }
} // namespace

void Scene_Pointer_Init()
{
    s_TrailNext = 0;
    s_TrailUsed = 0;
}

void Scene_Pointer_Update(float dt)
{
    (void)dt;

    const Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    const UiPointer& pointer = Ui_GetPointer();

    s_TrailX[s_TrailNext] = pointer.x / static_cast<float>(screenW);
    s_TrailY[s_TrailNext] = pointer.y / static_cast<float>(screenH);
    s_TrailNext = (s_TrailNext + 1) % TRAIL_LENGTH;
    if (s_TrailUsed < TRAIL_LENGTH)
        ++s_TrailUsed;

    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    const int width = (screenW - PANEL_MARGIN * 2 - COLUMN_GAP) / 2;
    const int height = screenH - PANEL_MARGIN * 2;

    Ui_BeginPanel("POINTER", PANEL_MARGIN, PANEL_MARGIN, width, height);

    char text[48];
    snprintf(text, sizeof(text), "%.0f %.0f", static_cast<double>(pointer.x), static_cast<double>(pointer.y));
    Ui_LabelValue("POSITION", text);
    Ui_LabelValue("SOURCE", SourceName(pointer.source));
    Ui_LabelValue("VISIBLE", pointer.visible ? "YES" : "HIDDEN BY DPAD");
    Ui_LabelValue("DOWN", pointer.down ? "YES" : "NO");
    snprintf(text, sizeof(text), "%.0f PX PER SEC", static_cast<double>(pointer.stickSpeed));
    Ui_LabelValue("STICK SPEED", text);
    Ui_Separator();
    Ui_Header("SOURCES HERE");
    Ui_LabelValue("LEFT STICK", "ALWAYS");
    Ui_LabelValue("MOUSE", platform->HasCapability(PlatformCapability::Mouse) ? "YES" : "NO");
    Ui_LabelValue("FRONT TOUCH", platform->HasCapability(PlatformCapability::Touch) ? "YES" : "NO");
    Ui_EndPanel();

    Ui_BeginPanel("TRAIL", PANEL_MARGIN + width + COLUMN_GAP, PANEL_MARGIN, width, height);
    Ui_BeginPointBox("LAST POSITIONS", height / 2);
    for (int i = 0; i < s_TrailUsed; ++i)
        Ui_Point(s_TrailX[i], s_TrailY[i], UiColor::BarFill);
    Ui_EndPointBox();
    Ui_Separator();
    Ui_Label("HOLD THE STICK TO RAMP");
    Ui_Label("DPAD HIDES THE CURSOR");
    Ui_EndPanel();
}
