#include "debug/TestbedScene.h"

#include <cstring>

#include "ui/EngineUi.h"
#include "platform/Platform.h"

namespace
{
    const TestbedScene s_Scenes[] = {
        {"GAMEPAD", TestbedCategory::InputDevices, Scene_Gamepad_Init, Scene_Gamepad_Update, nullptr},
        {"KEYBOARD MOUSE", TestbedCategory::InputDevices, Scene_KeyboardMouse_Init, Scene_KeyboardMouse_Update, nullptr},
        {"TOUCH", TestbedCategory::InputDevices, Scene_Touch_Init, Scene_Touch_Update, nullptr},
        {"POINTER", TestbedCategory::InputDevices, Scene_Pointer_Init, Scene_Pointer_Update, nullptr},
        {"DEBUG CHORDS", TestbedCategory::InputDevices, Scene_Chords_Init, Scene_Chords_Update, nullptr},
        {"SCREEN AND ASPECT", TestbedCategory::RenderingDisplay, Scene_ScreenAspect_Init, Scene_ScreenAspect_Update, nullptr},
        {"DEPTH RANGE", TestbedCategory::RenderingDisplay, Scene_DepthRange_Init, Scene_DepthRange_Update, nullptr},
        {"PLATFORM INFO", TestbedCategory::SystemsBudgets, Scene_PlatformInfo_Init, Scene_PlatformInfo_Update, nullptr},
        {"MEMORY", TestbedCategory::SystemsBudgets, Scene_Memory_Init, Scene_Memory_Update, nullptr},
        {"RESOURCES", TestbedCategory::SystemsBudgets, Scene_Resources_Init, Scene_Resources_Update, nullptr},
        {"ASSET BROWSER", TestbedCategory::SystemsBudgets, Scene_AssetBrowser_Init, Scene_AssetBrowser_Update, Scene_AssetBrowser_Shutdown},
        {"LEVEL STREAM", TestbedCategory::SystemsBudgets, Scene_LevelStream_Init, Scene_LevelStream_Update, Scene_LevelStream_Shutdown},
        {"ACHIEVEMENTS", TestbedCategory::SystemsBudgets, Scene_Achievements_Init, Scene_Achievements_Update, nullptr},
        {"PERFORMANCE", TestbedCategory::TimingPacing, Scene_Performance_Init, Scene_Performance_Update, nullptr},
        {"FRAME PACING", TestbedCategory::TimingPacing, Scene_FramePacing_Init, Scene_FramePacing_Update, nullptr},
    };

    const int s_SceneCount = static_cast<int>(sizeof(s_Scenes) / sizeof(s_Scenes[0]));
} // namespace

const TestbedScene* Testbed_Catalogue(int* outCount)
{
    if (outCount)
        *outCount = s_SceneCount;
    return s_Scenes;
}

const char* Testbed_CategoryName(TestbedCategory category)
{
    switch (category)
    {
    case TestbedCategory::InputDevices:
        return "INPUT AND DEVICES";
    case TestbedCategory::RenderingDisplay:
        return "RENDERING AND DISPLAY";
    case TestbedCategory::SystemsBudgets:
        return "SYSTEMS AND BUDGETS";
    case TestbedCategory::TimingPacing:
        return "TIMING AND PACING";
    case TestbedCategory::Count:
        break;
    }
    return "?";
}

int Testbed_Tabs(const char* id, const char* const* names, int count)
{
    if (count <= 1 || !Ui_BeginTabBar(id))
        return 0;

    int active = 0;
    for (int i = 0; i < count; ++i)
    {
        if (Ui_Tab(names[i]))
            active = i;
    }
    Ui_EndTabBar();
    return active;
}

void Testbed_DrawUnavailable(const char* what)
{
    Ui_LabelColored("NOT AVAILABLE ON THIS PLATFORM", UiColor::TextWarn);
    Ui_Label(what);
}
