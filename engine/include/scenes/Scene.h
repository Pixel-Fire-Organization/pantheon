#pragma once

#include "SceneResource.h"

namespace game
{
    // The base contract every scene implements. A scene is a long-lived C++
    // object (typically a static instance in game/src), never heap-allocated
    // or constructed per switch.
    class Scene
    {
    public:
        virtual ~Scene() = default;

        // Called once the scene's declared resources are attached (either all
        // of them, eagerly, or none — see GetResources).
        virtual void OnStart() = 0;

        // Called every frame this scene is the active one and has finished
        // loading.
        virtual void OnUpdate(float dt) = 0;

        // Called once when this scene is switched away from, or reloaded.
        // Release whatever OnStart acquired; declared resources are released
        // by the engine.
        virtual void OnStop() {}

        // Resources this scene wants attached. Returns the array and writes
        // its count (0 and null are both a legal "none"). The array must
        // outlive the call — a static table is the usual shape.
        virtual const SceneResource* GetResources(int* outCount) const
        {
            *outCount = 0;
            return nullptr;
        }

        // Called every frame while this scene's declared resources are still
        // outstanding, right after the built-in loading screen draws — so
        // content drawn here lands on top of it by submission order alone.
        // @param progress Fraction in [0,1] of declared resources now ready.
        virtual void OnDrawLoadingUI(float progress) { (void)progress; }
    };
} // namespace game
