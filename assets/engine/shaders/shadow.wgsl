
struct ShadowUniforms {
    lightViewProj : mat4x4<f32>,
};
@group(0) @binding(0) var<uniform> u : ShadowUniforms;

@vertex
fn vs_main_shadow(@location(0) pos : vec3<f32>) -> @builtin(position) vec4<f32> {
    return u.lightViewProj * vec4<f32>(pos, 1.0);
}
