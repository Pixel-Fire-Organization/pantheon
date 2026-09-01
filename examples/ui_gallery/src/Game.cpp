#include "GameAPI.h"
#include "core/EngineCore.h"
#include "core/EngineSubsystems.h"
#include "scenes/GalleryScene.h"

void GameConfigure(EngineConfig* config)
{
    static constexpr EngineSubsystem kSubsystems[] = {
        EngineSubsystem::Io, EngineSubsystem::Archive, EngineSubsystem::Resource, EngineSubsystem::Input, EngineSubsystem::Ui, EngineSubsystem::Scene,
    };

    config->subsystems = kSubsystems;
    config->subsystemCount = sizeof(kSubsystems) / sizeof(kSubsystems[0]);
}

void GameInit() { game::SetMainScene(GalleryScene::GetInstance()); }

void GameUpdate(float dt) { (void)dt; }
