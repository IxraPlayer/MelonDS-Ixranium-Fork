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

// Mirrors kColorTolerance (=2, in 0-255 units) in GPU3D_Texcache.h.
const float kColorTol = 2.0;

// Mirrors kTier0/kTier1/kTier2 in GPU3D_Texcache.h.
const float kTier0 = 1.00;
const float kTier1 = 0.70;
const float kTier2 = 0.35;

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

    bool condTL = Close(D, B) && !Close(D, H) && !Close(B, F);
    bool condTR = Close(B, F) && !Close(B, D) && !Close(F, H);
    bool condBL = Close(D, H) && !Close(D, B) && !Close(H, F);
    bool condBR = Close(H, F) && !Close(H, D) && !Close(F, B);

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

    return isActive ? mix(E, neighbor, TierWeight(dist)) : E;
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
    // TextureSharpenAndSaturate in GPU3D_Texcache.h. Already in 0-255
    // units here, so edgeMag needs no *255 the way the 2D (0-1 space)
    // shader does.
    vec4 n = GetUpscaled(outPx + ivec2(0, -1));
    vec4 s = GetUpscaled(outPx + ivec2(0, 1));
    vec4 w = GetUpscaled(outPx + ivec2(-1, 0));
    vec4 e = GetUpscaled(outPx + ivec2(1, 0));
    vec4 avg = (n + s + w + e) * 0.25;

    vec3 diff = centre.rgb - avg.rgb;
    float edgeMag = max(max(abs(diff.r), abs(diff.g)), abs(diff.b));
    float edgeFactor = smoothstep(4.0, 28.0, edgeMag);
    float effStrength = uSharpenStrength * (0.33 + edgeFactor * (1.5 - 0.33));

    vec3 sharpened = clamp(centre.rgb + diff * effStrength, 0.0, 255.0);

    // Saturation boost, luma-preserving - same as
    // TextureSharpenAndSaturate's saturation step.
    float luma = dot(sharpened, vec3(0.299, 0.587, 0.114));
    vec3 saturated = clamp(luma + (sharpened - luma) * uSaturationBoost, 0.0, 255.0);

    oColor = uvec4(uvec3(round(saturated)), uint(centre.a));
}
