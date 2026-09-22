// Depth-only shadow-pass fragment shader, shared by deko3d and opengl-nx --
// writes NDC depth into an ordinary colour target's R channel (see
// scene_shadow.vert.glsl).
layout (location = 0) in float vDepth;

layout (location = 0) out vec4 oColor;

void main()
{
    oColor = vec4(vDepth, vDepth, vDepth, 1.0);
}
