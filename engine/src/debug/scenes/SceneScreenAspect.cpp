#include <cstdio>

#include "debug/TestbedScene.h"
#include "platform/Platform.h"
#include "ui/EngineUi.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int PANEL_WIDTH = 240;
    const int SAFE_PERCENT = 10;
    const int CROSS_ARM = 40;
    const int RULE = 2;
} // namespace

void Scene_ScreenAspect_Init() {}

void Scene_ScreenAspect_Update(float dt)
{
    (void)dt;

    const Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    // Read every frame, not once: this platform's framebuffer can be resized
    // under the running engine, and a cached size is wrong the moment it is.
    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    const uint32_t aspectX = platform->GetConstant(PlatformConstant::DisplayAspectX);
    const uint32_t aspectY = platform->GetConstant(PlatformConstant::DisplayAspectY);

    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    Ui_RectOutline(0, 0, screenW, screenH, RULE, UiColor::Border);

    const int insetX = (screenW * SAFE_PERCENT) / 100;
    const int insetY = (screenH * SAFE_PERCENT) / 100;
    Ui_RectOutline(insetX, insetY, screenW - insetX * 2, screenH - insetY * 2, RULE, UiColor::TextAccent);

    const int centreX = screenW / 2;
    const int centreY = screenH / 2;
    Ui_Rect(centreX - CROSS_ARM, centreY - RULE / 2, CROSS_ARM * 2, RULE, UiColor::Header);
    Ui_Rect(centreX - RULE / 2, centreY - CROSS_ARM, RULE, CROSS_ARM * 2, UiColor::Header);

    // A square on the display, not in the framebuffer. Pixels are not square on
    // either console, so a framebuffer-square box is visibly a rectangle there.
    const int boxH = screenH / 3;
    int boxW = boxH;
    if (aspectX > 0 && aspectY > 0)
        boxW = static_cast<int>((static_cast<long long>(boxH) * screenH * aspectX) / (static_cast<long long>(screenW) * aspectY));
    Ui_RectOutline(centreX - boxW / 2, centreY - boxH / 2, boxW, boxH, RULE, UiColor::BarFill);

    Ui_BeginPanel("SCREEN", PANEL_MARGIN, PANEL_MARGIN, PANEL_WIDTH, screenH / 2);

    char text[48];
    snprintf(text, sizeof(text), "%dX%d", screenW, screenH);
    Ui_LabelValue("FRAMEBUFFER", text);
    snprintf(text, sizeof(text), "%u:%u", static_cast<unsigned>(aspectX), static_cast<unsigned>(aspectY));
    Ui_LabelValue("DISPLAY", text);
    snprintf(text, sizeof(text), "%dX%d", boxW, boxH);
    Ui_LabelValue("SQUARE BOX", text);
    Ui_LabelValue("RESIZABLE", platform->HasCapability(PlatformCapability::ResizableWindow) ? "YES" : "NO");
    Ui_Separator();
    Ui_Label("YELLOW IS TITLE SAFE");
    Ui_Label("GREEN SHOULD LOOK SQUARE");
    Ui_Label("ON THE DISPLAY");
    Ui_EndPanel();
}
