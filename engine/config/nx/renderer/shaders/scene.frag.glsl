layout (location = 0) in vec2 vUv;
layout (location = 1) in vec4 vColor;

layout (binding = 0) uniform sampler2D uTexture;

layout (location = 0) out vec4 oColor;

void main()
{
    vec4 t = texture(uTexture, vUv);
    oColor = vec4(t.rgb * vColor.rgb, t.a * vColor.a);
}
