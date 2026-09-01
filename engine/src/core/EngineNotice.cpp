#include "core/EngineNotice.h"

#include <cstring>

#include "core/EngineAchievement.h"
#include "core/EngineDebug.h"
#include "core/EngineSubsystems.h"
#include "platform/Platform.h"
#include "ui/EngineUi.h"

namespace
{
    const int NOTICE_MARGIN_RATIO = 8;
    const int NOTICE_TITLE_SCALE = 3;

    bool s_Pending = false;
    char s_Title[64] = {0};
    char s_Reason[256] = {0};

    void Raise(const char* title, const char* reason)
    {
        strncpy(s_Title, title, sizeof(s_Title) - 1);
        s_Title[sizeof(s_Title) - 1] = '\0';
        strncpy(s_Reason, reason ? reason : "", sizeof(s_Reason) - 1);
        s_Reason[sizeof(s_Reason) - 1] = '\0';
        s_Pending = true;
        Engine_LogInfo("Notice: %s - %s", s_Title, s_Reason);
    }

} // namespace

void Engine_Notice_RequestSubsystems(Platform* platform)
{
    // A notice can only be raised where the platform has achievements at all,
    // and whether it will be is not knowable until they have been initialised.
    if (!platform || !platform->GetAchievements())
        return;

    Engine_Subsystem_Enable(EngineSubsystem::Input);
    Engine_Subsystem_Enable(EngineSubsystem::Ui);
}

void Engine_Notice_Evaluate()
{
    s_Pending = false;

    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Achievement))
        return;
    if (Engine_Achievement_GetCount() == 0)
        return;
    if (!Engine_Ui_IsActive())
        return;

    if (!Engine_Achievement_IsPersistent())
    {
        Raise("ACHIEVEMENTS WILL NOT BE SAVED",
              "THIS SYSTEM HAS NOWHERE TO WRITE THEM. YOU CAN STILL EARN THEM, "
              "BUT THEY WILL BE FORGOTTEN WHEN THE SYSTEM IS TURNED OFF.");
    }
}

bool Engine_Notice_IsPending() { return s_Pending; }

bool Engine_Notice_Update(float dt)
{
    (void)dt;

    if (!s_Pending)
        return false;

    Platform* platform = Engine_GetPlatform();
    if (!platform || !Engine_Ui_IsActive())
    {
        s_Pending = false;
        return false;
    }

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    const int margin = screenW / NOTICE_MARGIN_RATIO;

    Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);
    Ui_Text((screenW - Ui_TextWidth(NOTICE_TITLE_SCALE, s_Title)) / 2, margin / 2, NOTICE_TITLE_SCALE, s_Title, UiColor::Header);

    Ui_BeginPanel("", margin, margin, screenW - margin * 2, screenH - margin * 2);
    Ui_LabelWrapped(s_Reason, UiColor::Text);
    Ui_Separator();
    Ui_Label("THE GAME PLAYS NORMALLY.");
    Ui_Spacing(Ui_TextHeight(Ui_GetStyle().textScale));

    if (Ui_Button("CONTINUE"))
        s_Pending = false;

    Ui_EndPanel();
    return s_Pending;
}
