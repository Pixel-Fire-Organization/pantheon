#include "../../engine/include/core/EngineCore.h"
#include "../../engine/include/core/EngineSubsystems.h"
#include "../include/scenes/MainMenu.h"
#include "../include/scenes/MainScene.h"
#include "GameAPI.h"

void GameConfigure(EngineConfig* config)
{
    static constexpr EngineSubsystem kSubsystems[] = {
        EngineSubsystem::Io,    EngineSubsystem::Archive, EngineSubsystem::Resource,    EngineSubsystem::Level,      EngineSubsystem::Sector,
        EngineSubsystem::Input, EngineSubsystem::Ui,      EngineSubsystem::Achievement, EngineSubsystem::PerfLogger, EngineSubsystem::Scene,
    };

    config->subsystems = kSubsystems;
    config->subsystemCount = sizeof(kSubsystems) / sizeof(kSubsystems[0]);
}

void GameInit() { game::SetMainScene((game::Scene*)MainScene::GetInstance()); }

// The Scene subsystem drives MainScene directly once GameInit registers it;
// this is kept only because GameAPI.h still requires the symbol.
void GameUpdate(float dt) { (void)dt; }
