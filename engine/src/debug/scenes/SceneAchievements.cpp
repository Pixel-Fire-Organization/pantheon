#include <cstdio>

#include "core/EngineAchievement.h"
#include "core/EngineSubsystems.h"
#include "ui/EngineUi.h"
#include "debug/TestbedScene.h"
#include "platform/Platform.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;
    const uint32_t IDS_SHOWN = 8;

    int s_LastId = -1;
    bool s_LastOk = false;
} // namespace

void Scene_Achievements_Init()
{
    s_LastId = -1;
    s_LastOk = false;
}

void Scene_Achievements_Update(float dt)
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

    Ui_BeginPanel("ACHIEVEMENTS", PANEL_MARGIN, PANEL_MARGIN, width, height);

    const bool available = Engine_Achievement_IsAvailable();
    const uint32_t count = Engine_Achievement_GetCount();
    const char* reason = Engine_Achievement_GetMirrorReason();

    Ui_LabelValue("RECORDING", available ? "YES" : "NO");
    Ui_LabelValue("SAVED TO DISC", Engine_Achievement_IsPersistent() ? "YES" : "NO");
    Ui_LabelValue("MIRRORED", Engine_Achievement_IsMirrored() ? "YES" : "NO");

    char text[48];
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(count));
    Ui_LabelValue("DECLARED", text);
    const int32_t consoleCount = Engine_Achievement_GetConsoleCount();
    if (consoleCount < 0)
    {
        Ui_LabelValue("CONSOLE HAS", "WOULD NOT SAY");
    }
    else
    {
        snprintf(text, sizeof(text), "%d", static_cast<int>(consoleCount));
        Ui_LabelValue("CONSOLE HAS", text);
    }

    if (reason)
    {
        Ui_Separator();
        Ui_LabelColored("NOT MIRRORED BECAUSE", UiColor::TextWarn);
        Ui_LabelWrapped(reason, UiColor::TextDim);
    }

    Ui_Separator();

    if (Ui_Button("SHOW ACHIEVEMENTS SCREEN"))
        Engine_Achievement_OpenScreen();
    Ui_Separator();

    const uint32_t shown = (count < IDS_SHOWN) ? count : IDS_SHOWN;
    for (uint32_t id = 0; id < shown; ++id)
    {
        const bool unlocked = Engine_Achievement_IsUnlocked(id);

        if (Ui_SelectableValue(Engine_Achievement_GetName(id), unlocked ? "EARNED" : "UNLOCK", unlocked) && available)
        {
            s_LastId = static_cast<int>(id);
            s_LastOk = Engine_Achievement_Unlock(id);
        }
    }
    if (count == 0)
        Ui_LabelColored("THIS TITLE DECLARES NONE", UiColor::TextDim);

    if (s_LastId >= 0)
    {
        char outcome[48];
        snprintf(outcome, sizeof(outcome), "ACHIEVEMENT %d", s_LastId);
        Ui_Separator();
        Ui_LabelValue(outcome, s_LastOk ? "RECORDED" : "REFUSED");
        if (!s_LastOk)
            Ui_LabelWrapped("REFUSED IS REPORTED, NOT SILENT. THE LOG CARRIES THE REASON.", UiColor::TextDim);
    }
    Ui_EndPanel();

    Ui_BeginPanel("NOTES", PANEL_MARGIN + width + COLUMN_GAP, PANEL_MARGIN, width, height);
    Ui_LabelValue("SUBSYSTEM", Engine_Subsystem_IsEnabled(EngineSubsystem::Achievement) ? "REQUESTED" : "NOT REQUESTED");
    Ui_Separator();
    Ui_Label("DECLARED IS WHAT THIS");
    Ui_Label("TITLE PACKAGED.");
    Ui_Label("RECORDING IS THE ENGINE.");
    Ui_Label("MIRRORED IS THE CONSOLE.");
    Ui_Label("THEY FAIL SEPARATELY AND");
    Ui_Label("NEED OPPOSITE FIXES.");
    Ui_Separator();
    Ui_Label("UNLOCKING IS ONE WAY");
    Ui_Label("AND IDEMPOTENT.");
    Ui_Label("NO PROGRESS, NO TIERS,");
    Ui_Label("NO TIMESTAMPS.");
    Ui_EndPanel();
}
