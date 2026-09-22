// Depth-only shadow-pass vertex shader, shared by deko3d and opengl-nx (see
// tools/nx_shader.py). Dynamic (model/primitive) geometry only -- static
// sector geometry already carries baked, shadow-aware lighting and is never
// rendered into this target. There is no confirmed readable-depth-texture
// path shared identically by both backends, so depth is written out through
// an ordinary colour target instead (see scene_shadow.frag.glsl) and sampled
// back as a plain texture in scene_pbr.frag.glsl's shadow lookup.
layout (location = 0) in vec3 aPos;

layout (std140, binding = 0) uniform ShadowUniforms
{
    mat4 lightViewProj;
} uShadow;

layout (location = 0) out float vDepth;

void main()
{
    gl_Position = uShadow.lightViewProj * vec4(aPos, 1.0);
    // uShadow.lightViewProj is built with zeroToOneDepth=true regardless of
    // what this platform's main camera projection uses (that choice is
    // independent -- this is a separate matrix used only for shadowing), so
    // Z is already [0,1] here, matching what scene_pbr.frag.glsl's
    // SampleShadow compares against.
    vDepth = gl_Position.z / gl_Position.w;
}
