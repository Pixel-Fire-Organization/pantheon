// PBR main-scene fragment shader, shared by deko3d and opengl-nx (see
// tools/nx_shader.py). Metallic-roughness Cook-Torrance (GGX distribution,
// Smith geometry, Schlick Fresnel), screen-space derivative tangent
// reconstruction (no vertex tangent attribute -- the shared vertex format
// carries none), a fixed dynamic-light array, and single-shadow-caster PCF
// sampling. Mirrors the WGSL/GLSL shaders in WebGpu.cpp/OpenGl.cpp (Win32) --
// same BRDF, same baked/dynamic lighting split, same reasons.
//
// The vertex's incoming colour (vColor) is the level compiler's baked static
// lighting result for sector geometry, or flat white for dynamic models (see
// docs/formats/MATERIAL_FORMAT.md, "Static vs. dynamic lighting"). It is
// added to the dynamic ambient once, then multiplied by albedo -- never
// folded into albedo itself, which would let it double up wherever albedo is
// reused (Fresnel F0, the diffuse BRDF term).
layout (location = 0) in vec3 vWorldPos;
layout (location = 1) in vec3 vWorldNormal;
layout (location = 2) in vec2 vUv;
layout (location = 3) in vec4 vColor;

layout (std140, binding = 0) uniform FrameUniforms
{
    mat4 viewProj;
    mat4 lightViewProj;
    vec4 cameraPos;
    vec4 ambient;
    vec4 lightPosOrDir[4];
    vec4 lightColorIntensity[4];
    vec4 lightRange[4];
    vec4 shadowCaster;
} uFrame;

layout (std140, binding = 1) uniform MaterialUniform
{
    vec4 baseColor;
    vec4 emissive; // xyz used
    vec4 mrna; // metallic, roughness, normalScale, alphaCutoff
    vec4 matFlags; // x = alpha-mask enabled
} uMat;

layout (binding = 0) uniform sampler2D uAlbedoTex;
layout (binding = 1) uniform sampler2D uNormalTex;
layout (binding = 2) uniform sampler2D uOrmTex;
// An ordinary colour texture (depth in the R channel), not a depth/shadow
// sampler: this must compile and behave identically under both the offline
// NVN compiler and desktop GLSL, and a depth-compare sampler is not confirmed
// to exist the same way on both -- see the shadow pass shaders' own comment.
layout (binding = 3) uniform sampler2D uShadowMap;

layout (location = 0) out vec4 oColor;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float SampleShadow(vec3 worldPos)
{
    if (uFrame.shadowCaster.x < 0.0)
        return 1.0;
    vec4 lightClip = uFrame.lightViewProj * vec4(worldPos, 1.0);
    if (lightClip.w <= 0.0)
        return 1.0;
    vec3 ndc = lightClip.xyz / lightClip.w;
    vec2 shadowUv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 || shadowUv.y < 0.0 || shadowUv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0;

    // Manual compare against a plain colour texture -- see uShadowMap's own
    // comment. GFX_SHADOW_MAP_SIZE is hardcoded to 1024 below (matches
    // PlatformConstants.h for nx; the two Init*Shaders functions
    // static_assert this), since this tool has no preprocessor-define
    // injection to keep the two in sync automatically.
    float bias = 0.01;
    float texel = 1.0 / 1024.0;
    float sum = 0.0;
    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            vec2 offs = vec2(float(dx), float(dy)) * texel;
            float storedDepth = texture(uShadowMap, shadowUv + offs).r;
            sum += (storedDepth < ndc.z - bias) ? 0.0 : 1.0;
        }
    }
    return sum / 9.0;
}

void main()
{
    vec4 albedoSample = texture(uAlbedoTex, vUv);
    // Pure material colour -- vColor is deliberately NOT folded in here; see
    // the file header comment.
    vec3 albedo = albedoSample.rgb * uMat.baseColor.rgb;
    float alpha = albedoSample.a * uMat.baseColor.a;
    if (uMat.matFlags.x > 0.5 && alpha < uMat.mrna.w)
        discard;

    float metallic = clamp(uMat.mrna.x, 0.0, 1.0);
    float roughness = clamp(uMat.mrna.y, 0.045, 1.0);

    vec3 N = normalize(vWorldNormal);
    if (dot(vWorldNormal, vWorldNormal) < 0.0001)
        N = vec3(0.0, 1.0, 0.0);

    // Screen-space derivative tangent reconstruction: no vertex tangent
    // attribute is carried in the shared vertex format.
    vec3 posDx = dFdx(vWorldPos);
    vec3 posDy = dFdy(vWorldPos);
    vec2 uvDx = dFdx(vUv);
    vec2 uvDy = dFdy(vUv);
    vec3 T = posDx * uvDy.y - posDy * uvDx.y;
    float TdotT = dot(T, T);
    if (TdotT < 1e-10)
        T = vec3(1.0, 0.0, 0.0);
    else
        T = T * inversesqrt(TdotT);
    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T);

    vec3 normalSample = texture(uNormalTex, vUv).xyz * 2.0 - vec3(1.0);
    vec3 mapped = normalize(vec3(normalSample.x * uMat.mrna.z, normalSample.y * uMat.mrna.z, normalSample.z));
    N = normalize(T * mapped.x + B * mapped.y + N * mapped.z);

    vec4 orm = texture(uOrmTex, vUv);
    float occlusion = orm.r;
    float finalRoughness = clamp(orm.g * roughness, 0.045, 1.0);
    float finalMetallic = clamp(orm.b * metallic, 0.0, 1.0);

    vec3 V = normalize(uFrame.cameraPos.xyz - vWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, finalMetallic);
    int shadowCasterIndex = int(uFrame.shadowCaster.x);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < 4; i++)
    {
        vec4 posOrDir = uFrame.lightPosOrDir[i];
        vec4 colorIntensity = uFrame.lightColorIntensity[i];
        if (colorIntensity.w <= 0.0)
            continue;

        vec3 L;
        float attenuation = 1.0;
        if (posOrDir.w < 0.5)
        {
            L = normalize(-posOrDir.xyz);
        }
        else
        {
            vec3 toLight = posOrDir.xyz - vWorldPos;
            float dist = length(toLight);
            float range = max(uFrame.lightRange[i].x, 1e-4);
            L = toLight / max(dist, 1e-4);
            attenuation = clamp(1.0 - (dist / range), 0.0, 1.0);
            attenuation = attenuation * attenuation;
        }

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0)
            continue;

        float shadowFactor = 1.0;
        if (i == shadowCasterIndex)
            shadowFactor = SampleShadow(vWorldPos);

        vec3 H = normalize(V + L);
        float NDF = DistributionGGX(N, H, finalRoughness);
        float G = GeometrySmith(N, V, L, finalRoughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
        vec3 kD = (vec3(1.0) - F) * (1.0 - finalMetallic);
        vec3 radiance = colorIntensity.rgb * colorIntensity.w * attenuation;

        Lo += (kD * albedo / PI + specular) * radiance * NdotL * shadowFactor;
    }

    vec3 indirect = (vColor.rgb + uFrame.ambient.rgb) * albedo * occlusion;
    vec3 finalRgb = indirect + Lo + uMat.emissive.rgb;
    oColor = vec4(finalRgb, alpha);
}
