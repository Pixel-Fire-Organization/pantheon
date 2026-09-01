// ---------------------------------------------------------------------------
// EcsHooks.cpp — hand-written game-side ECS glue.
//
// The generated EcsSpawn.cpp (build dir) *declares* one Game_Spawn_<classname>
// per Trenchbroom entity and the EcsHooks:: functions named by ECS.json's
// "hooks" array, then leaves them for the game to define. A missing definition
// is a link error on purpose: adding an entity or a hook to ECS.json forces the
// game to decide what it does.
//
// For now these are lightweight stubs that log the spawn; the runtime entity
// system (issue #16, later phase) will turn the typed defs into live entities.
// ---------------------------------------------------------------------------

#include <cstdio>

#include "EcsComponents.h" // generated (build dir): typed component/aggregate structs
#include "GameAPI.h"
#include "Macros.h"

namespace EcsHooks
{
    void HurtEntity(void* component)
    {
        (void)component;
        game::Log("[NotImpl] EcsHooks::HurtEntity invoked");
    }

    void UnlockDoor(void* component)
    {
        (void)component;
        game::Log("[NotImpl] EcsHooks::UnlockDoor invoked");
    }

    void LockDoor(void* component)
    {
        (void)component;
        game::Log("[NotImpl] EcsHooks::LockDoor invoked");
    }

    void ButtonPressed(void* component)
    {
        (void)component;
        game::Log("[NotImpl] EcsHooks::ButtonPressed invoked");
    }
} // namespace EcsHooks

void Game_Spawn_prop_barrel(const Ecs_prop_barrel& def, const game::EntitySpawn& spawn)
{
    char msg[128];
    std::snprintf(msg, sizeof(msg), "[NotImpl] spawn prop_barrel hp=%d at (%.1f, %.1f, %.1f)", def.healthComponent.max_health, spawn.x, spawn.y, spawn.z);
    game::Log(msg);
}

void Game_Spawn_prop_model(const Ecs_prop_model& def, const game::EntitySpawn& spawn)
{
    char msg[192];
    std::snprintf(msg, sizeof(msg), "[NotImpl] spawn prop_model model='%s' at (%.1f, %.1f, %.1f)", def.modelComponent.model, spawn.x, spawn.y, spawn.z);
    game::Log(msg);
}

void Game_Spawn_func_door(const Ecs_func_door& def, const game::EntitySpawn& spawn)
{
    UNUSED_VAR(def);
    char msg[192];
    std::snprintf(msg, sizeof(msg), "[NotImpl] spawn func_door at (%.1f, %.1f, %.1f)", spawn.x, spawn.y, spawn.z);
    game::Log(msg);
}

void Game_Spawn_func_changeLevel(const Ecs_func_changeLevel& def, const game::EntitySpawn& spawn)
{
    char msg[192];
    std::snprintf(msg, sizeof(msg), "[NotImpl] spawn func_changeLevel levelName='%s' at (%.1f, %.1f, %.1f)", def.levelComponent.level_name, spawn.x, spawn.y, spawn.z);
    game::Log(msg);
}

void Game_Spawn_func_button(const Ecs_func_button& def, const game::EntitySpawn& spawn)
{
    UNUSED_VAR(def);
    char msg[192];
    std::snprintf(msg, sizeof(msg), "[NotImpl] spawn func_button at (%.1f, %.1f, %.1f)", spawn.x, spawn.y, spawn.z);
    game::Log(msg);
}
