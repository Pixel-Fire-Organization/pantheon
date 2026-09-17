#include "debug/EngineTestbed.h"

#include <cstdio>

#include "GameAPI.h"
#include "core/EngineAction.h"
#include "core/EngineApp.h"
#include "core/EngineCore.h"
#include "core/EngineDebug.h"
#include "core/EngineInput.h"
#include "core/EngineSubsystems.h"
#include "debug/TestbedScene.h"
#include "platform/Platform.h"
#include "ui/EngineUi.h"

namespace
{
    enum class Mode : uint8_t
    {
        Closed = 0,
        Menu,
        Scene
    };

    const int MENU_MARGIN = 16;
    const int MENU_TITLE_HEIGHT = 34;
    const int MENU_FOOTER_HEIGHT = 26;
    const int MENU_COLUMN_GAP = 8;
    const int EXIT_CHOSEN = -2;

    bool s_Enabled = false;
    Mode s_Mode = Mode::Closed;
    int s_ActiveScene = -1;
    char s_ChordText[64] = "EngineDebugMenu";

    void LeaveActiveScene()
    {
        if (s_ActiveScene < 0)
            return;

        int count = 0;
        const TestbedScene* scenes = Testbed_Catalogue(&count);
        if (s_ActiveScene < count && scenes[s_ActiveScene].shutdown)
            scenes[s_ActiveScene].shutdown();
        s_ActiveScene = -1;
    }

    void EnterScene(int index)
    {
        int count = 0;
        const TestbedScene* scenes = Testbed_Catalogue(&count);
        if (index < 0 || index >= count)
            return;

        Engine_ResetRuntimeState();
        s_ActiveScene = index;
        s_Mode = Mode::Scene;
        Engine_LogInfo("[Testbed] entering '%s'", scenes[index].name);
        if (scenes[index].init)
            scenes[index].init();
    }

    void OpenMenu()
    {
        LeaveActiveScene();
        Engine_ResetRuntimeState();
        s_Mode = Mode::Menu;
    }

    void CloseToGame()
    {
        LeaveActiveScene();
        s_Mode = Mode::Closed;
        Engine_ResetRuntimeState();
        Engine_LogInfo("[Testbed] closed; restarting the game");
        GameInit();
    }

    /// Draw one column of the catalogue.
    /// @param x Left edge in screen pixels.
    /// @param y Top edge in screen pixels.
    /// @param w Column width in pixels.
    /// @param h Column height in pixels.
    /// @param first First category to draw in this column.
    /// @param last One past the last category to draw in this column.
    /// @return The scene index the player chose, or -1.
    int DrawColumn(int x, int y, int w, int h, int first, int last)
    {
        int chosen = -1;
        int count = 0;
        const TestbedScene* scenes = Testbed_Catalogue(&count);

        Ui_BeginPanel("", x, y, w, h);
        for (int category = first; category < last; ++category)
        {
            const TestbedCategory group = static_cast<TestbedCategory>(category);
            bool headerDrawn = false;
            for (int i = 0; i < count; ++i)
            {
                if (scenes[i].category != group)
                    continue;
                if (!headerDrawn)
                {
                    Ui_Header(Testbed_CategoryName(group));
                    headerDrawn = true;
                }
                if (Ui_Selectable(scenes[i].name, false))
                    chosen = i;
            }
        }
        if (first == 0)
        {
            Ui_Spacing(8);
            if (Ui_Button("EXIT GAME"))
                chosen = EXIT_CHOSEN;
        }
        Ui_EndPanel();
        return chosen;
    }

    void DrawMenu()
    {
        const Platform* platform = Engine_GetPlatform();
        const int screenW = platform ? static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth)) : GFX_SCREEN_WIDTH;
        const int screenH = platform ? static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight)) : GFX_SCREEN_HEIGHT;

        Ui_Rect(0, 0, screenW, screenH, UiColor::WindowBackground);

        const char* title = "ENGINE TESTBED";
        Ui_Text((screenW - Ui_TextWidth(3, title)) / 2, MENU_MARGIN, 3, title, UiColor::Header);

        const int top = MENU_MARGIN + MENU_TITLE_HEIGHT;
        const int height = screenH - top - MENU_FOOTER_HEIGHT - MENU_MARGIN;
        const int width = (screenW - MENU_MARGIN * 2 - MENU_COLUMN_GAP) / 2;
        const int half = static_cast<int>(TestbedCategory::Count) / 2;

        int chosen = DrawColumn(MENU_MARGIN, top, width, height, 0, half);
        const int right = DrawColumn(MENU_MARGIN + width + MENU_COLUMN_GAP, top, width, height, half, static_cast<int>(TestbedCategory::Count));
        if (right >= 0)
            chosen = right;

        char toggles[80];
        snprintf(toggles, sizeof(toggles), "%s TOGGLES", s_ChordText);
        const UiHint hints[] = {
            {Ui_ButtonIcon(GamepadButton::Cross), "SELECT"},
            {Ui_ButtonIcon(GamepadButton::Circle), "CLOSE"},
            {UiIcon::None, toggles},
        };
        Ui_HintBar(hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));

        if (chosen == EXIT_CHOSEN)
        {
            Engine_LogInfo("[Testbed] exit requested");
            EngineApp_OnExitRequested();
            return;
        }
        if (chosen >= 0)
        {
            EnterScene(chosen);
            return;
        }
        if (Ui_WasBackPressed())
            CloseToGame();
    }

    void RunScene(float dt)
    {
        int count = 0;
        const TestbedScene* scenes = Testbed_Catalogue(&count);
        if (s_ActiveScene < 0 || s_ActiveScene >= count)
        {
            OpenMenu();
            return;
        }

        if (scenes[s_ActiveScene].update)
            scenes[s_ActiveScene].update(dt);

        if (Ui_WasBackPressed())
            OpenMenu();
    }
} // namespace

void Engine_Testbed_RequestSubsystems()
{
    Engine_Subsystem_Enable(EngineSubsystem::Input);
    Engine_Subsystem_Enable(EngineSubsystem::Action);
    Engine_Subsystem_Enable(EngineSubsystem::Ui);
    Engine_Subsystem_Enable(EngineSubsystem::Testbed);
}

bool Engine_Testbed_Init()
{
    s_Mode = Mode::Closed;
    s_ActiveScene = -1;

    // A reserved action with no viable binding on this platform is a build
    // failure in tools/actions.py, not a runtime one -- so an incomplete
    // compiled table (older generated code than the engine expects) is the
    // only way this can actually be reached. Guarded anyway per the "every
    // entry point behaves correctly when not loaded" rule.
    if (Engine_Action_DeclaredCount() < ACTION_RESERVED_COUNT)
    {
        Engine_LogError("[Testbed] the compiled action table has no reserved debug-menu action; the testbed is unreachable.");
        s_Enabled = false;
        return false;
    }

    s_Enabled = true;
    Engine_LogInfo("[Testbed] ready. Hold %s to open.", s_ChordText);
    return true;
}

void Engine_Testbed_Shutdown()
{
    LeaveActiveScene();
    s_Mode = Mode::Closed;
    s_Enabled = false;
}

bool Engine_Testbed_IsOpen() { return s_Enabled && s_Mode != Mode::Closed; }

void Engine_Testbed_Update(float dt)
{
    if (!s_Enabled)
        return;

    // The only way Testbed checks for input: Engine_Action_WasPressed against
    // the reserved DebugMenu action, never Input or a platform chord directly.
    if (Engine_Action_WasPressed(Engine_Action_ReservedDebugMenu()))
    {
        if (s_Mode == Mode::Closed)
            OpenMenu();
        else
            CloseToGame();
        return;
    }

    if (s_Mode == Mode::Menu)
        DrawMenu();
    else if (s_Mode == Mode::Scene)
        RunScene(dt);
}
