#include "../../include/scenes/MainMenu.h"

#include "../../include/scenes/MainScene.h"
#include "GameAPI.h"
#include "Macros.h"
#include "core/EngineDebug.h"
#include "ui/EngineUi.h"

MainMenu* MainMenu::m_instance = nullptr;

void MainMenu::OnUiStart()
{
    game::Log("Main Menu Started");
    m_mainMenuImage = game::LoadResource("TEXTURE", game::MakePath(MAIN_MENU_BG_IMAGE));
}
const game::SceneResource* MainMenu::GetResources(int* outCount) const
{
    static const game::SceneResource kResources[] = {
        {"TEXTURE", MAIN_MENU_BG_IMAGE},
    };
    *outCount = 1;
    return kResources;
}
const MainMenu* MainMenu::GetInstance()
{
    if (!m_instance)
        m_instance = new MainMenu();

    return m_instance;
}

void MainMenu::OnDrawUi(float dt)
{
    UNUSED_VAR(dt);

    switch (m_mode)
    {
    default:
        game::Log("Missing menu state");
        DrawMainMenu();
        break;
    case MainMenuMode::MainMenu:
        DrawMainMenu();
        break;
    case MainMenuMode::LoadGame:
        DrawLoadGameMenu();
        break;
    case MainMenuMode::OptionsMenu:
        DrawOptionsMenu();
        break;
    case MainMenuMode::Help:
        DrawHelpMenu();
        break;
    }

    int height = Ui_TextHeight(Ui_GetStyle().textScale);
    Ui_TextAligned(0, Ui_ScreenHeight() - height - 2, 100, 1, "Test", UiAlign::Left, UiColor::Header);
}

void MainMenu::DrawMainMenu() const
{
    Ui_SetNextPanelBackgroundImage(m_mainMenuImage);
    Ui_BeginPanel("", 0, 0, Ui_ScreenWidth(), Ui_ScreenHeight());

    auto x = Ui_CursorX();
    auto y = Ui_CursorY();

    Ui_SetCursor(Ui_ScreenWidth() / 2.f - 150 / 2.f, Ui_ScreenHeight() / 2.f - Ui_TextHeight(Ui_GetStyle().textScale));
    Ui_SetNextItemWidth(150);
    if (Ui_Button("Start Game"))
    {
        game::SwitchScene((Scene*)MainScene::GetInstance());
    }
    Ui_SetCursor(x, y);

    Ui_EndPanel();
}
void MainMenu::DrawLoadGameMenu() { Engine_LogError("Not implemented: %s", __func__); }
void MainMenu::DrawOptionsMenu() { Engine_LogError("Not implemented: %s", __func__); }
void MainMenu::DrawHelpMenu() { Engine_LogError("Not implemented: %s", __func__); }
