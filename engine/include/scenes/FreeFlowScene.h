#pragma once
#include "Scene.h"

namespace game
{
    // No fixed pipeline: load, unload and render entirely at will, exactly as
    // GameUpdate always has. Provided so every shape this API names is
    // discoverable in one place; deriving from Scene directly is equivalent.
    class FreeFlowScene : public Scene
    {
    };
} // namespace game
