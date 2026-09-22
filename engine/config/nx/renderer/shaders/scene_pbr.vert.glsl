// PBR main-scene vertex shader, shared by deko3d and opengl-nx (see
// tools/nx_shader.py -- one source, so a frame that differs between the two
// backends is a bug, not an intentional shader difference). Vertex positions/
// normals arrive already in world space -- StagedGeometry bakes each object's
// model matrix into the vertex buffer on the CPU rather than uploading one
// per draw, so this stage only ever applies the camera's view-projection.
// See scene_pbr.frag.glsl for the shading model and docs/nx/renderers/DEKO3D.md
// / docs/nx/renderers/OPENGL.md.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUv;
layout (location = 3) in vec4 aColor;

layout (std140, binding = 0) uniform FrameUniforms
{
    mat4 viewProj;
    mat4 lightViewProj;
    vec4 cameraPos; // xyz used
    vec4 ambient; // xyz used
    vec4 lightPosOrDir[4];
    vec4 lightColorIntensity[4];
    vec4 lightRange[4];
    vec4 shadowCaster; // x = active light index as a float, -1 = none
} uFrame;

layout (location = 0) out vec3 vWorldPos;
layout (location = 1) out vec3 vWorldNormal;
layout (location = 2) out vec2 vUv;
layout (location = 3) out vec4 vColor;

void main()
{
    gl_Position = uFrame.viewProj * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vWorldNormal = aNormal;
    vUv = aUv;
    vColor = aColor;
}
