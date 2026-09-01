#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/EngineCore.h"
#include "core/EngineSubsystems.h"
#include "level/EngineLevel.h"
#include "level/EngineSector.h"
#include "ui/EngineUi.h"
#include "debug/TestbedScene.h"
#include "graphics/Renderer.h"
#include "platform/Platform.h"

namespace
{
    const int PANEL_MARGIN = 16;
    const int COLUMN_GAP = 8;
    const int BOX_HEIGHT = 130;
    const char* const LEVEL_NAME = "TEST";
    const float WALK_RADIUS = 24.0f;
    const float WALK_RATE = 0.35f;
    const float MAP_EXTENT = 64.0f;

    Level s_Level;
    bool s_Loaded = false;
    bool s_Walking = false;
    float s_Time = 0.0f;
    float s_CentreX = 0.0f;
    float s_CentreZ = 0.0f;

    void Unload()
    {
        if (!s_Loaded)
            return;
        Engine_Level_Unload(&s_Level, false);
        s_Loaded = false;
    }

    void Load()
    {
        Unload();
        memset(&s_Level, 0, sizeof(s_Level));
        strncpy(s_Level.name, LEVEL_NAME, sizeof(s_Level.name) - 1);
        s_Loaded = Engine_Level_Load(&s_Level);
    }

    /// Map a world coordinate into the residency box.
    /// @param value The world X or Z.
    /// @return Its position across the box, in [0,1].
    float Normalised(float value)
    {
        float n = (value + MAP_EXTENT) / (MAP_EXTENT * 2.0f);
        if (n < 0.0f)
            n = 0.0f;
        if (n > 1.0f)
            n = 1.0f;
        return n;
    }
} // namespace

void Scene_LevelStream_Init()
{
    s_Loaded = false;
    s_Walking = false;
    s_Time = 0.0f;
    s_CentreX = 0.0f;
    s_CentreZ = 0.0f;
    memset(&s_Level, 0, sizeof(s_Level));
}

void Scene_LevelStream_Shutdown() { Unload(); }

void Scene_LevelStream_Update(float dt)
{
    const Platform* platform = Engine_GetPlatform();
    if (!platform)
        return;

    if (s_Walking)
    {
        s_Time += dt;
        s_CentreX = WALK_RADIUS * sinf(s_Time * WALK_RATE);
        s_CentreZ = WALK_RADIUS * cosf(s_Time * WALK_RATE);
    }
    if (s_Loaded)
        Engine_Level_SetStreamingCenter(s_CentreX, s_CentreZ);

    Renderer* renderer = Engine_GetRenderer();
    if (renderer)
    {
        renderer->ClearFrame(Color3{0.06f, 0.07f, 0.10f});
        if (s_Loaded)
        {
            Camera3D camera;
            camera.position = Vector3{s_CentreX, 14.0f, s_CentreZ + 18.0f};
            camera.target = Vector3{s_CentreX, 0.0f, s_CentreZ};
            camera.up = Vector3{0.0f, 1.0f, 0.0f};
            camera.fovy = 55.0f;
            camera.projection = CAMERA_PERSPECTIVE;
            renderer->SetCamera3D(static_cast<CameraID>(0), camera);
            renderer->SetActiveCamera3D(static_cast<CameraID>(0));
            renderer->AddLevelToDrawList(s_Level);
        }
    }

    const int screenW = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenWidth));
    const int screenH = static_cast<int>(platform->GetConstant(PlatformConstant::ScreenHeight));
    const int width = (screenW - PANEL_MARGIN * 2 - COLUMN_GAP) / 2;
    const int height = screenH - PANEL_MARGIN * 2;

    Ui_BeginPanel("LEVEL", PANEL_MARGIN, PANEL_MARGIN, width, height);
    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Level))
    {
        Testbed_DrawUnavailable("LEVEL SUBSYSTEM");
        Ui_EndPanel();
        return;
    }

    Ui_LabelValue("NAME", LEVEL_NAME);
    Ui_LabelValue("STATE", s_Loaded ? "LOADED" : "UNLOADED");

    if (Ui_Selectable(s_Loaded ? "UNLOAD" : "LOAD", s_Loaded))
    {
        if (s_Loaded)
            Unload();
        else
            Load();
    }
    Ui_Checkbox("WALK THE CENTRE", &s_Walking);

    char text[48];
    snprintf(text, sizeof(text), "%.1f %.1f", static_cast<double>(s_CentreX), static_cast<double>(s_CentreZ));
    Ui_LabelValue("CENTRE", text);
    Ui_Separator();
    Ui_Label("LOAD THEN UNLOAD, TWICE.");
    Ui_Label("A LEAK SHOWS IN MEMORY.");
    Ui_EndPanel();

    Ui_BeginPanel("SECTORS", PANEL_MARGIN + width + COLUMN_GAP, PANEL_MARGIN, width, height);
    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Sector))
    {
        Testbed_DrawUnavailable("SECTOR SUBSYSTEM");
        Ui_EndPanel();
        return;
    }

    uint32_t residentCount = 0;
    const SectorResident* residents = Engine_Sector_GetResidents(&residentCount);

    int ready = 0;
    int loading = 0;
    for (uint32_t i = 0; i < residentCount; ++i)
    {
        if (residents[i].state == SECTOR_READY)
            ++ready;
        else if (residents[i].state == SECTOR_LOADING)
            ++loading;
    }

    snprintf(text, sizeof(text), "%d", ready);
    Ui_LabelValue("READY", text);
    snprintf(text, sizeof(text), "%d", loading);
    Ui_LabelValue("LOADING", text);

    Ui_BeginPointBox("RESIDENCY", BOX_HEIGHT);
    for (uint32_t i = 0; i < residentCount; ++i)
    {
        if (residents[i].state == SECTOR_EMPTY)
            continue;
        const float x = (residents[i].bounds.min.x + residents[i].bounds.max.x) * 0.5f;
        const float z = (residents[i].bounds.min.z + residents[i].bounds.max.z) * 0.5f;
        Ui_Point(Normalised(x), Normalised(z), (residents[i].state == SECTOR_READY) ? UiColor::BarFill : UiColor::TextWarn);
    }
    Ui_Point(Normalised(s_CentreX), Normalised(s_CentreZ), UiColor::TextAccent);
    Ui_EndPointBox();
    Ui_Label("YELLOW IS THE CENTRE");
    Ui_EndPanel();
}
