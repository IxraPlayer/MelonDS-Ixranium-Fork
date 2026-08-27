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

// GPU Anime4K-style upscale ("Optimized Graphics"), adapted from the
// Anime4K family of shaders (gradient-guided line push + refine) into a
// single fragment-shader pass - runs entirely on the GPU as part of the
// normal screen-blit draw call, no extra render target, no CPU work, no
// extra thread synchronisation. Anime4K normally chains several passes
// (luminance gradient -> push/thin -> refine); here that pipeline is
// folded into one pass: a Sobel gradient finds each line's direction and
// strength, a "push" step samples along the gradient normal and pulls
// the centre texel towards whichever side is more extreme (Anime4K's
// core line-thinning trick, so linework stays crisp instead of smearing
// into a flat gradient when magnified), and a refine/unsharp step
// restores the contrast the gradient step softens. Toggled at runtime
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

// Anime4K-style "push" (gradient-guided line thinning): estimate the
// local luminance gradient with a Sobel operator, then step a short
// distance along that gradient (i.e. across the line/edge) on both
// sides and blend the centre sample towards whichever side is more
// extreme relative to it. The blend weight is continuous rather than a
// hard either/or pick - on near-flat gradients a hard pick flips sign
// from one pixel (or one frame) to the next and reads as noise/static,
// so this fades smoothly to "no push at all" as the gradient weakens.
vec3 anime4kPush(sampler2DArray tex, vec3 uv, vec2 texel)
{
    vec3 c  = textureOffset(tex, uv, ivec2( 0,  0)).rgb;
    vec3 n  = textureOffset(tex, uv, ivec2( 0, -1)).rgb;
    vec3 s  = textureOffset(tex, uv, ivec2( 0,  1)).rgb;
    vec3 w  = textureOffset(tex, uv, ivec2(-1,  0)).rgb;
    vec3 e  = textureOffset(tex, uv, ivec2( 1,  0)).rgb;
    vec3 nw = textureOffset(tex, uv, ivec2(-1, -1)).rgb;
    vec3 ne = textureOffset(tex, uv, ivec2( 1, -1)).rgb;
    vec3 sw = textureOffset(tex, uv, ivec2(-1,  1)).rgb;
    vec3 se = textureOffset(tex, uv, ivec2( 1,  1)).rgb;

    float lnw = luma(nw), ln = luma(n), lne = luma(ne);
    float lw  = luma(w),                le = luma(e);
    float lsw = luma(sw), ls = luma(s), lse = luma(se);

    // Sobel gradient - points across the strongest local edge.
    float gx = (lne + 2.0*le + lse) - (lnw + 2.0*lw + lsw);
    float gy = (lsw + 2.0*ls + lse) - (lnw + 2.0*ln + lne);
    float gmag = length(vec2(gx, gy));

    // Below this the gradient is just source noise/dither, not a real
    // line - leave it completely alone rather than pushing it around.
    if (gmag < 0.05)
        return c;

    vec2 dir = vec2(gx, gy) / gmag;

    vec3 pushPos = texture(tex, vec3(uv.xy + dir * texel, uv.z)).rgb;
    vec3 pushNeg = texture(tex, vec3(uv.xy - dir * texel, uv.z)).rgb;

    float lc = luma(c), lp = luma(pushPos), lm = luma(pushNeg);

    // Continuous weight towards whichever side is more extreme, instead
    // of a hard branch - avoids the per-pixel flip-flopping that reads
    // as parasitic noise on gently sloped edges.
    float w = clamp(0.5 + (lp - lm) * 2.0, 0.0, 1.0);
    vec3 target = mix(pushNeg, pushPos, w);

    float strength = smoothstep(0.05, 0.35, gmag) * 0.15;
    return mix(c, target, strength);
}

// Refine/unsharp pass, run after the push step above - Anime4K's own
// pipeline ends the same way, since gradient-guided pushing sharpens
// line direction but softens overall contrast. This restores perceived
// detail without reintroducing jaggies: it only pushes the
// already-computed pixel away from its local (low-pass) average, it
// never samples new high-frequency data, so it can't undo the push step.
vec3 refinePass(sampler2DArray tex, vec3 uv, vec3 center)
{
    vec3 n  = textureOffset(tex, uv, ivec2( 0, -1)).rgb;
    vec3 s  = textureOffset(tex, uv, ivec2( 0,  1)).rgb;
    vec3 w  = textureOffset(tex, uv, ivec2(-1,  0)).rgb;
    vec3 e  = textureOffset(tex, uv, ivec2( 1,  0)).rgb;
    vec3 nw = textureOffset(tex, uv, ivec2(-1, -1)).rgb;
    vec3 ne = textureOffset(tex, uv, ivec2( 1, -1)).rgb;
    vec3 sw = textureOffset(tex, uv, ivec2(-1,  1)).rgb;
    vec3 se = textureOffset(tex, uv, ivec2( 1,  1)).rgb;
    // 8-tap (3x3 minus centre) low-pass - a wide, round blur kernel
    // means the unsharp mask pulls out real texture detail evenly in
    // every direction instead of only along the axes.
    vec3 blur = (n + s + w + e) * 0.15 + (nw + ne + sw + se) * 0.1;

    const float kSharpAmount = 0.35;
    // Clamp the push so real edges sharpen without blowing out into
    // visible halos/ringing on high-contrast boundaries - this pass
    // runs on top of the push step above rather than raw source, so it
    // needs a lighter touch than a standalone unsharp mask would.
    vec3 diff = clamp(center - blur, -0.15, 0.15);
    return clamp(center + diff * kSharpAmount, 0.0, 1.0);
}

vec3 anime4kUpscale(sampler2DArray tex, vec3 uv)
{
    vec2 texSize = vec2(textureSize(tex, 0).xy);
    vec2 texel = 1.0 / texSize;

    // Single centre sample - no multi-tap supersampling. Averaging
    // several independently-estimated push directions turned out to be
    // where the flickering/static came from (each sub-sample can pick a
    // slightly different gradient direction on soft source edges), so
    // this keeps the estimate to one stable sample per output pixel.
    return anime4kPush(tex, uv, texel);
}

void main()
{
    vec3 rgb;
    if (uSharpUpscale != 0)
    {
        rgb = anime4kUpscale(ScreenTex, fTexcoord);
        rgb = refinePass(ScreenTex, fTexcoord, rgb);
    }
    else
        rgb = texture(ScreenTex, fTexcoord).rgb;

    oColor = vec4(rgb, 1.0);
}
)";

#endif // MAIN_SHADERS_H
