#version 300 es
// text.frag

precision highp float;

uniform sampler2D uTex;

in vec2 vUV;
in vec4 vColor;

out vec4 fragColor;

void main() {
    float a = texture(uTex, vUV).r;  // atlas stored in R8/RED
    fragColor = vec4(vColor.rgb, vColor.a * a);
}