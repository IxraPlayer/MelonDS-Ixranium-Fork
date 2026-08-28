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

// GPU "Optimized Graphics" pipeline: four proven, complementary
// techniques layered on top of each other in a single fragment-shader
// pass (runs entirely on the GPU as part of the normal screen-blit draw
// call - no extra render target, no CPU work, no extra thread sync).
//
//   0) Diagonal reconnection (Eagle/2xSaI-style) - explicitly tests
//      whether a matching-colour diagonal line runs through each
//      corner, and reconnects it if so. Catches clean diagonal strokes
//      (outlines, swirl patterns) that a pure contrast-based blend only
//      partially smooths.
//   1) xBR-style edge-directed upscale - looks at the 3x3 neighbourhood,
//      finds the local diagonal edge direction from luma differences,
//      and blends the corner texel towards it with a smooth (never
//      binary/branching) blend strength, backing off on very
//      high-contrast outlines to avoid haloing.
//   2) CAS (Contrast Adaptive Sharpening, AMD's algorithm from FSR) -
//      a min/max-based local-contrast sharpen, amplitude-capped for the
//      same outline-haloing reason as stage 1.
//   3) Selective cleanup - a final outlier-suppression pass that mops
//      up the small amount of residual per-pixel noise the first three
//      stages can leave near edges, without touching real detail (it
//      only pulls in pixels close to their own neighbourhood average,
//      so it can't flatten the image into a "plastic" look).
//
// Each stage takes the previous stage's result as its centre sample, so
// they compound instead of one overwriting another. All four are
// purely local, per-pixel, and free of any directional binary choice
// (no "pick side A or B" branching) - a gradient-direction pick
// flip-flops on soft edges and reads as flicker/static, so none of
// these do that. Toggled at runtime via uSharpUpscale so filtering can
// stay off for users who prefer raw nearest/bilinear.
const char* kScreenFS = R"(#version 140

uniform sampler2DArray ScreenTex;
uniform int uSharpUpscale;

smooth in vec3 fTexcoord;

out vec4 oColor;

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Stage 0: diagonal reconnection (Eagle/2xSaI-style). xBR's corner
// blend below softens diagonals proportionally to local contrast, but
// it never explicitly asks "is there an actual matching-colour diagonal
// line running through this corner?" - that explicit test is what Eagle
// and 2xSaI are built around, and it catches clean diagonal strokes
// (swirl patterns, text outline corners) that a pure contrast-based
// blend only partially smooths. If both pixels flanking a corner agree
// with the diagonal pixel (and the centre is the odd one out), that's a
// real diagonal line passing through - pull the centre towards it.
vec3 diagonalReconnect(sampler2DArray tex, vec3 uv, vec2 texSize)
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

    bvec2 q = greaterThanEqual(subpix, vec2(0.5));

    vec3 diag; vec3 orthoA; vec3 orthoB;
    if (!q.x && !q.y)      { diag = nw; orthoA = n; orthoB = w; }
    else if (q.x && !q.y)  { diag = ne; orthoA = n; orthoB = e; }
    else if (!q.x && q.y)  { diag = sw; orthoA = s; orthoB = w; }
    else                   { diag = se; orthoA = s; orthoB = e; }

    // How well the two orthogonal flanks agree with the diagonal pixel
    // (i.e. "is this really one continuous diagonal-coloured line"),
    // and how much the centre pixel actually differs from that line -
    // both continuous, so there's no hard per-pixel flip.
    const float simThresh = 0.06;
    float flankAgreement = 1.0 - smoothstep(0.0, simThresh,
                                             max(distance(diag, orthoA), distance(diag, orthoB)));
    float centreIsOdd = smoothstep(0.02, simThresh * 1.5,
                                    min(distance(c, orthoA), distance(c, orthoB)));

    float cornerDist = 1.0 - clamp(max(abs(subpix.x - (q.x ? 1.0 : 0.0)),
                                        abs(subpix.y - (q.y ? 1.0 : 0.0))), 0.0, 1.0);

    float strength = flankAgreement * centreIsOdd * cornerDist * 0.85;
    return mix(c, diag, strength);
}

// Stage 1: xBR-style edge-directed corner blend. Takes the diagonally-
// reconnected colour from Stage 0 as its centre sample so the two
// stages compound instead of Stage 1 overwriting Stage 0's work.
vec3 edgeUpscale(sampler2DArray tex, vec3 uv, vec2 texSize, vec3 center)
{
    vec2 pixelPos = uv.xy * texSize;
    vec2 subpix = fract(pixelPos);

    vec3 c  = center;
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
    // Very high local contrast (thick black outlines against bright
    // colour, which is exactly what NDS anime-style sprites use) is
    // where this used to speckle: as edgeMag grows past a normal edge
    // it now ramps back down instead of staying maxed out, so outlines
    // are left mostly alone while softer diagonals still get smoothed.
    float blendStrength = smoothstep(0.02, 0.09, edgeMag) * cornerDist
                         * (1.0 - smoothstep(0.35, 0.7, edgeMag));

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
    // Cap the amplitude outright - this is what was letting thick
    // black-outline-vs-bright-colour boundaries (the sprite artwork's
    // own line art) get sharpened at near-full strength, which is what
    // produced the speckled/dashed white halo along every character
    // outline. Real detail still gets a solid push; already-hard edges
    // don't get pushed any further.
    // 0.45 -> 0.5: with cleanupPass now tightened below to leave real
    // edges (like these outlines) alone instead of softening them back
    // down, there's a bit more headroom here before the halo comes
    // back - this is the crisper-blacks request from the previous cap.
    ampl = min(ampl, 0.5);

    // sharpness in [0,1] - pulled back down; 0.85 was tuned purely for
    // "does the effect show at all" and turned out too hot once actual
    // sprite line art was on screen.
    const float sharpness = 0.45;
    float peak = -1.0 / mix(8.0, 5.0, sharpness);
    vec3 cw = ampl * peak;

    vec3 rcpWeight = 1.0 / (1.0 + 4.0 * cw);
    vec3 result = (a * cw + b * cw + f * cw + g * cw + e) * rcpWeight;
    return clamp(result, 0.0, 1.0);
}

// Stage 3: selective outlier suppression (final cleanup). After three
// compounding local-contrast stages, a small number of pixels near
// edges can end up as a slight outlier relative to their immediate
// neighbourhood - not a real edge, just leftover noise from the earlier
// stages compounding. This only touches pixels close to their own
// neighbourhood average: genuine edges/detail (a big difference from
// the average) are left completely alone, so this can't flatten real
// artwork into a flat "plastic" look - it only mops up what's left.
vec3 cleanupPass(sampler2DArray tex, vec3 uv, vec3 result)
{
    vec3 n = textureOffset(tex, uv, ivec2( 0, -1)).rgb;
    vec3 s = textureOffset(tex, uv, ivec2( 0,  1)).rgb;
    vec3 w = textureOffset(tex, uv, ivec2(-1,  0)).rgb;
    vec3 e = textureOffset(tex, uv, ivec2( 1,  0)).rgb;
    vec3 avg = (n + s + w + e) * 0.25;

    float diff = distance(result, avg);
    // Shrunk hard from 0.02-0.045 to 0.008-0.02, pull cut from 0.3 to
    // 0.12: DS background art commonly uses deliberate per-pixel
    // dithering to fake extra colour depth (checkerboard-style
    // alternation, not smooth gradients) - the previous window was
    // wide/strong enough to read that dithering as "shader noise" and
    // blur it into a flat smear (visible as one screen looking mushy
    // while a mostly-flat-colour screen next to it looked fine). This
    // narrower window only mops up genuinely tiny differences left by
    // the earlier stages, which is what this pass was meant for in the
    // first place - real dithering sits above it and survives intact.
    float pull = 1.0 - smoothstep(0.008, 0.02, diff);
    return mix(result, avg, pull * 0.12);
}

// Runs the full diagonal-reconnect -> edge-upscale -> CAS -> cleanup
// pipeline for one texcoord. Factored out of main() so it can be
// evaluated at several sub-pixel offsets below instead of once.
vec3 processPixel(vec3 uv, vec2 texSize)
{
    vec3 reconnected = diagonalReconnect(ScreenTex, uv, texSize);
    vec3 edged = edgeUpscale(ScreenTex, uv, texSize, reconnected);
    // CAS runs on top of the edge-blended result, not averaged
    // against a separately-computed raw-source sharpen - that's
    // what makes both stages actually visible together instead of
    // each cancelling half of the other out.
    vec3 sharpened = casSharpen(ScreenTex, uv, edged);
    return cleanupPass(ScreenTex, uv, sharpened);
}

void main()
{
    vec3 rgb;
    if (uSharpUpscale != 0)
    {
        vec2 texSize = vec2(textureSize(ScreenTex, 0).xy);

        // 2x2 supersampling on top of the algorithm above: each of the
        // four stages is itself continuous/smoothstep-based (see their
        // comments), so this isn't masking artifacts, it's averaging
        // out the last bit of aliasing along the diagonal edges the
        // algorithm targets in the first place - this is the actual
        // quality ceiling of a shader-only approach, since the source
        // is a fixed-resolution NDS framebuffer with no extra detail
        // to recover. fwidth() gives the on-screen texel footprint in
        // UV space, so the four taps stay exactly one output-pixel
        // apart regardless of window/upscale size.
        vec2 duv = fwidth(fTexcoord.xy) * 0.25;
        vec3 s0 = processPixel(vec3(fTexcoord.xy + vec2(-duv.x, -duv.y), fTexcoord.z), texSize);
        vec3 s1 = processPixel(vec3(fTexcoord.xy + vec2( duv.x, -duv.y), fTexcoord.z), texSize);
        vec3 s2 = processPixel(vec3(fTexcoord.xy + vec2(-duv.x,  duv.y), fTexcoord.z), texSize);
        vec3 s3 = processPixel(vec3(fTexcoord.xy + vec2( duv.x,  duv.y), fTexcoord.z), texSize);
        rgb = (s0 + s1 + s2 + s3) * 0.25;
    }
    else
        rgb = texture(ScreenTex, fTexcoord).rgb;

    oColor = vec4(rgb, 1.0);
}
)";

#endif // MAIN_SHADERS_H
