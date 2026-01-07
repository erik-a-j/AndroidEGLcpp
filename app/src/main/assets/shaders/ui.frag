#version 300 es

precision highp float;
precision highp int;
precision highp usampler2D;

in vec2 vLocal;
in vec2 vHalf;
in float vRadius;
in float vFeather;

in vec4 vTL;
in vec4 vTR;
in vec4 vBR;
in vec4 vBL;

out vec4 fragColor;

vec3 srgbToLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 linearToSrgb(vec3 c) { return pow(c, vec3(1.0/2.2)); }

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - (b - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    float r = clamp(vRadius, 0.0, min(vHalf.x, vHalf.y));
    float d = sdRoundBox(vLocal, vHalf, r);

    float aa = max(vFeather, fwidth(d));
    float aMask = smoothstep(aa, 0.0, d);

    // map local [-half..half] -> uv [0..1]
    vec2 uv = vLocal / max(vHalf, vec2(1e-6));
    uv = uv * 0.5 + 0.5;

    vec4 top = mix(vTL, vTR, uv.x);
    vec4 bot = mix(vBL, vBR, uv.x);
    vec4 c   = mix(top, bot, uv.y);

    vec3 rgbLin = srgbToLinear(c.rgb);
    float outA = c.a * aMask;
    vec3 outRgbLin = rgbLin * outA;
    fragColor = vec4(linearToSrgb(outRgbLin), outA);
}