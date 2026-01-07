#version 300 es

precision highp float;
precision highp int;
precision highp usampler2D;

layout(location=0) in vec2 aCorner; // -1..1 unit quad

uniform mat4 uMVP;

// Float instance texture: RGBA16F or RGBA32F
uniform sampler2D  uInstF;
uniform int        uInstF_W;   // width in texels

// Color instance texture: RGBA8UI
uniform usampler2D uInstU;
uniform int        uInstU_W;   // width in texels

out vec2 vLocal;
out vec2 vHalf;
out float vRadius;
out float vFeather;

out vec4 vTL;
out vec4 vTR;
out vec4 vBR;
out vec4 vBL;

vec4 fetchF(int texelIndex) {
    int x = texelIndex % uInstF_W;
    int y = texelIndex / uInstF_W;
    return texelFetch(uInstF, ivec2(x, y), 0);
}

uvec4 fetchU(int texelIndex) {
    int x = texelIndex % uInstU_W;
    int y = texelIndex / uInstU_W;
    return texelFetch(uInstU, ivec2(x, y), 0);
}

vec4 u8_to_norm(uvec4 p) {
    return vec4(p) * (1.0 / 255.0);
}

void main() {
    // 2 float texels / instance
    int fBase = gl_InstanceID * 2;
    vec4 c_half  = fetchF(fBase + 0);   // cx cy hx hy
    vec4 rad_fea = fetchF(fBase + 1);   // radius feather ...

    // 4 color texels / instance
    int uBase = gl_InstanceID * 4;
    vTL = u8_to_norm(fetchU(uBase + 0));
    vTR = u8_to_norm(fetchU(uBase + 1));
    vBR = u8_to_norm(fetchU(uBase + 2));
    vBL = u8_to_norm(fetchU(uBase + 3));

    vec2 center = c_half.xy;
    vHalf       = c_half.zw;

    vRadius  = rad_fea.x;
    vFeather = rad_fea.y;

    vLocal = aCorner * vHalf;
    vec2 pos = center + vLocal;

    gl_Position = uMVP * vec4(pos, 0.0, 1.0);
}