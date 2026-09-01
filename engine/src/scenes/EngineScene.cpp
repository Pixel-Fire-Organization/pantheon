#include "scenes/EngineScene.h"

#include <cstring>

#include "core/EngineCore.h"
#include "core/EngineDebug.h"
#include "core/EngineIO.h"
#include "resources/EngineResource.h"
#include "scenes/EngineLoadingScreen.h"

namespace
{
    enum class State : uint8_t
    {
        Idle = 0,
        Loading,
        Running
    };

    game::Scene* s_Active = nullptr;
    State s_State = State::Idle;

    int32_t s_TrackedHandles[SCENE_MAX_RESOURCES];
    int s_TrackedCount = 0;
    bool s_Streaming = false;

    ResourceType Internal_ParseResourceType(const char* type)
    {
        if (strcmp(type, "TEXTURE") == 0)
            return RES_TEXTURE;
        if (strcmp(type, "MODEL") == 0)
            return RES_MODEL;
        if (strcmp(type, "SOUND") == 0)
            return RES_SOUND;
        if (strcmp(type, "FONT") == 0)
            return RES_FONT;

        Engine_LogError("[Scene] unknown resource type '%s' — treating as TEXTURE", type);
        return RES_TEXTURE;
    }

    void Internal_ReleaseTracked()
    {
        for (int i = 0; i < s_TrackedCount; ++i)
        {
            if (s_TrackedHandles[i] >= 0)
                Engine_Resource_Unload(s_TrackedHandles[i]);
        }
        s_TrackedCount = 0;
    }

    /// Attempt every resource `scene` declares. On success they are tracked
    /// for progress polling; on any failure whatever was acquired is released
    /// and the scene streams for itself from then on.
    void Internal_BeginResourceAttempt(game::Scene* scene)
    {
        s_TrackedCount = 0;
        s_Streaming = false;

        int count = 0;
        const game::SceneResource* resources = scene->GetResources(&count);
        if (!resources || count <= 0)
            return;

        if (count > SCENE_MAX_RESOURCES)
        {
            Engine_LogError("[Scene] declares %d resources, more than SCENE_MAX_RESOURCES (%d) — truncating", count, SCENE_MAX_RESOURCES);
            count = SCENE_MAX_RESOURCES;
        }

        bool allLoaded = true;
        for (int i = 0; i < count; ++i)
        {
            char resolved[IO_FILE_MAX_PATH];
            Engine_BuildPath(nullptr, resources[i].path, resolved, sizeof(resolved));

            const int32_t handle = Engine_Resource_Load(Internal_ParseResourceType(resources[i].type), resolved);
            s_TrackedHandles[s_TrackedCount++] = handle;
            if (handle < 0)
                allLoaded = false;
        }

        if (!allLoaded)
        {
            Engine_LogInfo("[Scene] declared resources did not all fit into the resource table — streaming instead");
            Internal_ReleaseTracked();
            s_Streaming = true;
        }
    }

    float Internal_LoadProgress()
    {
        if (s_Streaming || s_TrackedCount == 0)
            return 1.0f;

        int ready = 0;
        for (int i = 0; i < s_TrackedCount; ++i)
        {
            if (Engine_Resource_IsReady(s_TrackedHandles[i]))
                ++ready;
        }
        return static_cast<float>(ready) / static_cast<float>(s_TrackedCount);
    }

    void Internal_BeginSwitch(game::Scene* next)
    {
        if (s_Active)
            s_Active->OnStop();

        Engine_ResetRuntimeState();
        Engine_LoadingScreen_Begin();

        s_Active = next;
        Internal_BeginResourceAttempt(next);
        s_State = State::Loading;

        if (Internal_LoadProgress() >= 1.0f)
        {
            s_Active->OnStart();
            s_State = State::Running;
        }
    }
} // namespace

bool Engine_Scene_Init()
{
    s_Active = nullptr;
    s_State = State::Idle;
    s_TrackedCount = 0;
    s_Streaming = false;
    return true;
}

void Engine_Scene_Shutdown()
{
    if (s_Active)
        s_Active->OnStop();
    s_Active = nullptr;
    s_State = State::Idle;
    s_TrackedCount = 0;
}

bool Engine_Scene_HasActive() { return s_Active != nullptr; }

void Engine_Scene_SetMain(game::Scene* scene)
{
    if (!scene)
        return;
    Internal_BeginSwitch(scene);
}

void Engine_Scene_Switch(game::Scene* scene)
{
    if (!scene || scene == s_Active)
        return;
    Internal_BeginSwitch(scene);
}

void Engine_Scene_Reload()
{
    if (!s_Active)
    {
        Engine_LogInfo("[Scene] ReloadScene: no active scene");
        return;
    }
    Internal_BeginSwitch(s_Active);
}

bool Engine_Scene_IsLoading() { return s_State == State::Loading; }

float Engine_Scene_GetLoadProgress() { return Internal_LoadProgress(); }

void Engine_Scene_Update(float dt)
{
    if (!s_Active)
        return;

    if (s_State == State::Loading)
    {
        const float progress = Internal_LoadProgress();
        Engine_LoadingScreen_Draw(progress, dt);
        s_Active->OnDrawLoadingUI(progress);

        if (progress >= 1.0f)
        {
            s_Active->OnStart();
            s_State = State::Running;
        }
        return;
    }

    s_Active->OnUpdate(dt);
}
