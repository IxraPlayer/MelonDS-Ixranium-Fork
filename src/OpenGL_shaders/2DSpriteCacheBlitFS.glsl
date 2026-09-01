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
    // the sprite's own upscaled dimensions) - rescale uv to that
    // fraction instead of assuming it fills the whole layer.
    vec2 usedFraction = vec2(uOAM[uSpriteIdx].Size) * 4.0 / 256.0;
    oColor = texture(uCacheArray, vec3(uv * usedFraction, float(uLayer)));
}
