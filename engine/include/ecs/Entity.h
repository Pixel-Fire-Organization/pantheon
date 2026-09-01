#pragma once

namespace game
{
    struct EntityProp
    {
        const char* key;
        const char* value;
    };
    struct EntitySpawn
    {
        const char* classname;
        const EntityProp* props;
        int propCount;
        float x, y, z; // origin
    };

    // A handler returns true if it recognised and spawned the classname.
    typedef bool (*SpawnHandler)(const EntitySpawn& spawn);
} // namespace game
