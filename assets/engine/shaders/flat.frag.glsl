in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 oColor;
void main() {
    vec4 t = texture(uTexture, vUv);
    oColor = vec4(t.rgb * vColor.rgb, t.a * vColor.a);
}
