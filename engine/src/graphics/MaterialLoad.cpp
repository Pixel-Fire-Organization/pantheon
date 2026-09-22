#include "../include/graphics/MaterialFormat.h"

#include <cmath>
#include <cstring>

#include "../../include/core/EngineDebug.h"

bool Material_LoadBaked(const void* data, size_t size, Material* outMaterial, MaterialTextureResolver resolver, void* resolverUser)
{
    if (!data || !outMaterial || size < sizeof(MaterialAssetHeader))
    {
        Engine_LogError("Material: blob is %zu bytes, need at least %zu.", size, sizeof(MaterialAssetHeader));
        return false;
    }

    MaterialAssetHeader hdr;
    std::memcpy(&hdr, data, sizeof(hdr));

    if (hdr.magic != BAKED_MATERIAL_MAGIC)
    {
        Engine_LogError("Material: bad magic 0x%08X.", hdr.magic);
        return false;
    }
    if (hdr.version != BAKED_MATERIAL_VERSION)
    {
        Engine_LogError("Material: unsupported version %u.", hdr.version);
        return false;
    }
    if (hdr.shaderType >= static_cast<uint8_t>(MaterialShaderType::Count))
    {
        Engine_LogError("Material: shader type %u is not one this build knows.", hdr.shaderType);
        return false;
    }

    // Establish, then publish: stage every field into a local object and
    // commit it to the caller's struct only once every check has passed.
    Material staged;
    std::memset(&staged, 0, sizeof(staged));
    staged.shaderType = hdr.shaderType;
    staged.flags = hdr.flags;

    for (int i = 0; i < MATERIAL_MAX_FLOAT_PARAMS; ++i)
    {
        if (!std::isfinite(hdr.floatParams[i]))
        {
            Engine_LogError("Material: floatParams[%d] is not finite.", i);
            return false;
        }
        staged.floatParams[i] = hdr.floatParams[i];
    }
    for (int i = 0; i < MATERIAL_MAX_COLOR_PARAMS; ++i)
    {
        for (int c = 0; c < 4; ++c)
        {
            if (!std::isfinite(hdr.colorParams[i][c]))
            {
                Engine_LogError("Material: colorParams[%d][%d] is not finite.", i, c);
                return false;
            }
            staged.colorParams[i][c] = hdr.colorParams[i][c];
        }
    }
    for (int i = 0; i < MATERIAL_MAX_TEXTURE_SLOTS; ++i)
    {
        const uint32_t ref = hdr.textureRefs[i];
        staged.textureRefs[i] = (resolver && ref != MATERIAL_TEXREF_NONE) ? resolver(ref, resolverUser) : -1;
    }

    *outMaterial = staged;
    return true;
}
