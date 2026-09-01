#pragma once

namespace game
{
    // One resource a scene wants attached before or during its run.
    // type: "TEXTURE","MODEL","SOUND","FONT" — the same strings as LoadResource.
    struct SceneResource
    {
        const char* type;
        const char* path;
    };
} // namespace game
