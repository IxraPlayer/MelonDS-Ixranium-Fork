#version 140

// Samples one cached, already-upscaled sprite out of
// SpriteUpscaleCacheArray and writes it into its atlas slot in the 4x
// SpriteUpFB. Flip is applied here (not baked into the cached image)
// so a sprite's flipped and unflipped orientations share one cache
// entry.

struct sOAM
{
    ivec2 Position;
    bvec2 Flip;
    ivec2 Size;
    ivec2 BoundSize;
    int OBJMode;
    int Type;
    int PalOffset;
    int TileOffset;
    int TileStride;
    int Rotscale;
    int BGPrio;
    bool Mosaic;
};

layout(std140) uniform ubSpriteConfig
{
    int uVRAMMask;
    ivec4 uRotscale[32];
    sOAM uOAM[128];
};

uniform sampler2DArray uCacheArray;
uniform int uLayer;
uniform int uSpriteIdx;

smooth in vec2 fTexcoord; // 0..1 across this sprite's own quad

out vec4 oColor;

void main()
{
    vec2 uv = fTexcoord;
    if (uOAM[uSpriteIdx].Flip.x) uv.x = 1.0 - uv.x;
    if (uOAM[uSpriteIdx].Flip.y) uv.y = 1.0 - uv.y;

    // Cache layers are a fixed 256x256; a sprite smaller than 64x64
    // native (most of them) only occupies the bottom-left fraction of
    // its layer (see GetOrBuildUpscaledSprite's glViewport sized to
    // the sprite's own upscaled dimensions) - the rest is whatever
    // was in that layer's texels before (uninitialized on first use,
    // or a previous sprite's leftovers after eviction/reuse).
    //
    // texture() + CLAMP_TO_EDGE is NOT safe here: at uv exactly on the
    // used/unused boundary, float rounding can round the NEAREST pick
    // one texel past the last valid one, sampling that garbage texel -
    // a thin (often 1px) corrupted edge, worst on sprites whose
    // footprint changes often (rotoscale), since that's when this
    // boundary case gets hit most. Same reasoning 2DSpriteFS.glsl
    // already uses texelFetch for its atlas reads - do the same here:
    // fetch by explicit integer texel, clamped to the region we
    // actually wrote, so there's no boundary to round across.
    ivec2 usedSize = uOAM[uSpriteIdx].Size * 4;
    ivec2 texel = ivec2(uv * vec2(usedSize));
    texel = clamp(texel, ivec2(0), usedSize - ivec2(1));
    oColor = texelFetch(uCacheArray, ivec3(texel, uLayer), 0);
}
