#include "ui/EngineAchievementUi.h"

#include <cstdio>
#include <cstring>

#include "core/EngineAchievement.h"
#include "ui/EngineUi.h"

namespace
{
    const float TOAST_SECONDS = 4.0f;
    const int SCREEN_MARGIN_RATIO = 10;

    bool s_ScreenOpen = false;

    const char* GradeName(AchievementGrade grade)
    {
        switch (grade)
        {
        case AchievementGrade::Platinum:
            return "PLATINUM";
        case AchievementGrade::Gold:
            return "GOLD";
        case AchievementGrade::Silver:
            return "SILVER";
        case AchievementGrade::Bronze:
            return "BRONZE";
        }
        return "BRONZE";
    }

    void DrawScreen()
    {
        const uint32_t count = Engine_Achievement_GetCount();
        const int margin = Ui_ScreenWidth() / SCREEN_MARGIN_RATIO;

        Ui_BeginPanel("ACHIEVEMENTS", margin, margin, Ui_ScreenWidth() - margin * 2, Ui_ScreenHeight() - margin * 2);

        uint32_t earned = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (Engine_Achievement_IsUnlocked(i))
                ++earned;
        }
        Ui_LabelValueFormat("EARNED", "%u OF %u", static_cast<unsigned>(earned), static_cast<unsigned>(count));
        Ui_Separator();

        const int listHeight = Ui_ContentHeight() - Ui_TextHeight(Ui_GetStyle().textScale) * 3;
        if (Ui_BeginScroll("list", listHeight > 0 ? listHeight : Ui_ContentHeight()))
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const bool unlocked = Engine_Achievement_IsUnlocked(i);
                const bool concealed = Engine_Achievement_IsHidden(i) && !unlocked;

                Ui_LabelValue(concealed ? "HIDDEN" : Engine_Achievement_GetName(i), unlocked ? "EARNED" : "LOCKED");
                if (!concealed)
                    Ui_LabelColored(Engine_Achievement_GetDetail(i), UiColor::TextDim);
            }
            Ui_EndScroll();
        }

        Ui_Separator();
        if (Ui_Button("CLOSE") || Ui_WasBackPressed())
            Engine_Achievement_CloseScreen();

        Ui_EndPanel();
    }
} // namespace

void Engine_AchievementUi_Notify(uint32_t id)
{
    char text[96];
    snprintf(text, sizeof(text), "%s  %s", GradeName(Engine_Achievement_GetGrade(id)), Engine_Achievement_GetName(id));
    Ui_Toast(text, TOAST_SECONDS);
}

void Engine_AchievementUi_Reset() { s_ScreenOpen = false; }

void Engine_Achievement_OpenScreen() { s_ScreenOpen = true; }

void Engine_Achievement_CloseScreen() { s_ScreenOpen = false; }

bool Engine_Achievement_IsScreenOpen() { return s_ScreenOpen; }

void Engine_Achievement_Update(float dt)
{
    if (!Engine_Ui_IsActive())
        return;

    (void)dt;
    if (s_ScreenOpen)
        DrawScreen();
}
