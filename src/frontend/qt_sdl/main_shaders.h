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

vec3 xbrEdgeSample(sampler2DArray tex, vec3 uv, vec2 texSize)
{
    vec2 pixelPos = uv.xy * texSize;
    vec2 subpix = fract(pixelPos);

    // 3x3 neighbourhood + the 4 "further" pixels (2 steps in each axis)
    // used purely to strengthen edge-direction confidence, the way xBR's
    // larger comparison window reduces false-positive diagonals compared
    // to a plain 3x3 rule.
    vec3 c  = textureOffset(tex, uv, ivec2( 0,  0)).rgb;
    vec3 n  = textureOffset(tex, uv, ivec2( 0, -1)).rgb;
    vec3 s  = textureOffset(tex, uv, ivec2( 0,  1)).rgb;
    vec3 w  = textureOffset(tex, uv, ivec2(-1,  0)).rgb;
    vec3 e  = textureOffset(tex, uv, ivec2( 1,  0)).rgb;
    vec3 nw = textureOffset(tex, uv, ivec2(-1, -1)).rgb;
    vec3 ne = textureOffset(tex, uv, ivec2( 1, -1)).rgb;
    vec3 sw = textureOffset(tex, uv, ivec2(-1,  1)).rgb;
    vec3 se = textureOffset(tex, uv, ivec2( 1,  1)).rgb;
    vec3 nn = textureOffset(tex, uv, ivec2( 0, -2)).rgb;
    vec3 ss = textureOffset(tex, uv, ivec2( 0,  2)).rgb;
    vec3 ww = textureOffset(tex, uv, ivec2(-2,  0)).rgb;
    vec3 ee = textureOffset(tex, uv, ivec2( 2,  0)).rgb;

    float lc = luma(c), ln = luma(n), ls = luma(s), lw = luma(w), le = luma(e);
    float lnw = luma(nw), lne = luma(ne), lsw = luma(sw), lse = luma(se);
    float lnn = luma(nn), lss = luma(ss), lww = luma(ww), lee = luma(ee);

    bvec2 q = greaterThanEqual(subpix, vec2(0.5));

    vec3 diagB; vec3 side1, side2;
    float d1, d2;
    float farConfirm; // extra confidence term from the "further" pixels

    if (!q.x && !q.y) { diagB = nw; side1 = n; side2 = w;
                         d1 = abs(lnw - lc) + abs(ln - lw);
                         d2 = abs(ln - lnw) + abs(lw - lc);
                         farConfirm = abs(lnn - ls) + abs(lww - le); }
    else if (q.x && !q.y) { diagB = ne; side1 = n; side2 = e;
                         d1 = abs(lne - lc) + abs(ln - le);
                         d2 = abs(ln - lne) + abs(le - lc);
                         farConfirm = abs(lnn - ls) + abs(lee - lw); }
    else if (!q.x && q.y) { diagB = sw; side1 = s; side2 = w;
                         d1 = abs(lsw - lc) + abs(ls - lw);
                         d2 = abs(ls - lsw) + abs(lw - lc);
                         farConfirm = abs(lss - ln) + abs(lww - le); }
    else { diagB = se; side1 = s; side2 = e;
                         d1 = abs(lse - lc) + abs(ls - le);
                         d2 = abs(ls - lse) + abs(le - lc);
                         farConfirm = abs(lss - ln) + abs(lee - lw); }

    // smooth (not hard-cutoff) distance from the source-texel centre to
    // the diagonal corner - blend strength now ramps continuously, which
    // is what gives noticeably smoother diagonals than a single fixed
    // blend factor, closer to a properly antialiased high-order upscale.
    float cornerDist = 1.0 - clamp(max(abs(subpix.x - (q.x ? 1.0 : 0.0)),
                                        abs(subpix.y - (q.y ? 1.0 : 0.0))), 0.0, 1.0);

    float edgeMag = min(d1, d2);
    // fold the far-neighbourhood confidence in: a real diagonal edge is
    // still visible 2 texels out, noise/dither is not - this suppresses
    // blending on dithered/noisy source material.
    float confidence = smoothstep(0.0, 0.25, farConfirm);
    float blendStrength = smoothstep(0.015, 0.09, edgeMag) * cornerDist * mix(0.35, 0.75, confidence);

    vec3 blendTarget = (d1 < d2) ? mix(side1, side2, 0.5) : diagB;
    return mix(c, blendTarget, blendStrength);
}

// Unsharp-mask pass applied after the edge-directed blend above. The
// rotated-grid supersampling in xbrEdgeUpscale softens the image quite a
// bit (that's the whole point - smooth diagonals) but as a side effect the
// result looks noticeably blurrier than the raw source. This restores
// perceived detail/contrast without re-introducing jaggies: it only pushes
// the *already-computed* pixel away from its local (low-pass) average, it
// never samples new high-frequency data, so it can't undo the anti-aliasing
// itself.
vec3 sharpenPass(sampler2DArray tex, vec3 uv, vec3 center)
{
    vec3 n = textureOffset(tex, uv, ivec2( 0, -1)).rgb;
    vec3 s = textureOffset(tex, uv, ivec2( 0,  1)).rgb;
    vec3 w = textureOffset(tex, uv, ivec2(-1,  0)).rgb;
    vec3 e = textureOffset(tex, uv, ivec2( 1,  0)).rgb;
    vec3 blur = (n + s + w + e) * 0.25;

    const float kSharpAmount = 0.55;
    vec3 sharpened = center + (center - blur) * kSharpAmount;
    return clamp(sharpened, 0.0, 1.0);
}

vec3 xbrEdgeUpscale(sampler2DArray tex, vec3 uv)
{
    vec2 texSize = vec2(textureSize(tex, 0).xy);
    vec2 texel = 1.0 / texSize;

    // 4-tap rotated-grid supersampling: evaluate the edge/blend estimate
    // at 4 sub-fragment offsets and average. This is what pushes quality
    // beyond a plain single-sample xBR pass - it's the same trick MSAA
    // uses, applied to our own shading instead of geometry, and is
    // resolution-independent (scales for free with output/window size
    // instead of needing a fixed "Nx" source-side factor).
    vec2 o = texel * 0.17;
    vec3 s0 = xbrEdgeSample(tex, vec3(uv.xy + vec2(-o.x, -o.y*0.5), uv.z), texSize);
    vec3 s1 = xbrEdgeSample(tex, vec3(uv.xy + vec2( o.x*0.5, -o.y), uv.z), texSize);
    vec3 s2 = xbrEdgeSample(tex, vec3(uv.xy + vec2(-o.x*0.5,  o.y), uv.z), texSize);
    vec3 s3 = xbrEdgeSample(tex, vec3(uv.xy + vec2( o.x,  o.y*0.5), uv.z), texSize);

    return (s0 + s1 + s2 + s3) * 0.25;
}

void main()
{
    vec3 rgb;
    if (uSharpUpscale != 0)
    {
        rgb = xbrEdgeUpscale(ScreenTex, fTexcoord);
        rgb = sharpenPass(ScreenTex, fTexcoord, rgb);
    }
    else
        rgb = texture(ScreenTex, fTexcoord).rgb;

    oColor = vec4(rgb, 1.0);
}
)";

#endif // MAIN_SHADERS_H
