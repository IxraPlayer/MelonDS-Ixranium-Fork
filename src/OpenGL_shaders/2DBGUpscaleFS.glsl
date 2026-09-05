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

// When SrcTex is a single contiguous image (a BG layer), (0,0) - Fetch
// just clamps to uSrcSize as before. When SrcTex is actually a GRID of
// independent images packed into one texture (the sprite atlas: 16
// unrelated 64x64 sprites per row), set this to the cell size (64,64).
// Without it, a neighbour read near a sprite's edge can cross into the
// NEXT sprite's cell - a completely unrelated texture - and get treated
// as real image content by the edge-detect/sharpen below. Right where
// that neighbour's colour is very different from the sprite's own (e.g.
// a white sprite edge next to a dark neighbouring cell), the unsharp
// mask reads a huge false edge and overshoots hard: black/coloured
// speckles hugging the sprite's border that visually look like another
// texture bleeding in - because it is.
uniform ivec2 uCellSize;

out vec4 oColor;

// Mirrors kColorTolerance (=12, in 0-255 units) in GPU3D_Texcache.h -
// see that constant's comment for why 12, not the original 2.
const float kColorTol = 12.0 / 255.0;

// Mirrors kTier0/kTier1/kTier2 in GPU3D_Texcache.h.
const float kTier0 = 0.75;
const float kTier1 = 0.65;
const float kTier2 = 0.45;

// Mirrors kCornerMinDiff in GPU3D_Texcache.h (10 on the 0-63 native
// scale -> ~10/63 here, in this shader's normalized 0-1 space). See
// that constant's comment: gates Eagle's corner-fill so a soft AA/
// gradient step (outlined text, shaded sprites) can't fire it on its
// own the way a genuine hard corner can.
const float kCornerMinDiff = 10.0 / 63.0;

bool StrongDiffLuma(vec4 a, vec4 b)
{
    vec3 lw = vec3(0.299, 0.587, 0.114);
    float la = dot(a.rgb, lw);
    float lb = dot(b.rgb, lw);
    return abs(la - lb) > kCornerMinDiff;
}

// Mirrors kSharpenStrength / the 4.0-28.0 edge-magnitude range / the
// 0.33-1.5 strength-multiplier range in TextureSharpen (GPU3D_Texcache.h).
const float kSharpenStrength = 0.2;

// Mirrors kSaturationBoost in GPU3D_Texcache.h.
const float kSaturationBoost = 1.05;

vec4 Fetch(ivec2 p, ivec2 cellMin, ivec2 cellMax)
{
    p = clamp(p, cellMin, cellMax);
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
    // Cell that srcPx itself belongs to - every neighbour below is
    // clamped into THIS cell (not just the overall texture), so a read
    // that would otherwise cross into an adjacent, unrelated sprite's
    // cell instead repeats srcPx's own edge texel, same as clamp-to-edge
    // would do at a real texture boundary.
    ivec2 cellMin = ivec2(0);
    ivec2 cellMax = uSrcSize - ivec2(1);
    if (uCellSize.x > 0)
    {
        cellMin = (srcPx / uCellSize) * uCellSize;
        cellMax = min(cellMin + uCellSize - ivec2(1), uSrcSize - ivec2(1));
    }

    vec4 B = Fetch(srcPx + ivec2(0, -1), cellMin, cellMax);
    vec4 D = Fetch(srcPx + ivec2(-1, 0), cellMin, cellMax);
    vec4 E = Fetch(srcPx, cellMin, cellMax);
    vec4 F = Fetch(srcPx + ivec2(1, 0), cellMin, cellMax);
    vec4 H = Fetch(srcPx + ivec2(0, 1), cellMin, cellMax);

    bool condTL = Close(D, B) && !Close(D, H) && !Close(B, F) && StrongDiffLuma(D, H) && StrongDiffLuma(B, F);
    bool condTR = Close(B, F) && !Close(B, D) && !Close(F, H) && StrongDiffLuma(B, D) && StrongDiffLuma(F, H);
    bool condBL = Close(D, H) && !Close(D, B) && !Close(H, F) && StrongDiffLuma(D, B) && StrongDiffLuma(H, F);
    bool condBR = Close(H, F) && !Close(H, D) && !Close(F, B) && StrongDiffLuma(H, D) && StrongDiffLuma(F, B);

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

    if (!isActive) return E;

    vec3 lumaWeights = vec3(0.299, 0.587, 0.114);
    float lumaE = dot(E.rgb, lumaWeights);
    float lumaNeighbor = dot(neighbor.rgb, lumaWeights);
    float blendedLuma = mix(lumaE, lumaNeighbor, TierWeight(dist));
    return vec4(E.rgb + (blendedLuma - lumaE), E.a);
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

    // A neighbour on the far side of a transparency boundary (a
    // sprite's silhouette edge, or the unused/cleared part of its
    // atlas cell) has whatever RGB happened to be written there
    // alongside alpha=0 - that colour is meaningless (never actually
    // drawn) but still gets counted as a real neighbour by the diff
    // below, registering as a huge false edge right at the sprite's
    // outline. The unsharp mask then overshoots there, which shows up
    // as colour fringing (often black or green, since R/G/B overshoot
    // by different amounts) hugging moving sprites - worse the higher
    // the tier weights/sharpen strength are pushed. Fix: a neighbour
    // whose alpha doesn't match centre's isn't a real colour edge for
    // this purpose, so fall back to centre's own colour for it instead
    // of letting its arbitrary RGB feed the diff.
    if (abs(n.a - centre.a) > kColorTol) n.rgb = centre.rgb;
    if (abs(s.a - centre.a) > kColorTol) s.rgb = centre.rgb;
    if (abs(w.a - centre.a) > kColorTol) w.rgb = centre.rgb;
    if (abs(e.a - centre.a) > kColorTol) e.rgb = centre.rgb;

    vec4 avg = (n + s + w + e) * 0.25;

    // Sharpen in luma only, then apply the SAME delta to every channel.
    // Sharpening R/G/B independently (the previous approach) lets each
    // channel overshoot by a different amount at a high-contrast edge
    // (thick black outlines against saturated colour, exactly this
    // game's art style) - the three channels drift apart from each
    // other right at the edge, which isn't seen as "too bright/dark"
    // but as an actual colour shift (commonly green, since G carries
    // the most luma weight and so has the most room to overshoot). A
    // uniform luma delta moves all channels together, so the edge can
    // still sharpen but can't change hue while doing it.
    vec3 lumaWeights = vec3(0.299, 0.587, 0.114);
    float lumaCentre = dot(centre.rgb, lumaWeights);
    float lumaAvg = dot(avg.rgb, lumaWeights);
    float lumaDiff = lumaCentre - lumaAvg;

    float edgeMag = abs(lumaDiff) * 255.0;
    float edgeFactor = smoothstep(4.0, 28.0, edgeMag);
    float effStrength = kSharpenStrength * (0.33 + edgeFactor * (1.5 - 0.33));

    float lumaN = dot(n.rgb, lumaWeights);
    float lumaS = dot(s.rgb, lumaWeights);
    float lumaW = dot(w.rgb, lumaWeights);
    float lumaE = dot(e.rgb, lumaWeights);
    float lumaLocalMin = min(min(min(lumaCentre, lumaN), min(lumaS, lumaW)), lumaE);
    float lumaLocalMax = max(max(max(lumaCentre, lumaN), max(lumaS, lumaW)), lumaE);

    float lumaSharpened = clamp(lumaCentre + lumaDiff * effStrength, lumaLocalMin, lumaLocalMax);
    vec3 sharpened = centre.rgb + (lumaSharpened - lumaCentre);

    // Saturation boost, luma-preserving - same as ApplySaturationBoost.
    float luma = lumaSharpened;
    vec3 saturated = clamp(luma + (sharpened - luma) * kSaturationBoost, 0.0, 1.0);

    oColor = vec4(saturated, centre.a);
}
