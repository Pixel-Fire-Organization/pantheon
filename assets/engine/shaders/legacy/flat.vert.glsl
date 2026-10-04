attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec2 aUv;
attribute vec4 aColor;
uniform mat4 uViewProj;
varying vec2 vUv;
varying vec4 vColor;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vUv = aUv;
    float n = length(aNormal);
    float shade = 1.0;
    if (n > 0.0001) {
        vec3 l = normalize(vec3(0.4, 0.8, 0.45));
        shade = 0.35 + 0.65 * max(dot(normalize(aNormal), l), 0.0);
    }
    vColor = vec4(aColor.rgb * shade, aColor.a);
}
