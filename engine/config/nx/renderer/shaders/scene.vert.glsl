layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUv;
layout (location = 3) in vec4 aColor;

layout (std140, binding = 0) uniform SceneTransform
{
    mat4 viewProj;
} uScene;

layout (location = 0) out vec2 vUv;
layout (location = 1) out vec4 vColor;

void main()
{
    gl_Position = uScene.viewProj * vec4(aPos, 1.0);
    vUv = aUv;
    float n = length(aNormal);
    float shade = 1.0;
    if (n > 0.0001)
    {
        vec3 l = normalize(vec3(0.4, 0.8, 0.45));
        shade = 0.35 + 0.65 * max(dot(normalize(aNormal), l), 0.0);
    }
    vColor = vec4(aColor.rgb * shade, aColor.a);
}
