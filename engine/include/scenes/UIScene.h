#pragma once

#include "Scene.h"

namespace game
{
    // A scene whose only content is drawn through the interface. Its declared
    // resources are guaranteed attached before OnDrawUi ever runs.
    class UiScene : public Scene
    {
    public:
        void OnStart() final { OnUiStart(); }
        void OnUpdate(float dt) final;
        void OnStop() final {}

    protected:
        // Called once this scene's declared resources are attached. Reset
        // gameplay state here, exactly as LevelScene::OnLevelStart.
        virtual void OnUiStart() {}

        virtual void OnDrawUi(float dt) = 0;
    };
} // namespace game
