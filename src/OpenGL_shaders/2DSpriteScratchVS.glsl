#version 140

// Used ONLY by GetOrBuildUpscaledSprite's scratch pass (GPU2D_OpenGL.cpp)
// to render a single sprite in isolation into SpriteScratchFB before
// upscaling it into the cache. Unlike 2DSpritePreVS.glsl - which places
// a sprite at its slot in the shared 1024x512 atlas via a fixed
// (idx&0xF)*64 grid offset and a hardcoded fbsize=(1024,512) - this
// pass renders to a scratch buffer sized to exactly this sprite
// (glViewport(0,0,Size.x,Size.y) in the caller), so the sprite must
// fill the ENTIRE clip space with no atlas offset or fixed fbsize
// involved. Using 2DSpritePreVS.glsl unmodified here was the bug: its
// atlas-grid math computed a position meant for the 1024x512 atlas and
// left it there while the actual framebuffer was 64x64, misplacing
// (or fully clipping) the sprite.
//
// Same FS (2DSpritePreFS.glsl) as the atlas pass - it only needs
// fSpriteIndex + fTexcoord (local pixel coords within the sprite),
// both of which are unaffected by which framebuffer we're targeting.

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

in ivec2 vPosition;
in int vSpriteIndex;

flat out int fSpriteIndex;
smooth out vec2 fTexcoord;

void main()
{
    ivec2 sprsize = uOAM[vSpriteIndex].Size;

    // No atlas offset, no fixed 1024x512 fbsize - vPosition (0/1 unit
    // quad corners) maps straight to clip space, filling whatever
    // framebuffer/viewport the caller bound (SpriteScratchFB, sized
    // exactly sprsize).
    gl_Position = vec4((vec2(vPosition) * 2.0) - 1.0, 0, 1);
    fSpriteIndex = vSpriteIndex;
    fTexcoord = vec2(vPosition) * vec2(sprsize);
}
