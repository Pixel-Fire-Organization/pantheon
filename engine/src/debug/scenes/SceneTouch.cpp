#include <cstdio>

#include "ui/EngineUi.h"
#include "debug/TestbedScene.h"
#include "platform/Platform.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;
    const int BOX_HEIGHT = 120;
    const int CONTACTS_LISTED = 3;

    /// Draw one touch surface as its own box, because the two have no shared
    /// coordinate space: a rear pad is behind the device and maps to nothing on
    /// screen.
    /// @param platform The running platform.
    /// @param surface Which surface to show.
    /// @param label Its heading.
    void Surface(Platform& platform, TouchSurface surface, const char* label)
    {
        const uint8_t count = platform.Touch_GetContactCount(surface);

        char heading[48];
        snprintf(heading, sizeof(heading), "%s CONTACTS %u", label, static_cast<unsigned>(count));
        Ui_BeginPointBox(heading, BOX_HEIGHT);
        for (uint8_t i = 0; i < count; ++i)
        {
            TouchContact contact;
            if (platform.Touch_GetContact(surface, i, &contact))
                Ui_Point(contact.position.x, contact.position.y, UiColor::TextAccent);
        }
        Ui_EndPointBox();

        for (uint8_t i = 0; i < count && i < CONTACTS_LISTED; ++i)
        {
            TouchContact contact;
            if (!platform.Touch_GetContact(surface, i, &contact))
                continue;

            char name[16];
            char value[48];
            snprintf(name, sizeof(name), "ID %u", static_cast<unsigned>(contact.id));
            snprintf(value, sizeof(value), "%.2f %.2f F%.2f", static_cast<double>(contact.position.x), static_cast<double>(contact.position.y),
                     static_cast<double>(contact.force));
            Ui_LabelValue(name, value);
        }
    }
} // namespace

void Scene_Touch_Init() {}

void Scene_Touch_Update(float dt)
{
    (void)dt;

    Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

    const int width = (screenW - PANEL_MARGIN * 2 - COLUMN_GAP) / 2;
    const int height = screenH - PANEL_MARGIN * 2;

    Ui_BeginPanel("TOUCH", PANEL_MARGIN, PANEL_MARGIN, width, height);
    if (!platform->HasCapability(PlatformCapability::Touch))
    {
        Testbed_DrawUnavailable("TOUCH SURFACES");
        Ui_Label("POSITIONS WOULD BE 0 TO 1");
        Ui_Label("NEVER PIXELS");
        Ui_EndPanel();
        return;
    }

    Surface(*platform, TouchSurface::Front, "FRONT");
    Ui_EndPanel();

    Ui_BeginPanel("REAR SURFACE", PANEL_MARGIN + width + COLUMN_GAP, PANEL_MARGIN, width, height);
    Surface(*platform, TouchSurface::Rear, "REAR");
    Ui_Separator();
    Ui_Label("REAR IS NOT A POINTER");
    Ui_Label("IT MAPS TO NO PIXELS");
    Ui_EndPanel();
}
