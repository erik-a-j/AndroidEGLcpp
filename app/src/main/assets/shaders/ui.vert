#version 300 es
// ui.vert

precision mediump float;

uniform mat4 uMVP;

layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aColor;
layout(location=2) in vec2 aCenter;
layout(location=3) in vec2 aHalf;
layout(location=4) in float aRadius;
layout(location=5) in float aFeather;

out vec4 vColor;
out vec2 vLocal;
out vec2 vHalf;
out float vRadius;
out float vFeather;

void main() {
    vColor = aColor;
    vLocal = aPos - aCenter;
    vHalf = aHalf;
    vRadius = aRadius;
    vFeather = aFeather;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}