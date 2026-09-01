#pragma once

#include "../../../engine/include/scenes/UIScene.h"

enum class MainMenuMode
{
    MainMenu,
    LoadGame,
    OptionsMenu,
    Help
};

class MainMenu final : public game::UiScene
{
protected:
    void OnDrawUi(float dt) override;
    void OnUiStart() override;

public:
    const game::SceneResource* GetResources(int* outCount) const override;
    const static MainMenu* GetInstance();

private:
    const char* MAIN_MENU_BG_IMAGE = "RASSETS\\MAINMENU.PS2A";
    int m_mainMenuImage = -1;
    MainMenuMode m_mode = MainMenuMode::MainMenu;

    static MainMenu* m_instance;

    void DrawMainMenu() const;
    void DrawLoadGameMenu();
    void DrawOptionsMenu();
    void DrawHelpMenu();
};
