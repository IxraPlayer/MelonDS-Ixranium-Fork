#version 140

// "Ixranium Graphics" for 2D BG layers (Faz A). Runs once per frame,
// after PrerenderLayer's normal (unmodified) decode pass, reading that
// pass's raw output (SrcTex) and writing a 4x-larger, upscaled +
// sharpened + saturated version that the compositor samples from
// instead - never touches the final composited screen image itself
// (blend/mosaic/window effects still happen afterwards, in the
// compositor, exactly as before).
//
// This is a direct GLSL port of the same logic already used (and
// already tuned against real gameplay) for 3D textures in
// GPU3D_Texcache.h - EagleUpscale4x, TextureSharpen, and
// ApplySaturationBoost. Kept in sync with that file's tier/threshold/
// strength constants by hand (GLSL can't #include a C++ header), so if
// those get retuned there, mirror the same numbers here.
//
// Unlike the 3D path (decoded once, cached for many frames), 2D BG
// content can change every frame, so this runs every frame a layer is
// active - each output pixel costs 5 self-contained lookups (centre +
// 4 neighbours, each internally sampling 5 source texels), all fully
// parallel on the GPU. For typical BG sizes (256x256-512x512) this is
// inexpensive; if a game uses the largest 1024x1024 BG layers, this is
// the thing to watch on a slower GPU.

uniform sampler2D SrcTex;
uniform ivec2 uSrcSize;

out vec4 oColor;

// Mirrors kColorTolerance (=2, in 0-255 units) in GPU3D_Texcache.h.
const float kColorTol = 2.0 / 255.0;

// Mirrors kTier0/kTier1/kTier2 in GPU3D_Texcache.h.
const float kTier0 = 1.00;
const float kTier1 = 0.70;
const float kTier2 = 0.35;

// Mirrors kSharpenStrength / the 4.0-28.0 edge-magnitude range / the
// 0.33-1.5 strength-multiplier range in TextureSharpen (GPU3D_Texcache.h).
const float kSharpenStrength = 0.3;

// Mirrors kSaturationBoost in GPU3D_Texcache.h.
const float kSaturationBoost = 1.05;

vec4 Fetch(ivec2 p)
{
    p = clamp(p, ivec2(0), uSrcSize - ivec2(1));
    return texelFetch(SrcTex, p, 0);
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
// (srcPx, local) instead of writing a whole 4x4 block at once - this
// lets both the main upscale AND the sharpen pass below call the exact
// same function for arbitrary positions (including ones that land in a
// neighbouring source pixel's block), with no separate texture or pass
// needed for the sharpening step.
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

    int qr = local.y / 2; // which quadrant: 0 = top, 1 = bottom
    int qc = local.x / 2; // which quadrant: 0 = left, 1 = right
    int wr = local.y - qr * 2; // row within that quadrant (0 or 1)
    int wc = local.x - qc * 2; // col within that quadrant (0 or 1)
    int dist = abs(wr - qr) + abs(wc - qc);

    bool isActive;
    vec4 neighbor;
    if (qr == 0 && qc == 0)      { isActive = condTL; neighbor = D; }
    else if (qr == 0 && qc == 1) { isActive = condTR; neighbor = F; }
    else if (qr == 1 && qc == 0) { isActive = condBL; neighbor = D; }
    else                          { isActive = condBR; neighbor = F; }

    return isActive ? mix(E, neighbor, TierWeight(dist)) : E;
}

// Looks up the upscaled colour at an arbitrary point in OUTPUT (4x)
// space, resolving which source pixel's block it falls into - used for
// both this fragment's own colour and its four neighbours below.
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
    // TextureSharpen in GPU3D_Texcache.h.
    vec4 n = GetUpscaled(outPx + ivec2(0, -1));
    vec4 s = GetUpscaled(outPx + ivec2(0, 1));
    vec4 w = GetUpscaled(outPx + ivec2(-1, 0));
    vec4 e = GetUpscaled(outPx + ivec2(1, 0));
    vec4 avg = (n + s + w + e) * 0.25;

    vec3 diff = centre.rgb - avg.rgb;
    float edgeMag = max(max(abs(diff.r), abs(diff.g)), abs(diff.b)) * 255.0;
    float edgeFactor = smoothstep(4.0, 28.0, edgeMag);
    float effStrength = kSharpenStrength * (0.33 + edgeFactor * (1.5 - 0.33));

    vec3 sharpened = clamp(centre.rgb + diff * effStrength, 0.0, 1.0);

    // Saturation boost, luma-preserving - same as ApplySaturationBoost.
    float luma = dot(sharpened, vec3(0.299, 0.587, 0.114));
    vec3 saturated = clamp(luma + (sharpened - luma) * kSaturationBoost, 0.0, 1.0);

    oColor = vec4(saturated, centre.a);
}
