
varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying vec2 vUv;
varying vec4 vColor;

uniform vec3 uCameraPos;
uniform vec3 uAmbient;
uniform vec4 uLightPosOrDir[GFX_MAX_LIGHTS];
uniform vec4 uLightColorIntensity[GFX_MAX_LIGHTS];
uniform vec4 uLightRange[GFX_MAX_LIGHTS];
uniform int uShadowCaster;
uniform mat4 uLightViewProj;
uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalTex;
uniform sampler2D uOrmTex;
uniform sampler2DShadow uShadowMap;
uniform vec4 uBaseColor;
uniform vec3 uEmissive;
uniform vec4 uMrna;
uniform float uAlphaMask;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float SampleShadow(vec3 worldPos) {
    if (uShadowCaster < 0) {
        return 1.0;
    }
    vec4 lightClip = uLightViewProj * vec4(worldPos, 1.0);
    if (lightClip.w <= 0.0) {
        return 1.0;
    }
    vec3 ndc = lightClip.xyz / lightClip.w;
    vec3 shadowUv = ndc * 0.5 + 0.5;
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 || shadowUv.y < 0.0 || shadowUv.y > 1.0 || shadowUv.z < 0.0 || shadowUv.z > 1.0) {
        return 1.0;
    }
    float bias = 0.0025;
    float texel = 1.0 / GFX_SHADOW_MAP_SIZE;
    float sum = 0.0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 offs = vec2(float(dx), float(dy)) * texel;
            sum += shadow2D(uShadowMap, vec3(shadowUv.xy + offs, shadowUv.z - bias)).r;
        }
    }
    return sum / 9.0;
}

void main() {
    vec4 albedoSample = texture2D(uAlbedoTex, vUv);
    vec3 albedo = albedoSample.rgb * uBaseColor.rgb;
    float alpha = albedoSample.a * uBaseColor.a;
    if (uAlphaMask > 0.5 && alpha < uMrna.w) {
        discard;
    }

    float metallic = clamp(uMrna.x, 0.0, 1.0);
    float roughness = clamp(uMrna.y, 0.045, 1.0);

    vec3 N = normalize(vWorldNormal);
    if (dot(vWorldNormal, vWorldNormal) < 0.0001) {
        N = vec3(0.0, 1.0, 0.0);
    }

    vec3 posDx = dFdx(vWorldPos);
    vec3 posDy = dFdy(vWorldPos);
    vec2 uvDx = dFdx(vUv);
    vec2 uvDy = dFdy(vUv);
    vec3 T = posDx * uvDy.y - posDy * uvDx.y;
    float tdott = dot(T, T);
    if (tdott < 1e-10) {
        T = vec3(1.0, 0.0, 0.0);
    } else {
        T = T * inversesqrt(tdott);
    }
    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T);

    vec3 normalSample = texture2D(uNormalTex, vUv).xyz * 2.0 - vec3(1.0);
    vec3 mapped = normalize(vec3(normalSample.x * uMrna.z, normalSample.y * uMrna.z, normalSample.z));
    N = normalize(T * mapped.x + B * mapped.y + N * mapped.z);

    vec4 orm = texture2D(uOrmTex, vUv);
    float occlusion = orm.r;
    float finalRoughness = clamp(orm.g * roughness, 0.045, 1.0);
    float finalMetallic = clamp(orm.b * metallic, 0.0, 1.0);

    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, finalMetallic);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < GFX_MAX_LIGHTS; i++) {
        vec4 posOrDir = uLightPosOrDir[i];
        vec4 colorIntensity = uLightColorIntensity[i];
        if (colorIntensity.w <= 0.0) {
            continue;
        }

        vec3 L;
        float attenuation = 1.0;
        if (posOrDir.w < 0.5) {
            L = normalize(-posOrDir.xyz);
        } else {
            vec3 toLight = posOrDir.xyz - vWorldPos;
            float dist = length(toLight);
            float range = max(uLightRange[i].x, 1e-4);
            L = toLight / max(dist, 1e-4);
            attenuation = clamp(1.0 - (dist / range), 0.0, 1.0);
            attenuation = attenuation * attenuation;
        }

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) {
            continue;
        }

        float shadowFactor = 1.0;
        if (i == uShadowCaster) {
            shadowFactor = SampleShadow(vWorldPos);
        }

        vec3 H = normalize(V + L);
        float NDF = DistributionGGX(N, H, finalRoughness);
        float G = GeometrySmith(N, V, L, finalRoughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
        vec3 kD = (vec3(1.0) - F) * (1.0 - finalMetallic);
        vec3 radiance = colorIntensity.rgb * colorIntensity.w * attenuation;

        Lo += (kD * albedo / PI + specular) * radiance * NdotL * shadowFactor;
    }

    vec3 indirect = (vColor.rgb + uAmbient) * albedo * occlusion;
    vec3 finalRgb = indirect + Lo + uEmissive;
    gl_FragColor = vec4(finalRgb, alpha);
}
