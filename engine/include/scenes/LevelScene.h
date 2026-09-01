#pragma once

#include "../ecs/Entity.h"
#include "Scene.h"

namespace game
{
    // A scene whose level and its resident geometry are always kept current;
    // the game supplies only what the level is called and its own per-frame
    // gameplay, never the mount/stream/draw calls themselves.
    class LevelScene : public Scene
    {
    public:
        void OnStart() final;
        void OnUpdate(float dt) final;
        void OnStop() final;

    protected:
        // The compiled level to mount (see LoadLevel).
        virtual const char* GetLevelName() const = 0;

        // The handler entities spawn through while this scene's level is
        // mounted (see SetSpawnHandler). Re-registered on every OnStart,
        // since the runtime reset every switch performs clears it. None by
        // default.
        virtual SpawnHandler GetSpawnHandler() const { return nullptr; }

        // Called once the level has (successfully or not) finished mounting.
        // Reset gameplay state here — OnStart itself is fixed, this is where
        // a level scene's own "GameInit()" logic belongs.
        virtual void OnLevelStart() {}

        // Per-frame gameplay: camera, input, drawing anything beyond the
        // level's own geometry. Called after the streaming centre and the
        // level's geometry have both been submitted for this frame.
        virtual void OnLevelUpdate(float dt) { (void)dt; }

        // World position the resident sector ring should follow (see
        // SetStreamingCenter). Defaults to the origin.
        virtual void GetStreamingCenter(float* outX, float* outZ) const
        {
            *outX = 0.0f;
            *outZ = 0.0f;
        }
    };
} // namespace game
