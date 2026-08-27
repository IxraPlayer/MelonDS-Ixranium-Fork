/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef MAIN_SHADERS_H
#define MAIN_SHADERS_H

const char* kScreenVS = R"(#version 140

uniform vec2 uScreenSize;
uniform mat2x3 uTransform;

in vec2 vPosition;
in vec3 vTexcoord;

smooth out vec3 fTexcoord;

void main()
{
    vec4 fpos;

    fpos.xy = vec3(vPosition, 1.0) * uTransform;

    fpos.xy = ((fpos.xy * 2.0) / uScreenSize) - 1.0;
    fpos.y *= -1;
    fpos.z = 0.0;
    fpos.w = 1.0;

    gl_Position = fpos;
    fTexcoord = vTexcoord;
}
)";

// GPU "Optimized Graphics" pipeline: two proven, complementary
// techniques layered on top of each other in a single fragment-shader
// pass (runs entirely on the GPU as part of the normal screen-blit draw
// call - no extra render target, no CPU work, no extra thread sync).
//
//   1) xBR-style edge-directed upscale - looks at the 3x3 neighbourhood,
//      finds the local diagonal edge direction from luma differences,
//      and blends the corner texel towards it with a smooth (never
//      binary/branching) blend strength. This is what actually improves
//      pixel-art diagonals instead of leaving them stair-stepped.
//   2) CAS (Contrast Adaptive Sharpening, AMD's algorithm from FSR) -
//      a min/max-based local-contrast sharpen. Chosen over a plain
//      unsharp mask because its weight is derived from and clamped by
//      the local min/max, which is what keeps it from blowing out into
//      halos/ringing the way unsharp masks can on high-contrast edges.
//
// Both stages are purely local, per-pixel, and free of any directional
// binary choice (no "pick side A or B" branching) - that's what a
// gradient-direction pick flip-flops on soft edges and reads as
// flicker/static, so neither stage here does that. Toggled at runtime
// via uSharpUpscale so filtering can stay off for users who prefer raw
// nearest/bilinear.
const char* kScreenFS = R"(#version 140

uniform sampler2DArray ScreenTex;
uniform int uSharpUpscale;

smooth in vec3 fTexcoord;

out vec4 oColor;

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Stage 1: xBR-style edge-directed corner blend.
vec3 edgeUpscale(sampler2DArray tex, vec3 uv, vec2 texSize)
{
    vec2 pixelPos = uv.xy * texSize;
    vec2 subpix = fract(pixelPos);

    vec3 c  = textureOffset(tex, uv, ivec2( 0,  0)).rgb;
    vec3 n  = textureOffset(tex, uv, ivec2( 0, -1)).rgb;
    vec3 s  = textureOffset(tex, uv, ivec2( 0,  1)).rgb;
    vec3 w  = textureOffset(tex, uv, ivec2(-1,  0)).rgb;
    vec3 e  = textureOffset(tex, uv, ivec2( 1,  0)).rgb;
    vec3 nw = textureOffset(tex, uv, ivec2(-1, -1)).rgb;
    vec3 ne = textureOffset(tex, uv, ivec2( 1, -1)).rgb;
    vec3 sw = textureOffset(tex, uv, ivec2(-1,  1)).rgb;
    vec3 se = textureOffset(tex, uv, ivec2( 1,  1)).rgb;

    float lc = luma(c), ln = luma(n), ls = luma(s), lw = luma(w), le = luma(e);
    float lnw = luma(nw), lne = luma(ne), lsw = luma(sw), lse = luma(se);

    bvec2 q = greaterThanEqual(subpix, vec2(0.5));

    vec3 diagB; vec3 side1, side2;
    float d1, d2;

    if (!q.x && !q.y) { diagB = nw; side1 = n; side2 = w;
                         d1 = abs(lnw - lc) + abs(ln - lw);
                         d2 = abs(ln - lnw) + abs(lw - lc); }
    else if (q.x && !q.y) { diagB = ne; side1 = n; side2 = e;
                         d1 = abs(lne - lc) + abs(ln - le);
                         d2 = abs(ln - lne) + abs(le - lc); }
    else if (!q.x && q.y) { diagB = sw; side1 = s; side2 = w;
                         d1 = abs(lsw - lc) + abs(ls - lw);
                         d2 = abs(ls - lsw) + abs(lw - lc); }
    else { diagB = se; side1 = s; side2 = e;
                         d1 = abs(lse - lc) + abs(ls - le);
                         d2 = abs(ls - lse) + abs(le - lc); }

    // Continuous distance-from-corner falloff - never a hard cutoff, so
    // there's no per-pixel/per-frame flip to read as noise.
    float cornerDist = 1.0 - clamp(max(abs(subpix.x - (q.x ? 1.0 : 0.0)),
                                        abs(subpix.y - (q.y ? 1.0 : 0.0))), 0.0, 1.0);

    float edgeMag = min(d1, d2);
    float blendStrength = smoothstep(0.015, 0.07, edgeMag) * cornerDist;

    vec3 sideAvg = mix(side1, side2, 0.5);
    vec3 blendTarget = mix(diagB, sideAvg, 0.5 - 0.5 * clamp((d2 - d1) / max(d1 + d2, 1e-4), -1.0, 1.0));
    return mix(c, blendTarget, blendStrength);
}

// Stage 2: CAS (Contrast Adaptive Sharpening). Samples a plus-shaped
// neighbourhood, derives a per-pixel sharpen weight from the local
// min/max (so flat areas and already-high-contrast edges naturally get
// little to no sharpening, only genuine mid-contrast detail does), and
// folds that weight back in through a normalised blend - this is what
// keeps CAS from ringing the way a fixed-amount unsharp mask can.
// Takes the edge-blended colour as its centre sample (rather than the
// raw texel) so the sharpen is applied on top of stage 1's result
// instead of being computed independently and then diluted against it.
vec3 casSharpen(sampler2DArray tex, vec3 uv, vec3 center)
{
    vec3 a = textureOffset(tex, uv, ivec2( 0, -1)).rgb; // N
    vec3 b = textureOffset(tex, uv, ivec2(-1,  0)).rgb; // W
    vec3 e = center;
    vec3 f = textureOffset(tex, uv, ivec2( 1,  0)).rgb; // E
    vec3 g = textureOffset(tex, uv, ivec2( 0,  1)).rgb; // S

    vec3 mn = min(min(min(a, b), min(f, g)), e);
    vec3 mx = max(max(max(a, b), max(f, g)), e);

    vec3 rcpMax = 1.0 / max(mx, vec3(1e-4));
    vec3 ampl = clamp(min(mn, 2.0 - mx) * rcpMax, 0.0, 1.0);
    ampl = sqrt(ampl);

    // sharpness in [0,1] - raised from the initial 0.5 default, which
    // read as barely-there against a full pixel-art source.
    const float sharpness = 0.85;
    float peak = -1.0 / mix(8.0, 5.0, sharpness);
    vec3 cw = ampl * peak;

    vec3 rcpWeight = 1.0 / (1.0 + 4.0 * cw);
    vec3 result = (a * cw + b * cw + f * cw + g * cw + e) * rcpWeight;
    return clamp(result, 0.0, 1.0);
}

void main()
{
    vec3 rgb;
    if (uSharpUpscale != 0)
    {
        vec2 texSize = vec2(textureSize(ScreenTex, 0).xy);
        vec3 edged = edgeUpscale(ScreenTex, fTexcoord, texSize);
        // CAS runs on top of the edge-blended result, not averaged
        // against a separately-computed raw-source sharpen - that's
        // what makes both stages actually visible together instead of
        // each cancelling half of the other out.
        rgb = casSharpen(ScreenTex, fTexcoord, edged);
    }
    else
        rgb = texture(ScreenTex, fTexcoord).rgb;

    oColor = vec4(rgb, 1.0);
}
)";

#endif // MAIN_SHADERS_H
