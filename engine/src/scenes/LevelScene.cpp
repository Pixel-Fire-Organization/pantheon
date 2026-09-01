#include "scenes/LevelScene.h"

#include <cstring>

#include "GameAPI.h"
#include "core/EngineCore.h"
#include "core/EngineDebug.h"
#include "core/EngineSubsystems.h"
#include "graphics/Renderer.h"
#include "level/EngineLevel.h"

namespace
{
    Level s_LevelSceneLevel;
    bool s_LevelSceneLoaded = false;
} // namespace

// ---------------------------------------------------------------------------
// LevelScene's fixed pipeline. Its body reaches into engine internals
// (EngineLevel.h, the renderer) that game/** may not include directly; a
// game-authored subclass only ever overrides the protected hooks declared in
// LevelScene.h, whose own bodies stay within the game:: surface.
// ---------------------------------------------------------------------------

void game::LevelScene::OnStart()
{
    s_LevelSceneLoaded = false;

    if (!Engine_Subsystem_IsEnabled(EngineSubsystem::Level))
    {
        Engine_LogError("[Scene] LevelScene '%s' started with the Level subsystem not enabled", GetLevelName());
        return;
    }

    game::SetSpawnHandler(GetSpawnHandler());

    memset(&s_LevelSceneLevel, 0, sizeof(s_LevelSceneLevel));
    strncpy(s_LevelSceneLevel.name, GetLevelName(), sizeof(s_LevelSceneLevel.name) - 1);
    s_LevelSceneLoaded = Engine_Level_Load(&s_LevelSceneLevel);
    if (!s_LevelSceneLoaded)
        Engine_LogError("[Scene] LevelScene failed to load level '%s'", GetLevelName());

    OnLevelStart();
}

void game::LevelScene::OnUpdate(float dt)
{
    if (s_LevelSceneLoaded)
    {
        float x = 0.0f;
        float z = 0.0f;
        GetStreamingCenter(&x, &z);
        Engine_Level_SetStreamingCenter(x, z);

        Renderer* renderer = Engine_GetRenderer();
        if (renderer)
            renderer->AddLevelToDrawList(s_LevelSceneLevel);
    }

    OnLevelUpdate(dt);
}

void game::LevelScene::OnStop()
{
    if (s_LevelSceneLoaded)
    {
        Engine_Level_Unload(&s_LevelSceneLevel, false);
        s_LevelSceneLoaded = false;
    }
}
