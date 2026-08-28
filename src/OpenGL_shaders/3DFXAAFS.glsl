#version 140

// Lightweight console-style FXAA. Runs as a single full-screen pass
// over the already-composited 3D colour buffer at native resolution,
// so it's O(pixels) - the whole point is giving 3D polygon edges some
// antialiasing without paying the O(pixels^2) cost of supersampling
// via GL_ScaleFactor (see Window.cpp - Optimized Graphics is forced
// to stay at 1x internal resolution, this pass is what covers 3D edge
// smoothing instead).

uniform sampler2D ColorBuffer;

in vec2 fTexcoord;
out vec4 oColor;

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main()
{
    ivec2 texSize = textureSize(ColorBuffer, 0);
    vec2 texel = 1.0 / vec2(texSize);

    vec3 rgbCenter = texture(ColorBuffer, fTexcoord).rgb;

    vec3 rgbN = texture(ColorBuffer, fTexcoord + vec2( 0.0, -texel.y)).rgb;
    vec3 rgbS = texture(ColorBuffer, fTexcoord + vec2( 0.0,  texel.y)).rgb;
    vec3 rgbE = texture(ColorBuffer, fTexcoord + vec2( texel.x,  0.0)).rgb;
    vec3 rgbW = texture(ColorBuffer, fTexcoord + vec2(-texel.x,  0.0)).rgb;
    vec3 rgbNW = texture(ColorBuffer, fTexcoord + vec2(-texel.x, -texel.y)).rgb;
    vec3 rgbNE = texture(ColorBuffer, fTexcoord + vec2( texel.x, -texel.y)).rgb;
    vec3 rgbSW = texture(ColorBuffer, fTexcoord + vec2(-texel.x,  texel.y)).rgb;
    vec3 rgbSE = texture(ColorBuffer, fTexcoord + vec2( texel.x,  texel.y)).rgb;

    float lC = luma(rgbCenter);
    float lN = luma(rgbN), lS = luma(rgbS), lE = luma(rgbE), lW = luma(rgbW);
    float lNW = luma(rgbNW), lNE = luma(rgbNE), lSW = luma(rgbSW), lSE = luma(rgbSE);

    float lMin = min(lC, min(min(lN, lS), min(lE, lW)));
    float lMax = max(lC, max(max(lN, lS), max(lE, lW)));
    float range = lMax - lMin;

    // Skip flat areas outright (most of a sprite-heavy DS frame) - only
    // pixels near a real contrast edge pay for the blend below.
    const float edgeThresholdMin = 0.03;
    const float edgeThresholdMax = 0.125;
    float threshold = max(edgeThresholdMin, lMax * edgeThresholdMax);
    if (range < threshold)
    {
        oColor = texture(ColorBuffer, fTexcoord);
        return;
    }

    // Estimate the local edge direction from the diagonal gradients and
    // blend a couple of texels along the perpendicular - this is the
    // classic FXAA "edge search lite" simplification, cheap enough for
    // a single full-screen pass every frame.
    float edgeVert = abs(lNW + lSW - 2.0*lW) + 2.0*abs(lN + lS - 2.0*lC) + abs(lNE + lSE - 2.0*lE);
    float edgeHorz = abs(lNW + lNE - 2.0*lN) + 2.0*abs(lW + lE - 2.0*lC) + abs(lSW + lSE - 2.0*lS);
    bool isHorizontal = edgeHorz >= edgeVert;

    vec2 dir = isHorizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec3 rgbA = texture(ColorBuffer, fTexcoord + dir).rgb;
    vec3 rgbB = texture(ColorBuffer, fTexcoord - dir).rgb;

    // Blend strength scales with how far past the threshold this pixel
    // is - subtle edges get a light touch, hard edges get smoothed more.
    float blend = clamp((range - threshold) / max(range, 1e-4), 0.0, 0.75);
    vec3 result = mix(rgbCenter, (rgbA + rgbB) * 0.5, blend);

    // Alpha is not colour data here - it's the 3D layer's per-pixel
    // "is there anything here" mask that the 2D compositor uses to
    // decide whether to show this layer or the 2D graphics underneath.
    // The early-return branch above already passes it through
    // untouched via rgbCenter's source texel; this branch must too,
    // instead of hardcoding it to 1.0 (opaque) - forcing every pixel
    // opaque here is what was corrupting scenes with no 3D content in
    // them at all (this pass runs every frame regardless).
    float a = texture(ColorBuffer, fTexcoord).a;
    oColor = vec4(result, a);
}
