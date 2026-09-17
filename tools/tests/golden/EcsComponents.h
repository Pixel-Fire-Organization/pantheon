// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY
// Generated from tools/ECS/ECS.json by tools/ECS/generate_ecs.py
#pragma once

#include <cstdint>

// Engine hooks the game must implement (see game/src/EcsHooks.cpp).
namespace EcsHooks
{
    void HurtEntity(void* component);
    void UnlockDoor(void* component);
    void LockDoor(void* component);
    void ButtonPressed(void* component);
}  // namespace EcsHooks

enum class ComponentTypeId : uint16_t
{
    None = 0,
    HealthComponent = 1,
    CollisionComponent = 2,
    TransformComponent = 3,
    ModelComponent = 4,
    ButtonComponent = 5,
    LevelComponent = 6,
    DoorComponent = 7,
};

struct HealthComponent
{
    static const ComponentTypeId kTypeId = ComponentTypeId::HealthComponent;
    int max_health = 100;
    void hurtPlayer() { EcsHooks::HurtEntity(this); }
};

struct CollisionComponent
{
    static const ComponentTypeId kTypeId = ComponentTypeId::CollisionComponent;
    uint32_t layer_mask = 1;
};

struct TransformComponent
{
    static const ComponentTypeId kTypeId = ComponentTypeId::TransformComponent;
    const char* position = "0.0 0.0 0.0";
    const char* angles = "0 0 0";
    const char* scale = "1.0 1.0 1.0";
};

struct ModelComponent
{
    static const ComponentTypeId kTypeId = ComponentTypeId::ModelComponent;
    const char* model = "models/barrel.mdl";
    bool is_static = true;
};

struct ButtonComponent
{
    static const ComponentTypeId kTypeId = ComponentTypeId::ButtonComponent;
    void buttonPressed() { EcsHooks::ButtonPressed(this); }
};

struct LevelComponent
{
    static const ComponentTypeId kTypeId = ComponentTypeId::LevelComponent;
    const char* level_name = "";
    int wait_time = 0;
    const char* args = "";
};

struct DoorComponent
{
    static const ComponentTypeId kTypeId = ComponentTypeId::DoorComponent;
    float door_weight = 1.0f;
    bool is_locked = false;
    void unlockDoor() { EcsHooks::UnlockDoor(this); }
    void lockDoor() { EcsHooks::LockDoor(this); }
};

// Per-entity aggregates: one typed struct per Trenchbroom classname.
struct Ecs_prop_barrel
{
    HealthComponent healthComponent;
    CollisionComponent collisionComponent;
    ModelComponent modelComponent;
};

struct Ecs_prop_model
{
    TransformComponent transformComponent;
    CollisionComponent collisionComponent;
    ModelComponent modelComponent;
};

struct Ecs_func_button
{
    ButtonComponent buttonComponent;
};

struct Ecs_func_changeLevel
{
    LevelComponent levelComponent;
};

struct Ecs_func_door
{
    CollisionComponent collisionComponent;
};

// Spawn dispatch entry point (defined in the generated EcsSpawn.cpp).
// Register it with game::SetSpawnHandler(&Ecs_SpawnDispatch) in GameInit().
namespace game
{
    struct EntitySpawn;
}
bool Ecs_SpawnDispatch(const game::EntitySpawn& spawn);
