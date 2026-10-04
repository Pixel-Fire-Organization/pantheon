
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec2 aUv;
attribute vec4 aColor;
uniform mat4 uViewProj;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying vec2 vUv;
varying vec4 vColor;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vWorldNormal = aNormal;
    vUv = aUv;
    vColor = aColor;
}
