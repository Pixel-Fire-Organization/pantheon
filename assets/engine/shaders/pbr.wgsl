
struct GpuLight {
    positionOrDir : vec4<f32>,
    colorIntensity : vec4<f32>,
    rangeParams : vec4<f32>,
};

struct FrameUniforms {
    viewProj : mat4x4<f32>,
    lightViewProj : mat4x4<f32>,
    cameraPos : vec4<f32>,
    ambient : vec4<f32>,
    lights : array<GpuLight, GFX_MAX_LIGHTS_PLACEHOLDER>,
    shadowCaster : vec4<f32>,
};
@group(0) @binding(0) var<uniform> u : FrameUniforms;
@group(0) @binding(1) var shadowTex : texture_depth_2d;
@group(0) @binding(2) var shadowSmp : sampler_comparison;

struct MaterialUniform {
    baseColor : vec4<f32>,
    emissive : vec4<f32>,
    mrna : vec4<f32>, // metallic, roughness, normalScale, alphaCutoff
    matFlags : vec4<f32>, // x = alpha-mask enabled
};
@group(1) @binding(0) var<uniform> mat : MaterialUniform;

@group(2) @binding(0) var albedoTex : texture_2d<f32>;
@group(2) @binding(1) var normalTex : texture_2d<f32>;
@group(2) @binding(2) var ormTex : texture_2d<f32>;
@group(2) @binding(3) var matSmp : sampler;

struct VsOut {
    @builtin(position) clipPos : vec4<f32>,
    @location(0) worldPos : vec3<f32>,
    @location(1) worldNormal : vec3<f32>,
    @location(2) uv : vec2<f32>,
    @location(3) color : vec4<f32>,
};

@vertex
fn vs_main_3d(@location(0) pos : vec3<f32>,
              @location(1) normal : vec3<f32>,
              @location(2) uv : vec2<f32>,
              @location(3) color : vec4<f32>) -> VsOut {
    var out : VsOut;
    out.clipPos = u.viewProj * vec4<f32>(pos, 1.0);
    out.worldPos = pos;
    out.worldNormal = normal;
    out.uv = uv;
    out.color = color;
    return out;
}

const PI : f32 = 3.14159265359;

fn distributionGGX(N : vec3<f32>, H : vec3<f32>, roughness : f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH = max(dot(N, H), 0.0);
    let NdotH2 = NdotH * NdotH;
    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

fn geometrySchlickGGX(NdotV : f32, roughness : f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometrySmith(N : vec3<f32>, V : vec3<f32>, L : vec3<f32>, roughness : f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

fn fresnelSchlick(cosTheta : f32, F0 : vec3<f32>) -> vec3<f32> {
    return F0 + (vec3<f32>(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn sampleShadow(worldPos : vec3<f32>) -> f32 {
    if (u.shadowCaster.x < 0.0) {
        return 1.0;
    }
    let lightClip = u.lightViewProj * vec4<f32>(worldPos, 1.0);
    if (lightClip.w <= 0.0) {
        return 1.0;
    }
    let ndc = lightClip.xyz / lightClip.w;
    let shadowUv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 || shadowUv.y < 0.0 || shadowUv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }
    let bias = 0.0025;
    let texel = 1.0 / GFX_SHADOW_MAP_SIZE_PLACEHOLDER;
    var sum = 0.0;
    for (var dy = -1; dy <= 1; dy = dy + 1) {
        for (var dx = -1; dx <= 1; dx = dx + 1) {
            let offs = vec2<f32>(f32(dx), f32(dy)) * texel;
            sum = sum + textureSampleCompare(shadowTex, shadowSmp, shadowUv + offs, ndc.z - bias);
        }
    }
    return sum / 9.0;
}

@fragment
fn fs_main_3d(in : VsOut) -> @location(0) vec4<f32> {
    let albedoSample = textureSample(albedoTex, matSmp, in.uv);
    // Pure material colour -- the vertex's incoming colour (baked static
    // lighting, or white for a dynamic model) is deliberately NOT folded in
    // here: it is added once, below, as the indirect/baked lighting term.
    // Multiplying it into albedo too would let it double up wherever albedo
    // is reused (F0, the diffuse BRDF term), squaring a static mesh's baked
    // shading instead of applying it once.
    let albedo = albedoSample.rgb * mat.baseColor.rgb;
    let alpha = albedoSample.a * mat.baseColor.a;
    if (mat.matFlags.x > 0.5 && alpha < mat.mrna.w) {
        discard;
    }

    let metallic = clamp(mat.mrna.x, 0.0, 1.0);
    let roughness = clamp(mat.mrna.y, 0.045, 1.0);

    var N = normalize(in.worldNormal);
    if (dot(in.worldNormal, in.worldNormal) < 0.0001) {
        N = vec3<f32>(0.0, 1.0, 0.0);
    }

    // Screen-space derivative tangent reconstruction: no vertex tangent
    // attribute is carried in the shared vertex format.
    let posDx = dpdx(in.worldPos);
    let posDy = dpdy(in.worldPos);
    let uvDx = dpdx(in.uv);
    let uvDy = dpdy(in.uv);
    var T = posDx * uvDy.y - posDy * uvDx.y;
    let TdotT = dot(T, T);
    if (TdotT < 1e-10) {
        T = vec3<f32>(1.0, 0.0, 0.0);
    } else {
        T = T * inverseSqrt(TdotT);
    }
    T = normalize(T - N * dot(N, T));
    let B = cross(N, T);

    let normalSample = textureSample(normalTex, matSmp, in.uv).xyz * 2.0 - vec3<f32>(1.0);
    let mapped = normalize(vec3<f32>(normalSample.x * mat.mrna.z, normalSample.y * mat.mrna.z, normalSample.z));
    N = normalize(T * mapped.x + B * mapped.y + N * mapped.z);

    let orm = textureSample(ormTex, matSmp, in.uv);
    let occlusion = orm.r;
    let finalRoughness = clamp(orm.g * roughness, 0.045, 1.0);
    let finalMetallic = clamp(orm.b * metallic, 0.0, 1.0);

    let V = normalize(u.cameraPos.xyz - in.worldPos);
    let F0 = mix(vec3<f32>(0.04), albedo, finalMetallic);
    let shadowCasterIndex = i32(u.shadowCaster.x);

    var Lo = vec3<f32>(0.0);
    for (var i = 0; i < GFX_MAX_LIGHTS_PLACEHOLDER; i = i + 1) {
        let light = u.lights[i];
        if (light.colorIntensity.w <= 0.0) {
            continue;
        }

        var L : vec3<f32>;
        var attenuation = 1.0;
        if (light.positionOrDir.w < 0.5) {
            L = normalize(-light.positionOrDir.xyz);
        } else {
            let toLight = light.positionOrDir.xyz - in.worldPos;
            let dist = length(toLight);
            let range = max(light.rangeParams.x, 1e-4);
            L = toLight / max(dist, 1e-4);
            attenuation = clamp(1.0 - (dist / range), 0.0, 1.0);
            attenuation = attenuation * attenuation;
        }

        let NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) {
            continue;
        }

        var shadowFactor = 1.0;
        if (i == shadowCasterIndex) {
            shadowFactor = sampleShadow(in.worldPos);
        }

        let H = normalize(V + L);
        let NDF = distributionGGX(N, H, finalRoughness);
        let G = geometrySmith(N, V, L, finalRoughness);
        let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        let specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
        let kD = (vec3<f32>(1.0) - F) * (1.0 - finalMetallic);
        let radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;

        Lo = Lo + (kD * albedo / PI + specular) * radiance * NdotL * shadowFactor;
    }

    // The vertex's incoming colour is baked static lighting (sectors) or
    // flat white (dynamic models); occlusion only attenuates this indirect/
    // baked term, matching how ambient occlusion is conventionally applied
    // to indirect rather than direct light.
    let indirect = (in.color.rgb + u.ambient.rgb) * albedo * occlusion;
    let finalRgb = indirect + Lo + mat.emissive.rgb;
    return vec4<f32>(finalRgb, alpha);
}
