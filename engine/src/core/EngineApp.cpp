#include "core/EngineApp.h"
#include "Engine.h"
#include "GameAPI.h"
#include "core/EngineAchievement.h"
#include "core/EngineAction.h"
#include "core/EngineNotice.h"
#include "debug/EngineTestbed.h"
#include "platform/Platform.h"
#include "scenes/EngineScene.h"
#include "ui/EngineUi.h"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Internal exit flag — set by EngineApp_OnExitRequested (called by game::Exit()).
// ---------------------------------------------------------------------------
static bool s_ExitRequested = false;
static bool s_StartupSettled = false;

void EngineApp_OnExitRequested() { s_ExitRequested = true; }

// ---------------------------------------------------------------------------
// Engine shell. Device-token resolution used to live here; it is platform
// grammar, so it moved to engine/src/platform/<name>/Filesystem.cpp.
// ---------------------------------------------------------------------------

bool EngineStart(const EngineConfig& config, Platform* platform, Renderer* renderer)
{
    if (!Engine_Init(config, platform, renderer))
    {
        Engine_Panic("Engine_Init failed — hardware or memory error");
    }

    s_ExitRequested = false;
    s_StartupSettled = false;

    // Gameplay (and UI) is authored in C++: hand control to the game module.
    Engine_LogInfo("EngineStart: token='%s' — starting C++ game module", config.resourceLocationToken ? config.resourceLocationToken : "<none>");
    GameInit();
    return true;
}

void EngineUpdate()
{
    static double frameStartTime = 0;
    static double gameLogicEndTime = 0;
    static double renderEndTime = 0;

    Platform* platform = Engine_GetPlatform();
    frameStartTime = platform->GetTimeSeconds();

    Engine_Update();
    float dt = Engine_GetDeltaTime();

    // Resolved once per frame, right after the input poll and before anything
    // -- UI, the testbed, scene or game code -- can query an action.
    if (Engine_Subsystem_IsEnabled(EngineSubsystem::Action))
        Engine_Action_Update(dt);

    // A platform may finish binding to its achievement set only once frames are
    // running, so nothing may report on it until it has stopped settling.
    if (!s_StartupSettled && !Engine_Achievement_PumpStartup())
    {
        s_StartupSettled = true;
        Engine_Notice_Evaluate();
    }

    // 1. Gameplay Phase (C++) — the game module's per-frame update + draw submission.
    Ui_BeginFrame();
    if (!Engine_Notice_Update(dt))
    {
        Engine_Testbed_Update(dt);
        if (!Engine_Testbed_IsOpen())
        {
            if (Engine_Scene_HasActive())
                Engine_Scene_Update(dt);
            else
                GameUpdate(dt);
        }
        Engine_Achievement_Update(dt);
    }
    Engine_DrawDebugOverlay();
    Ui_EndFrame();
    gameLogicEndTime = platform->GetTimeSeconds();

    // 2. Renderer Phase (CPU-side transforms)
    Renderer* r = Engine_GetRenderer();
    if (r && r->IsInitialized())
    {
        r->BeginFrame();
        r->Render();
        r->EndFrame();
    }

    renderEndTime = platform->GetTimeSeconds();

    const double frameEndTime = platform->GetTimeSeconds();

    // Detailed Stats Reporting
    // Game Logic = Start to GameLogicEnd
    // C++ Render = GameLogicEnd to RenderEnd (Mega-batching)
    // GS Wait = RenderEnd to Vblank/Finish
    Engine_ReportFrameStats((float)(gameLogicEndTime - frameStartTime), // Logic
                            (float)(renderEndTime - gameLogicEndTime), // Render
                            (float)(frameEndTime - renderEndTime) // Wait
    );

    Engine_PerfLogger_Tick();
}

bool EngineExited() { return s_ExitRequested; }

void EngineStop() { Engine_Close(); }
