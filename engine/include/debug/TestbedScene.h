#pragma once

#include <cstdint>

#include "../include/ui/EngineUi.h"

/// Which group of the catalogue a scene belongs to. The menu draws one heading
/// per category with that category's scenes beneath it.
enum class TestbedCategory : uint8_t
{
    InputDevices = 0,
    RenderingDisplay,
    SystemsBudgets,
    TimingPacing,

    Count
};

/// One entry of the catalogue. `init` runs on entry, `update` every frame while
/// active, `shutdown` on the way out; the last two may be null.
struct TestbedScene
{
    const char* name;
    TestbedCategory category;
    void (*init)();
    void (*update)(float dt);
    void (*shutdown)();
};

/// @param outCount Receives how many entries the catalogue holds.
/// @return The catalogue, in declaration order.
const TestbedScene* Testbed_Catalogue(int* outCount);

/// @param category The group to name.
/// @return Its heading text.
const char* Testbed_CategoryName(TestbedCategory category);

/// Draw a strip of pages inside the open panel and report which is showing.
/// Replaces the shoulder-button paging a scene used to do for itself, so the
/// page is a property of the scene rather than of the testbed.
/// @param id Identity of the strip, which is where the active page is filed.
/// @param names One name per page.
/// @param count How many pages.
/// @return The page to draw, counting from zero.
int Testbed_Tabs(const char* id, const char* const* names, int count);

/// Draw the panel a scene shows when the thing it tests is not present here.
/// @param what The capability or subsystem that is unavailable.
void Testbed_DrawUnavailable(const char* what);

// --- Scene entry points ----------------------------------------------------

void Scene_PlatformInfo_Init();
void Scene_PlatformInfo_Update(float dt);

void Scene_Gamepad_Init();
void Scene_Gamepad_Update(float dt);

void Scene_KeyboardMouse_Init();
void Scene_KeyboardMouse_Update(float dt);

void Scene_Touch_Init();
void Scene_Touch_Update(float dt);

void Scene_Pointer_Init();
void Scene_Pointer_Update(float dt);

void Scene_Chords_Init();
void Scene_Chords_Update(float dt);

void Scene_ScreenAspect_Init();
void Scene_ScreenAspect_Update(float dt);

void Scene_DepthRange_Init();
void Scene_DepthRange_Update(float dt);

void Scene_Memory_Init();
void Scene_Memory_Update(float dt);

void Scene_Resources_Init();
void Scene_Resources_Update(float dt);

void Scene_AssetBrowser_Init();
void Scene_AssetBrowser_Update(float dt);
void Scene_AssetBrowser_Shutdown();

void Scene_LevelStream_Init();
void Scene_LevelStream_Update(float dt);
void Scene_LevelStream_Shutdown();

void Scene_Achievements_Init();
void Scene_Achievements_Update(float dt);

void Scene_Performance_Init();
void Scene_Performance_Update(float dt);

void Scene_FramePacing_Init();
void Scene_FramePacing_Update(float dt);
