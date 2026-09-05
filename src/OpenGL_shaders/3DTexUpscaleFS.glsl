#version 140

// "Ixranium Graphics" for 3D textures, GPU version. Direct integer-
// texture port of the same corner-detection Eagle4x upscale + edge-
// adaptive sharpen + luma-preserving saturation already used (and
// already visually tuned) on the CPU in GPU3D_Texcache.h
// (EagleUpscale4x / TextureSharpenAndSaturate), and structurally
// mirrors the already-working 2DBGUpscaleFS.glsl (2D BG layers, "Faz
// A") - same single-pass "upscale + sample own neighbours + sharpen +
// saturate" trick, just working in raw 0-255 integer texel values
// instead of normalized 0-1 float, because the 3D texture cache's
// array textures are GL_RGBA8UI (integer format), not GL_RGBA like the
// 2D layer textures. Working in 0-255 space also means every constant
// below matches GPU3D_Texcache.h's constants directly with no unit
// conversion, unlike the 2D shader's kColorTol (which had to divide by
// 255 to work in its normalized 0-1 space).
//
// Replaces BOTH EagleUpscale4x and TextureSharpenAndSaturate for a
// cache-miss texture in one GPU pass - profiling showed those two
// steps costing ~2-25ms combined per texture on CPU (the actual FPS
// bottleneck this whole file's parent commit chain was chasing); this
// runs the equivalent per-pixel work across the GPU's full width of
// parallelism instead of a handful of CPU threads.

uniform usampler2D SrcTex;
uniform ivec2 uSrcSize;
uniform float uSharpenStrength;
uniform float uSaturationBoost;

out uvec4 oColor;

// SrcTex actually holds native RGB6A5 data (6-bit RGB, 0-63 per
// channel) uploaded straight from DecodingBuffer with no rescale (see
// GPUUpscaleSharpenSaturate's glTexSubImage2D call in
// GPU3D_TexcacheOpenGL.cpp) - despite this file's header comment
// claiming "0-255 integer texel values". 3DRenderFS.glsl always
// normalizes CurTexture by dividing by (63,63,63,31), for every
// texture regardless of which pipeline produced it, so RGB channels
// written here must stay within 0-63 or they read back out-of-range
// once normalized for rendering - seen as colour distortion right at
// high-contrast edges, where the sharpen delta is largest and most
// likely to push a channel past 63.
const float kChannelMax = 63.0;

// Mirrors kColorTolerance (=3, against the native 0-63 RGB6A5 range)
// in GPU3D_Texcache.h - see that constant's comment for the reasoning.
const float kColorTol = 3.0;

// Mirrors kTier0/kTier1/kTier2 in GPU3D_Texcache.h.
const float kTier0 = 0.75;
const float kTier1 = 0.55;
const float kTier2 = 0.35;

// Mirrors kCornerMinDiff in GPU3D_Texcache.h - same 0-63 native scale
// as this shader's texels, so used directly with no unit conversion.
// See that constant's comment: gates Eagle's corner-fill so a soft AA/
// gradient step can't fire it on its own the way a genuine hard corner
// (two flat regions meeting at an angle) can.
const float kCornerMinDiff = 10.0;

bool StrongDiffLuma(vec4 a, vec4 b)
{
    vec3 lw = vec3(0.299, 0.587, 0.114);
    float la = dot(a.rgb, lw);
    float lb = dot(b.rgb, lw);
    return abs(la - lb) > kCornerMinDiff;
}

vec4 Fetch(ivec2 p)
{
    p = clamp(p, ivec2(0), uSrcSize - ivec2(1));
    return vec4(texelFetch(SrcTex, p, 0));
}

bool Close(vec4 a, vec4 b)
{
    return all(lessThanEqual(abs(a - b), vec4(kColorTol)));
}

float TierWeight(int dist)
{
    if (dist == 0) return kTier0;
    if (dist == 1) return kTier1;
    return kTier2;
}

// Same corner-detection + tiered-blend logic as EagleUpscale4x's
// fillQuadrant in GPU3D_Texcache.h, evaluated for one specific position
// (srcPx, local) instead of writing a whole 4x4 block at once - lets
// both the upscale itself AND the sharpen pass below (which needs its
// four upscaled neighbours, not just its own pixel) call this same
// function for arbitrary output-space positions.
vec4 UpscaledColorAt(ivec2 srcPx, ivec2 local)
{
    vec4 B = Fetch(srcPx + ivec2(0, -1));
    vec4 D = Fetch(srcPx + ivec2(-1, 0));
    vec4 E = Fetch(srcPx);
    vec4 F = Fetch(srcPx + ivec2(1, 0));
    vec4 H = Fetch(srcPx + ivec2(0, 1));

    bool condTL = Close(D, B) && !Close(D, H) && !Close(B, F) && StrongDiffLuma(D, H) && StrongDiffLuma(B, F);
    bool condTR = Close(B, F) && !Close(B, D) && !Close(F, H) && StrongDiffLuma(B, D) && StrongDiffLuma(F, H);
    bool condBL = Close(D, H) && !Close(D, B) && !Close(H, F) && StrongDiffLuma(D, B) && StrongDiffLuma(H, F);
    bool condBR = Close(H, F) && !Close(H, D) && !Close(F, B) && StrongDiffLuma(H, D) && StrongDiffLuma(F, B);

    int qr = local.y / 2;
    int qc = local.x / 2;
    int wr = local.y - qr * 2;
    int wc = local.x - qc * 2;
    int dist = abs(wr - qr) + abs(wc - qc);

    bool isActive;
    vec4 neighbor;
    if (qr == 0 && qc == 0)      { isActive = condTL; neighbor = D; }
    else if (qr == 0 && qc == 1) { isActive = condTR; neighbor = F; }
    else if (qr == 1 && qc == 0) { isActive = condBL; neighbor = D; }
    else                          { isActive = condBR; neighbor = F; }

    if (!isActive) return E;

    vec3 lumaWeights = vec3(0.299, 0.587, 0.114);
    float lumaE = dot(E.rgb, lumaWeights);
    float lumaNeighbor = dot(neighbor.rgb, lumaWeights);
    float blendedLuma = mix(lumaE, lumaNeighbor, TierWeight(dist));
    return vec4(E.rgb + (blendedLuma - lumaE), E.a);
}

vec4 GetUpscaled(ivec2 outPx)
{
    ivec2 srcPx = ivec2(floor(vec2(outPx) / 4.0));
    ivec2 local = outPx - srcPx * 4;
    return UpscaledColorAt(srcPx, local);
}

void main()
{
    ivec2 outPx = ivec2(gl_FragCoord.xy);
    vec4 centre = GetUpscaled(outPx);

    // Adaptive sharpen - same edge-magnitude-scaled unsharp mask as
    // TextureSharpenAndSaturate in GPU3D_Texcache.h, but rescaled from
    // that function's assumed 0-255 range down to this data's actual
    // 0-63 range (4.0/28.0 * 63/255 =~ 1.0/6.9) - see kChannelMax's
    // comment above for why 0-63, not 0-255.
    vec4 n = GetUpscaled(outPx + ivec2(0, -1));
    vec4 s = GetUpscaled(outPx + ivec2(0, 1));
    vec4 w = GetUpscaled(outPx + ivec2(-1, 0));
    vec4 e = GetUpscaled(outPx + ivec2(1, 0));

    // A neighbour on the far side of a transparency boundary may carry
    // a matte/backing colour that was never meant to be seen - discard
    // it in favour of centre's own colour, same fix already applied in
    // 2DBGUpscaleFS.glsl and GPU3D_Texcache.h's CPU path (this GPU path
    // never had it, which is why the CPU-side fix alone didn't help
    // when the hardware/GPU texture cache path is the one actually
    // active).
    if (abs(n.a - centre.a) > kColorTol) n.rgb = centre.rgb;
    if (abs(s.a - centre.a) > kColorTol) s.rgb = centre.rgb;
    if (abs(w.a - centre.a) > kColorTol) w.rgb = centre.rgb;
    if (abs(e.a - centre.a) > kColorTol) e.rgb = centre.rgb;

    vec4 avg = (n + s + w + e) * 0.25;

    // Sharpen in luma only, then apply the SAME delta to every channel
    // - sharpening R/G/B independently let each channel overshoot by a
    // different amount at a high-contrast edge, seen as colour fringing
    // (e.g. green) rather than brightness fringing. Same fix as
    // 2DBGUpscaleFS.glsl / GPU3D_Texcache.h.
    vec3 lumaWeights = vec3(0.299, 0.587, 0.114);
    float lumaCentre = dot(centre.rgb, lumaWeights);
    float lumaAvg = dot(avg.rgb, lumaWeights);
    float lumaDiff = lumaCentre - lumaAvg;

    float edgeMag = abs(lumaDiff);
    float edgeFactor = smoothstep(4.0 * kChannelMax / 255.0, 28.0 * kChannelMax / 255.0, edgeMag);
    float effStrength = uSharpenStrength * (0.33 + edgeFactor * (1.5 - 0.33));

    float lumaN = dot(n.rgb, lumaWeights);
    float lumaS = dot(s.rgb, lumaWeights);
    float lumaW = dot(w.rgb, lumaWeights);
    float lumaE = dot(e.rgb, lumaWeights);
    float lumaLocalMin = min(min(min(lumaCentre, lumaN), min(lumaS, lumaW)), lumaE);
    float lumaLocalMax = max(max(max(lumaCentre, lumaN), max(lumaS, lumaW)), lumaE);

    float lumaSharpened = clamp(lumaCentre + lumaDiff * effStrength, lumaLocalMin, lumaLocalMax);
    vec3 sharpened = clamp(centre.rgb + (lumaSharpened - lumaCentre), 0.0, kChannelMax);

    // Saturation boost, luma-preserving - same as
    // TextureSharpenAndSaturate's saturation step.
    float luma = lumaSharpened;
    vec3 saturated = clamp(luma + (sharpened - luma) * uSaturationBoost, 0.0, kChannelMax);

    oColor = uvec4(uvec3(round(saturated)), uint(centre.a));
}
