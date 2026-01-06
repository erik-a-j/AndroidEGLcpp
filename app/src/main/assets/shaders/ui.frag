#version 300 es
// ui.frag

precision mediump float;

in vec4 vColor;
in vec2 vLocal;
in vec2 vHalf;
in float vRadius;
in float vFeather;

out vec4 fragColor;

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - (b - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    float r = clamp(vRadius, 0.0, min(vHalf.x, vHalf.y));
    float d = sdRoundBox(vLocal, vHalf, r);

    float a = 1.0;
    if (vFeather > 0.0) {
        a = smoothstep(vFeather, 0.0, d);
    } else {
        a = (d <= 0.0) ? 1.0 : 0.0;
    }

    fragColor = vec4(vColor.rgb, vColor.a * a);
}