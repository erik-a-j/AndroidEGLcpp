#version 300 es
// text.vert

precision highp float;

uniform mat4 uMVP;
uniform vec2 uTranslate;

layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;   // GL_UNSIGNED_BYTE normalized -> 0..1

out vec2 vUV;
out vec4 vColor;

void main() {
    vUV = aUV;
    vColor = aColor;
    vec2 p = aPos + uTranslate;
    gl_Position = uMVP * vec4(p, 0.0, 1.0);
}