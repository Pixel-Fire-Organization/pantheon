// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY
// Generated from tools/ECS/ECS.json by tools/ECS/generate_ecs.py
#include "EcsComponents.h"
#include "GameAPI.h"

#include <cstdlib>
#include <cstring>

// Game-implemented spawn handlers, one per classname. A missing definition is a
// link error on purpose: adding an entity to ECS.json forces the game to handle it.
void Game_Spawn_prop_barrel(const Ecs_prop_barrel& def, const game::EntitySpawn& spawn);
void Game_Spawn_prop_model(const Ecs_prop_model& def, const game::EntitySpawn& spawn);
void Game_Spawn_func_button(const Ecs_func_button& def, const game::EntitySpawn& spawn);
void Game_Spawn_func_changeLevel(const Ecs_func_changeLevel& def, const game::EntitySpawn& spawn);
void Game_Spawn_func_door(const Ecs_func_door& def, const game::EntitySpawn& spawn);

namespace
{
    const char* Internal_FindProp(const game::EntitySpawn& spawn, const char* key)
    {
        for (int i = 0; i < spawn.propCount; ++i)
        {
            if (std::strcmp(spawn.props[i].key, key) == 0)
            {
                return spawn.props[i].value;
            }
        }
        return nullptr;
    }
}  // namespace

// Turn a generic engine spawn record into the game's typed components, applying
// declared defaults for any property the map did not set. Returns false for an
// unknown classname. String properties point into the spawn record, which is only
// valid for the duration of the handler call -- copy anything you need to keep.
bool Ecs_SpawnDispatch(const game::EntitySpawn& spawn)
{
    if (std::strcmp(spawn.classname, "prop_barrel") == 0)
    {
        Ecs_prop_barrel def;
        {
            const char* raw = Internal_FindProp(spawn, "max_health");
            if (raw) { long v = std::strtol(raw, nullptr, 10); def.healthComponent.max_health = (int)v; }
        }
        {
            const char* raw = Internal_FindProp(spawn, "layer_mask");
            if (raw) { unsigned long v = std::strtoul(raw, nullptr, 10); def.collisionComponent.layer_mask = (uint32_t)v; }
        }
        {
            const char* raw = Internal_FindProp(spawn, "model");
            if (raw) { const char* v = raw; def.modelComponent.model = v; }
        }
        {
            const char* raw = Internal_FindProp(spawn, "is_static");
            if (raw) { bool v = (std::strcmp(raw, "1") == 0 || std::strcmp(raw, "true") == 0); def.modelComponent.is_static = v; }
        }
        Game_Spawn_prop_barrel(def, spawn);
        return true;
    }
    if (std::strcmp(spawn.classname, "prop_model") == 0)
    {
        Ecs_prop_model def;
        {
            const char* raw = Internal_FindProp(spawn, "position");
            if (raw) { const char* v = raw; def.transformComponent.position = v; }
        }
        {
            const char* raw = Internal_FindProp(spawn, "angles");
            if (raw) { const char* v = raw; def.transformComponent.angles = v; }
        }
        {
            const char* raw = Internal_FindProp(spawn, "scale");
            if (raw) { const char* v = raw; def.transformComponent.scale = v; }
        }
        {
            const char* raw = Internal_FindProp(spawn, "layer_mask");
            if (raw) { unsigned long v = std::strtoul(raw, nullptr, 10); def.collisionComponent.layer_mask = (uint32_t)v; }
        }
        {
            const char* raw = Internal_FindProp(spawn, "model");
            if (raw) { const char* v = raw; def.modelComponent.model = v; }
        }
        {
            const char* raw = Internal_FindProp(spawn, "is_static");
            if (raw) { bool v = (std::strcmp(raw, "1") == 0 || std::strcmp(raw, "true") == 0); def.modelComponent.is_static = v; }
        }
        Game_Spawn_prop_model(def, spawn);
        return true;
    }
    if (std::strcmp(spawn.classname, "func_button") == 0)
    {
        Ecs_func_button def;
        Game_Spawn_func_button(def, spawn);
        return true;
    }
    if (std::strcmp(spawn.classname, "func_changeLevel") == 0)
    {
        Ecs_func_changeLevel def;
        {
            const char* raw = Internal_FindProp(spawn, "level_name");
            if (raw) { const char* v = raw; def.levelComponent.level_name = v; }
        }
        {
            const char* raw = Internal_FindProp(spawn, "wait_time");
            if (raw) { long v = std::strtol(raw, nullptr, 10); def.levelComponent.wait_time = (int)v; }
        }
        {
            const char* raw = Internal_FindProp(spawn, "args");
            if (raw) { const char* v = raw; def.levelComponent.args = v; }
        }
        Game_Spawn_func_changeLevel(def, spawn);
        return true;
    }
    if (std::strcmp(spawn.classname, "func_door") == 0)
    {
        Ecs_func_door def;
        {
            const char* raw = Internal_FindProp(spawn, "layer_mask");
            if (raw) { unsigned long v = std::strtoul(raw, nullptr, 10); def.collisionComponent.layer_mask = (uint32_t)v; }
        }
        Game_Spawn_func_door(def, spawn);
        return true;
    }
    return false;
}
