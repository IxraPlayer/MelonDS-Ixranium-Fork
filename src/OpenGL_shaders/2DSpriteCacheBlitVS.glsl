#version 140

// Ixranium sprite cache: places ONE already-upscaled sprite (sampled
// from SpriteUpscaleCacheArray by GetOrBuildUpscaledSprite's cached
// layer) at its atlas slot in the 4x SpriteUpFB, instead of re-running
// the BGUpscaleShader pass over the whole atlas. Position/size logic
// mirrors 2DSpritePreVS.glsl exactly, just scaled 4x and targeting a
// single quad per draw call (see PrerenderSprites' cache-hit path).

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

uniform int uSpriteIdx;

in vec2 vPosition; // unit quad corners, 0..1 (same RectVtxArray used elsewhere)

smooth out vec2 fTexcoord;

void main()
{
    // Same 16-per-row, 64px-cell atlas layout as PrerenderSprites'
    // native pass, just at 4x scale (256px cells in the 4096x2048
    // upscaled atlas).
    ivec2 sprpos4x = ivec2((uSpriteIdx & 0xF) * 256, (uSpriteIdx >> 4) * 256);
    ivec2 sprsize4x = uOAM[uSpriteIdx].Size * 4;

    vec2 vtxpos = vec2(sprpos4x) + (vPosition * vec2(sprsize4x));
    vec2 fbsize = vec2(1024 * 4, 512 * 4);

    gl_Position = vec4(((vtxpos * 2) / fbsize) - 1, 0, 1);
    fTexcoord = vPosition;
}
