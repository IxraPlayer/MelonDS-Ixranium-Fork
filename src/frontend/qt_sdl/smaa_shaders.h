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

#ifndef SMAA_SHADERS_H
#define SMAA_SHADERS_H

// Stage 5 of the "Optimized Graphics" pipeline: a post-process
// edge-antialiasing pass that runs *after* the existing diagonal-
// reconnect -> xBR-upscale -> CAS -> cleanup chain (kScreenFS), on the
// fully composited, already-upscaled window image.
//
// This is a SIMPLIFIED, distance-based SMAA variant, not a bit-exact
// port of the reference SMAA algorithm. Reference SMAA's blend-weight
// stage relies on two precomputed lookup textures (AreaTex/SearchTex)
// generated offline from an exact geometric-coverage integral; we have
// no way to embed or verify that binary data here, and re-deriving the
// integral from memory without a way to compile/visually verify it
// would risk exactly the kind of silent, hard-to-spot error (wrong
// edge weighting, thin-line flicker) this project is explicitly trying
// to avoid. Instead, the blend-weight stage below computes a smooth,
// monotonic falloff from the raw crossing distance found by a bounded
// four-direction search - same three-pass structure and same goal
// (antialias detected edges only, leave everything else untouched),
// but the weighting math is our own, simple, and easy to verify by
// inspection rather than reproduced from a paper we can't test against.
//
// All three passes operate on a single, full-window-sized, axis-aligned
// offscreen colour buffer that already contains the final composited
// image (both screens, in their real on-screen positions/rotations, as
// drawn by the existing screenShaderProgram redirected to an FBO) - so
// none of this code needs to know anything about per-screen transforms,
// rotation, or layout. It just treats the buffer as a normal 2D image.

// Fullscreen-quad vertex shader shared by all three SMAA passes and by
// the final passthrough blit. Expects vPosition already in [0,1] quad
// space (see smaaQuadVertices in Screen.cpp) - deliberately NOT reusing
// kScreenVS/its uTransform, since these passes must never apply a
// per-screen transform: the offscreen buffer they read from and write
// to is already in final, axis-aligned window-pixel space.
const char* kSMAAQuadVS = R"(#version 140

in vec2 vPosition;
smooth out vec2 fTexcoord;

void main()
{
    fTexcoord = vPosition;
    gl_Position = vec4(vPosition * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Pass 1: edge detection. Flags a pixel's left and top edges (standard
// SMAA convention) when the luma step across them exceeds a fixed
// threshold. Output: R = left edge, G = top edge (0 or 1).
const char* kSMAAEdgeFS = R"(#version 140

uniform sampler2D ColorTex;
uniform vec2 uTexelSize; // 1.0 / buffer size, in pixels

smooth in vec2 fTexcoord;
out vec4 oEdges;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main()
{
    float lC = luma(texture(ColorTex, fTexcoord).rgb);
    float lL = luma(texture(ColorTex, fTexcoord - vec2(uTexelSize.x, 0.0)).rgb);
    float lT = luma(texture(ColorTex, fTexcoord - vec2(0.0, uTexelSize.y)).rgb);

    const float threshold = 0.06;
    vec2 edges = step(vec2(threshold), abs(vec2(lC - lL, lC - lT)));

    if (edges.x + edges.y == 0.0)
        discard;

    oEdges = vec4(edges, 0.0, 1.0);
}
)";

// Pass 2: blend weight. For each of the four cardinal directions, walks
// a small fixed number of texels (compile-time-unrolled loop, so this
// stays a bounded, predictable cost) looking for where the edge flagged
// in Pass 1 stops. The resulting distance is turned into a blend weight
// via a plain smoothstep falloff - short crossing distance (a thin,
// sharp edge) gets a strong blend, a long one (a shallow/soft edge,
// already handled by the earlier cleanup pass) gets little to none.
// This is the "our own simple math instead of the reference area
// table" substitution described above.
const char* kSMAABlendFS = R"(#version 140

uniform sampler2D EdgesTex;
uniform vec2 uTexelSize;

smooth in vec2 fTexcoord;
out vec4 oWeights;

const int kMaxSearchSteps = 8;

// Walks in one direction (dir, a single-axis texel step) while the
// corresponding edge flag (channel selects R=vertical-search-for-left-
// edges or G=horizontal-search-for-top-edges) stays set. Returns the
// number of steps taken before the edge stopped (or kMaxSearchSteps if
// it never did within the search window).
float searchDistance(vec2 dir, int channel)
{
    vec2 uv = fTexcoord;
    for (int i = 1; i <= kMaxSearchSteps; i++)
    {
        uv += dir * uTexelSize;
        vec2 e = texture(EdgesTex, uv).rg;
        float flagged = channel == 0 ? e.x : e.y;
        if (flagged < 0.5)
            return float(i);
    }
    return float(kMaxSearchSteps);
}

void main()
{
    vec2 e = texture(EdgesTex, fTexcoord).rg;

    if (e.x + e.y == 0.0)
    {
        oWeights = vec4(0.0);
        return;
    }

    // Distance-based falloff: short crossing -> strong blend, long
    // crossing -> weak blend. Monotonic and clamped, no discontinuities.
    vec4 w = vec4(0.0);

    if (e.x > 0.5) // left edge -> blend along the horizontal (N/S search)
    {
        float dN = searchDistance(vec2(0.0, -1.0), 1);
        float dS = searchDistance(vec2(0.0,  1.0), 1);
        float d  = min(dN, dS);
        float strength = 1.0 - smoothstep(1.0, float(kMaxSearchSteps), d);
        w.x = strength * (dN <= dS ? 1.0 : 0.0);
        w.y = strength * (dN >  dS ? 1.0 : 0.0);
    }

    if (e.y > 0.5) // top edge -> blend along the vertical (E/W search)
    {
        float dE = searchDistance(vec2( 1.0, 0.0), 0);
        float dW = searchDistance(vec2(-1.0, 0.0), 0);
        float d  = min(dE, dW);
        float strength = 1.0 - smoothstep(1.0, float(kMaxSearchSteps), d);
        w.z = strength * (dE <= dW ? 1.0 : 0.0);
        w.w = strength * (dE >  dW ? 1.0 : 0.0);
    }

    oWeights = w;
}
)";

// Pass 3: neighbourhood blend. Uses the four directional weights from
// Pass 2 to mix the centre pixel with its N/S/E/W neighbours - a plain,
// energy-preserving weighted average (weights are already clamped to
// [0,1] and this normalises their sum), never sharpening or ringing.
const char* kSMAANeighborFS = R"(#version 140

uniform sampler2D ColorTex;
uniform sampler2D WeightsTex;
uniform vec2 uTexelSize;

smooth in vec2 fTexcoord;
out vec4 oColor;

void main()
{
    vec4 w = texture(WeightsTex, fTexcoord);
    vec3 c = texture(ColorTex, fTexcoord).rgb;

    float wSum = w.x + w.y + w.z + w.w;
    if (wSum <= 0.0001)
    {
        oColor = vec4(c, 1.0);
        return;
    }

    vec3 n = texture(ColorTex, fTexcoord + vec2(0.0, -uTexelSize.y)).rgb;
    vec3 s = texture(ColorTex, fTexcoord + vec2(0.0,  uTexelSize.y)).rgb;
    vec3 e = texture(ColorTex, fTexcoord + vec2( uTexelSize.x, 0.0)).rgb;
    vec3 wst = texture(ColorTex, fTexcoord + vec2(-uTexelSize.x, 0.0)).rgb;

    // Normalise so the centre pixel's own contribution fills whatever
    // weight the four neighbours don't use - keeps this a true blend
    // (never over- or under-saturating) regardless of how many
    // directions are active at once.
    vec3 blended = c * (1.0 - min(wSum, 1.0))
                 + n * w.x + s * w.y + e * w.z + wst * w.w;

    oColor = vec4(blended, 1.0);
}
)";

// Trivial 1:1 copy used for the final blit from the working buffer back
// to whichever target the caller bound (default framebuffer or an
// intermediate) - no transform, no filtering decisions, just a copy.
const char* kSMAAPassthroughFS = R"(#version 140

uniform sampler2D ColorTex;

smooth in vec2 fTexcoord;
out vec4 oColor;

void main()
{
    oColor = texture(ColorTex, fTexcoord);
}
)";

#endif // SMAA_SHADERS_H
