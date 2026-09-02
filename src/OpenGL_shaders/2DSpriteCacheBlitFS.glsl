#version 140

// Samples one cached, already-upscaled sprite out of
// SpriteUpscaleCacheArray and writes it into its atlas slot in the 4x
// SpriteUpFB.
//
// Flip is NOT applied here. It used to be ("share one cache entry
// between a sprite's flipped and unflipped instances"), but the final
// screen compositor (2DSpriteVS.glsl's fTexcoord mix, shared by BOTH
// the native 1024x512 atlas path and this 4x ixranium path) ALSO
// applies Flip when it reads back out of whichever atlas ended up in
// SpriteTex/SpriteUpTex. Baking it in again here meant every flipped
// sprite got mirrored twice - which cancels out geometrically (so it
// silently rendered as if never flipped: an arrow meant to face left
// via Flip.x kept facing its canonical right), and because the two
// flips use two different coordinate spaces (this shader's own
// per-sprite fTexcoord/uv vs. 2DSpriteVS's BoundSize-relative mix)
// they don't cancel pixel-exactly at the edges either - producing the
// torn "right half leaking out of the left half" look on top of the
// wrong facing. The native (non-ixranium) atlas never had this
// problem since 2DSpritePreFS.glsl (its equivalent of this shader)
// never touched Flip at all - match that here: write the sprite's
// plain canonical orientation into the atlas cell, exactly like the
// cache layer itself already stores, and let 2DSpriteVS.glsl's single
// flip application (which runs either way) be the only place it
// happens.

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
