
attribute vec3 aPos;
uniform mat4 uLightViewProj;
void main() {
    gl_Position = uLightViewProj * vec4(aPos, 1.0);
}
