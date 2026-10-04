
struct Uniforms {
    viewProj : mat4x4<f32>,
};
@group(0) @binding(0) var<uniform> u : Uniforms;
@group(1) @binding(0) var tex : texture_2d<f32>;
@group(1) @binding(1) var smp : sampler;

struct VsOut {
    @builtin(position) clipPos : vec4<f32>,
    @location(0) uv : vec2<f32>,
    @location(1) color : vec4<f32>,
};

@vertex
fn vs_main(@location(0) pos : vec3<f32>,
           @location(1) normal : vec3<f32>,
           @location(2) uv : vec2<f32>,
           @location(3) color : vec4<f32>) -> VsOut {
    var out : VsOut;
    out.clipPos = u.viewProj * vec4<f32>(pos, 1.0);
    out.uv = uv;

    // Fixed headlight term, matching the flat look the vertex-lit backends
    // produce. Normals are zero for 2D geometry, which falls through to full
    // brightness.
    let n = length(normal);
    var shade = 1.0;
    if (n > 0.0001) {
        let l = normalize(vec3<f32>(0.4, 0.8, 0.45));
        shade = 0.35 + 0.65 * max(dot(normalize(normal), l), 0.0);
    }
    out.color = vec4<f32>(color.rgb * shade, color.a);
    return out;
}

@fragment
fn fs_main(in : VsOut) -> @location(0) vec4<f32> {
    let t = textureSample(tex, smp, in.uv);
    return vec4<f32>(t.rgb * in.color.rgb, t.a * in.color.a);
}
