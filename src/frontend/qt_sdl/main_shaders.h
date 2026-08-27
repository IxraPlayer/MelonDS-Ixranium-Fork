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
// sides and pull the centre sample towards whichever side is more
// extreme relative to it. This is what keeps thin anime-style linework
// crisp under magnification instead of it smearing into a soft
// gradient, which is the point of Anime4K's line-thinning pass.
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

    if (gmag < 1e-4)
        return c;

    vec2 dir = vec2(gx, gy) / gmag;

    // The actual "push": look a little further out along the gradient
    // on each side of the centre texel to see which direction the line
    // continues towards, rather than only the immediate neighbours.
    vec3 pushPos = texture(tex, vec3(uv.xy + dir * texel * 1.5, uv.z)).rgb;
    vec3 pushNeg = texture(tex, vec3(uv.xy - dir * texel * 1.5, uv.z)).rgb;

    float lc = luma(c), lp = luma(pushPos), lm = luma(pushNeg);

    // Pull the centre towards whichever side of the line is more
    // extreme relative to it - this is what thins a line down instead
    // of just blurring across it. Kept subtle: too strong a pull here
    // drags in wrong-side colour on any real edge and reads as smeared,
    // haloed artifacting rather than a sharper line.
    vec3 target = (abs(lp - lc) > abs(lm - lc)) ? pushPos : pushNeg;

    float strength = smoothstep(0.06, 0.30, gmag) * 0.25;
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

    const float kSharpAmount = 0.6;
    // Clamp the push so real edges sharpen without blowing out into
    // visible halos/ringing on high-contrast boundaries - this pass
    // runs on top of the push step above rather than raw source, so it
    // needs a lighter touch than a standalone unsharp mask would.
    vec3 diff = clamp(center - blur, -0.25, 0.25);
    return clamp(center + diff * kSharpAmount, 0.0, 1.0);
}

vec3 anime4kUpscale(sampler2DArray tex, vec3 uv)
{
    vec2 texSize = vec2(textureSize(tex, 0).xy);
    vec2 texel = 1.0 / texSize;

    // 4-tap rotated-grid supersampling: evaluate the push estimate at 4
    // sub-fragment offsets and average - the same trick MSAA uses,
    // applied to our own shading instead of geometry, and it's
    // resolution-independent (scales for free with output/window size
    // instead of needing a fixed "Nx" source-side factor).
    vec2 o = texel * 0.4;
    vec3 s0 = anime4kPush(tex, vec3(uv.xy + vec2(-o.x, -o.y*0.5), uv.z), texel);
    vec3 s1 = anime4kPush(tex, vec3(uv.xy + vec2( o.x*0.5, -o.y), uv.z), texel);
    vec3 s2 = anime4kPush(tex, vec3(uv.xy + vec2(-o.x*0.5,  o.y), uv.z), texel);
    vec3 s3 = anime4kPush(tex, vec3(uv.xy + vec2( o.x,  o.y*0.5), uv.z), texel);

    return (s0 + s1 + s2 + s3) * 0.25;
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
