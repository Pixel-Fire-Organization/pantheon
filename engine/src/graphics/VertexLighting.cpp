#include "graphics/VertexLighting.h"

#include <cmath>

void VertexLighting_Compute(const Vector3& worldPos, const Vector3& worldNormal, const float baseline[4], const Light3D* lights, uint32_t lightCount, const Color3& ambient, float outColor[4])
{
    const float nLenSq = worldNormal.x * worldNormal.x + worldNormal.y * worldNormal.y + worldNormal.z * worldNormal.z;
    if (nLenSq <= 0.0001f)
    {
        outColor[0] = baseline[0];
        outColor[1] = baseline[1];
        outColor[2] = baseline[2];
        outColor[3] = baseline[3];
        return;
    }

    const float invLen = 1.0f / sqrtf(nLenSq);
    const float Nx = worldNormal.x * invLen, Ny = worldNormal.y * invLen, Nz = worldNormal.z * invLen;

    float litR = baseline[0] + ambient.r;
    float litG = baseline[1] + ambient.g;
    float litB = baseline[2] + ambient.b;

    for (uint32_t i = 0; i < lightCount; ++i)
    {
        const Light3D& light = lights[i];
        if (light.intensity <= 0.0f)
            continue;

        float Lx, Ly, Lz, attenuation = 1.0f;
        if (light.type == LightType::Directional)
        {
            const float dx = light.direction.x, dy = light.direction.y, dz = light.direction.z;
            const float dLenSq = dx * dx + dy * dy + dz * dz;
            const float dInv = (dLenSq > 0.0001f) ? (1.0f / sqrtf(dLenSq)) : 0.0f;
            Lx = -dx * dInv;
            Ly = -dy * dInv;
            Lz = -dz * dInv;
        }
        else
        {
            const float tx = light.position.x - worldPos.x, ty = light.position.y - worldPos.y, tz = light.position.z - worldPos.z;
            const float dist = sqrtf(tx * tx + ty * ty + tz * tz);
            const float dInv = (dist > 0.0001f) ? (1.0f / dist) : 0.0f;
            Lx = tx * dInv;
            Ly = ty * dInv;
            Lz = tz * dInv;

            const float range = (light.range > 0.0001f) ? light.range : 0.0001f;
            float att = 1.0f - (dist / range);
            att = (att < 0.0f) ? 0.0f : ((att > 1.0f) ? 1.0f : att);
            attenuation = att * att;
        }

        const float NdotL = Nx * Lx + Ny * Ly + Nz * Lz;
        if (NdotL <= 0.0f)
            continue;

        const float scale = NdotL * light.intensity * attenuation;
        litR += light.color.r * scale;
        litG += light.color.g * scale;
        litB += light.color.b * scale;
    }

    outColor[0] = litR;
    outColor[1] = litG;
    outColor[2] = litB;
    outColor[3] = baseline[3];
}
