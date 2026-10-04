
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;
uniform mat4 uViewProj;
out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vUv;
out vec4 vColor;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vWorldNormal = aNormal;
    vUv = aUv;
    vColor = aColor;
}
