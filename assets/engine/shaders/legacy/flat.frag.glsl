varying vec2 vUv;
varying vec4 vColor;
uniform sampler2D uTexture;
void main() {
    vec4 t = texture2D(uTexture, vUv);
    gl_FragColor = vec4(t.rgb * vColor.rgb, t.a * vColor.a);
}
