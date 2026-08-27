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

// GPU edge-directed upscale ("Sharp Upscale" v2), xBR-inspired.
// Single fragment-shader pass, runs entirely on the GPU as part of the
// normal screen-blit draw call - no extra render target, no CPU work,
// no extra thread synchronisation. Replaces the old CPU-side EPX/Scale2x
// 2x prepass (sharpUpscale2x in Screen.cpp), which only looked at the
// 4 orthogonal neighbours of each pixel. This version samples the full
// 3x3 neighbourhood and picks a diagonal blend direction from local
// luma edges (like xBR's edge detection step), which gives smoother,
// less jagged diagonals with very little halo/ringing risk since we
// only ever blend between the centre pixel and its immediate neighbours.
// Toggled at runtime via uSharpUpscale so filtering can stay off for
// users who prefer raw nearest/bilinear.
const char* kScreenFS = R"(#version 140

uniform sampler2DArray ScreenTex;
uniform int uSharpUpscale;

smooth in vec3 fTexcoord;

out vec4 oColor;

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec3 xbrEdgeUpscale(sampler2DArray tex, vec3 uv)
{
    vec2 texSize = vec2(textureSize(tex, 0).xy);
    vec2 texel = 1.0 / texSize;

    // position within the source texel, in [0,1)^2
    vec2 pixelPos = uv.xy * texSize;
    vec2 subpix = fract(pixelPos);

    // 3x3 neighbourhood around the centre texel
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

    // which quadrant of the source texel is this fragment in
    bvec2 q = greaterThanEqual(subpix, vec2(0.5));

    // pick the relevant corner neighbours + straight neighbours for
    // that quadrant, and estimate a diagonal edge strength (xBR-style
    // "edge detection rule": compare the two possible diagonal
    // gradients through the corner)
    vec3 diagA, diagB; // the two pixels an edge would run between
    vec3 side1, side2; // straight neighbours flanking the corner
    float d1, d2;

    if (!q.x && !q.y) { diagA = c; diagB = nw; side1 = n; side2 = w;
                         d1 = abs(lnw - lc) + abs(ln - lw);
                         d2 = abs(ln - lnw) + abs(lw - lc); }
    else if (q.x && !q.y) { diagA = c; diagB = ne; side1 = n; side2 = e;
                         d1 = abs(lne - lc) + abs(ln - le);
                         d2 = abs(ln - lne) + abs(le - lc); }
    else if (!q.x && q.y) { diagA = c; diagB = sw; side1 = s; side2 = w;
                         d1 = abs(lsw - lc) + abs(ls - lw);
                         d2 = abs(ls - lsw) + abs(lw - lc); }
    else { diagA = c; diagB = se; side1 = s; side2 = e;
                         d1 = abs(lse - lc) + abs(ls - le);
                         d2 = abs(ls - lse) + abs(le - lc); }

    // corner distance within the quadrant: 0 at centre texel, 1 at
    // the diagonal neighbour - used to fade the blend smoothly across
    // the whole output resolution instead of a hard per-source-texel
    // step, which is what gives this the "6x-class" smooth look
    // regardless of the actual output/window resolution.
    vec2 cornerFrac = q.x ? subpix - 0.5 : 0.5 - subpix;
    cornerFrac = q.y ? cornerFrac : cornerFrac; // (kept explicit for clarity)
    float cornerDist = clamp(max(abs(subpix.x - (q.x ? 1.0 : 0.0)),
                                  abs(subpix.y - (q.y ? 1.0 : 0.0))), 0.0, 1.0);
    cornerDist = 1.0 - cornerDist;

    vec3 result = c;

    // only blend near a genuine edge (skip flat areas entirely - keeps
    // pixel-art regions perfectly crisp, avoids blur/ringing on flat
    // fills which is the main failure mode of naive upscalers)
    float edgeMag = min(d1, d2);
    if (edgeMag > 0.02)
    {
        vec3 blendTarget = (d1 < d2) ? mix(side1, side2, 0.5) : diagB;
        float w2 = cornerDist * 0.6; // conservative blend strength
        result = mix(c, blendTarget, w2);
    }

    return result;
}

void main()
{
    vec3 rgb;
    if (uSharpUpscale != 0)
        rgb = xbrEdgeUpscale(ScreenTex, fTexcoord);
    else
        rgb = texture(ScreenTex, fTexcoord).rgb;

    oColor = vec4(rgb, 1.0);
}
)";

#endif // MAIN_SHADERS_H
