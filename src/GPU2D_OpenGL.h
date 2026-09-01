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

#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include "OpenGLSupport.h"
#include "GPU2D.h"

namespace melonDS
{
class GLRenderer;

class GLRenderer2D : public Renderer2D
{
public:
    GLRenderer2D(melonDS::GPU2D& gpu2D, GLRenderer& parent);
    ~GLRenderer2D() override;
    bool Init() override;
    void Reset() override;

    bool InitShaders();
    bool InitShaders(GLRenderer2D& other);
    void DeleteShaders();

    void PostSavestate();

    void SetScaleFactor(int scale);

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;
    void VBlank() override;
    void VBlankEnd() override;

private:
    friend class GLRenderer;
    GLRenderer& Parent;

    int ScaleFactor;
    int ScreenW, ScreenH;

    GLuint LayerPreShader;
    GLint LayerPreCurBGULoc;

    GLuint ScanlineConfigUBO;
    GLuint SpriteScanlineConfigUBO;

    GLuint SpritePreShader;
    GLuint SpritePreVtxBuffer;
    GLuint SpritePreVtxArray;
    u16* SpritePreVtxData;

    GLuint SpriteShader;
    GLint SpriteRenderTransULoc;
    GLuint SpriteVtxBuffer;
    GLuint SpriteVtxArray;
    u16* SpriteVtxData;

    GLuint CompositorShader;
    GLuint CompositorConfigUBO;
    GLint CompositorScaleULoc;

    // base index for a BG layer within the BG texture arrays
    // based on BG type and size
    const u8 BGBaseIndex[4][4] = {
        {2, 10, 6, 14},     // text mode
        {0, 4, 16, 20},     // rotscale
        {0, 4, 12, 16},     // bitmap
        {18, 19, 12, 16},   // large bitmap
    };

    GLuint LayerConfigUBO;
    GLuint SpriteConfigUBO;

    GLuint VRAMTex_BG;
    GLuint VRAMTex_OBJ;
    GLuint PalTex_BG;
    GLuint PalTex_OBJ;

    GLuint MosaicTex;

    GLuint AllBGLayerFB[22];
    GLuint AllBGLayerTex[22];

    GLuint BGLayerFB[4];
    GLuint BGLayerTex[4];

    // "Ixranium Graphics" (2D BG layers - Faz A). A second, 4x-larger
    // pool paralleling AllBGLayerFB/Tex above, one-to-one by index -
    // see BGUpscaleShader / PrerenderLayer in the .cpp. Only actually
    // rendered into and sampled from when
    // melonDS::IxraniumTexUpscaleEnabled is on; otherwise the compositor
    // keeps reading BGLayerTex (native, unmodified) exactly as before.
    GLuint AllBGLayerUpFB[22];
    GLuint AllBGLayerUpTex[22];

    GLuint BGLayerUpFB[4];
    GLuint BGLayerUpTex[4];

    GLuint BGUpscaleShader;
    GLint BGUpscaleSrcSizeULoc;

    // Ixranium sprite cache blit pass (see PrerenderSprites' cache-hit
    // path) - places each cached, already-upscaled sprite into the 4x
    // atlas without re-running BGUpscaleShader over the whole thing.
    GLuint SpriteCacheBlitShader;
    GLint SpriteCacheBlitLayerULoc;
    GLint SpriteCacheBlitSpriteIdxULoc;

    // Renders ONE sprite in isolation into SpriteScratchFB (see
    // GetOrBuildUpscaledSprite) - deliberately separate from
    // SpritePreShader, which hardcodes the shared 1024x512 atlas's
    // grid position/size and would misplace/clip a sprite rendered
    // into a small scratch buffer instead.
    GLuint SpriteScratchShader;

    GLuint SpriteFB;
    GLuint SpriteTex;

    // "Ixranium Graphics" (sprites/OBJ layer). Same 4x-larger pool
    // approach as AllBGLayerUpFB/Tex above, but sprites use a single
    // shared 1024x512 atlas (SpriteTex/SpriteFB) rather than one
    // texture per BG layer, so there's only one "up" pair here - see
    // PrerenderSprites (the upscale pass) and 2DSpriteFS.glsl's
    // uSpriteScale-driven GetSpritePixel (the sampling side) in the
    // .cpp/.glsl.
    GLuint SpriteUpFB;
    GLuint SpriteUpTex;
    GLint SpriteScaleULoc;

    GLuint OBJLayerFB;
    GLuint OBJLayerTex;
    GLuint OBJDepthTex;

    GLuint OutputFB;
    GLuint OutputTex;

    // std140 compliant config struct for the layer shader
    struct sLayerConfig
    {
        u32 uVRAMMask;
        u32 __pad0[3];
        struct sBGConfig
        {
            u32 Size[2];
            u32 Type;
            u32 PalOffset;
            u32 TileOffset;
            u32 MapOffset;
            u32 Clamp;
            u32 __pad0[1];
        } uBGConfig[4];
    } LayerConfig;

    struct sSpriteConfig
    {
        u32 uVRAMMask;
        u32 __pad0[3];
        s32 uRotscale[32][4];
        struct sOAM
        {
            s32 Position[2];
            s32 Flip[2];
            s32 Size[2];
            s32 BoundSize[2];
            u32 OBJMode;
            u32 Type;
            u32 PalOffset;
            u32 TileOffset;
            u32 TileStride;
            u32 Rotscale;
            u32 BGPrio;
            u32 Mosaic;
        } uOAM[128];
    } SpriteConfig;
    int NumSprites;
    bool SpriteUseMosaic;

    // Ixranium: per-sprite upscaled-image cache. Instead of re-upscaling
    // the whole 1024x512 sprite atlas every frame PrerenderSprites()
    // runs, each OAM entry's native sprite image is upscaled ONCE (keyed
    // by everything that affects its pixels) and reused from a GL
    // texture array on every subsequent frame where the same sprite
    // content reappears - the common case for idle/looping animation
    // frames, repeated enemy sprites, UI icons, etc.
    struct SpriteCacheKey
    {
        u32 TileOffset, TileStride, PalOffset;
        s32 SizeX, SizeY, FlipX, FlipY;
        u32 OBJMode, Mosaic, Type;
        u64 ContentHash; // hash of the referenced VRAM tile bytes

        bool operator==(const SpriteCacheKey& o) const
        {
            return TileOffset==o.TileOffset && TileStride==o.TileStride &&
                   PalOffset==o.PalOffset && SizeX==o.SizeX && SizeY==o.SizeY &&
                   FlipX==o.FlipX && FlipY==o.FlipY && OBJMode==o.OBJMode &&
                   Mosaic==o.Mosaic && Type==o.Type && ContentHash==o.ContentHash;
        }
    };
    struct SpriteCacheKeyHash
    {
        size_t operator()(const SpriteCacheKey& k) const noexcept
        {
            // Simple FNV-1a style fold; adequate for a lookup table key,
            // not a cryptographic hash.
            u64 h = 1469598103934665603ull;
            const u32 words[] = {k.TileOffset, k.TileStride, k.PalOffset,
                (u32)k.SizeX, (u32)k.SizeY, (u32)k.FlipX, (u32)k.FlipY,
                k.OBJMode, k.Mosaic, k.Type};
            for (u32 w : words) { h ^= w; h *= 1099511628211ull; }
            h ^= k.ContentHash; h *= 1099511628211ull;
            return (size_t)h;
        }
    };
    struct SpriteCacheEntry
    {
        int ArrayLayer;
        u64 LastUsedFrame;
    };
    std::unordered_map<SpriteCacheKey, SpriteCacheEntry, SpriteCacheKeyHash> SpriteUpscaleCache;
    GLuint SpriteUpscaleCacheArray = 0; // GL_TEXTURE_2D_ARRAY, one 4x-upscaled sprite per layer
    GLuint SpriteUpscaleCacheFB = 0;
    GLuint SpriteScratchTex = 0, SpriteScratchFB = 0;
    std::vector<bool> SpriteCacheLayerFree;
    u64 SpriteCacheFrameCounter = 0;
    static constexpr int kSpriteCacheMaxLayers = 256; // native max sprite is 64x64 -> 256x256 upscaled
    static constexpr int kSpriteCacheLayerDim = 256;  // 64*4, biggest native OBJ size upscaled

    // Computes the cache key for OAM entry i and, on miss, upscales just
    // that sprite's native image into a free cache layer; on hit, reuses
    // the existing layer. Returns the array layer to sample from, or -1
    // if the sprite couldn't be cached (falls back to the old whole-
    // atlas path for that frame as a safety net).
    int GetOrBuildUpscaledSprite(int oamIndex);
    void EvictLRUSpriteCacheEntry();
    // Per-512-byte OBJ VRAM region "generation" counters, bumped when
    // GPU.VRAMDirty_AOBJ/BOBJ (already maintained by the emu core for
    // exactly this kind of invalidation - see GPU3D_Texcache.h's
    // Update() using the sibling VRAMDirty_Texture the same way) mark a
    // region dirty. Cheap CPU-side proxy for "did this sprite's tile
    // data change" - no GPU readback needed, unlike hashing pixels.
    static constexpr u32 kSpriteVRAMRegionSize = 512; // matches VRAMDirtyGranularity in GPU.h
    std::vector<u32> SpriteVRAMGenerationA; // sized 256*1024/512 regions (engine A OBJ)
    std::vector<u32> SpriteVRAMGenerationB; // sized 128*1024/512 regions (engine B OBJ)

    // Bumped whenever this engine's OBJ palette (standard or extended)
    // changes, tracked at the granularity sprites actually key off of
    // (16-color bank / whole-256 standard table / extended-palette
    // slot) so a write to ONE bank/slot doesn't invalidate every
    // cached sprite using every OTHER bank/slot too - that used to
    // cause a full-cache rebuild storm (and same-frame LRU eviction
    // of still-in-use layers) on any per-frame palette animation.
    u16 ObjPalShadow[2][256] = {};   // last-seen standard OBJ palette (u16 BGR555 entries), per engine
    u32 ObjPalBankEpoch[2][16] = {}; // per 16-color bank, for Type 0 sprites
    u32 ObjPalStdEpoch[2] = {};      // whole-256 table, for Type 1 sprites w/ standard palette (PalOffset==0)
    u32 ObjExtPalEpoch[2][16] = {};  // per extended-palette slot, for Type 1 sprites w/ ext palette
    void RefreshSpriteVRAMGenerations(); // call once per PrerenderSprites(), before any cache lookups
    void BumpSpriteVRAMGenerations(const u64* dirtyBits, std::vector<u32>& gen);
    u64 HashSpriteVRAM(u32 tileOffset, u32 tileStride, int sizeX, int sizeY, u32 objMode, u32 type, u32 palOffset, bool engineB) const;
    void UpdateObjPalStdEpoch(int engine);

    struct sScanlineConfig
    {
        struct sScanline
        {
            s32 BGOffset[4][4];     // really [4][2]
            s32 BGRotscale[2][4];
            u32 BackColor;          // 96
            u32 WinRegs;            // 100
            u32 WinMask;            // 104
            u32 __pad0[1];
            s32 WinPos[4];
            u32 BGMosaicEnable[4];
            s32 MosaicSize[4];
        } uScanline[192];
    } ScanlineConfig;

    struct sSpriteScanlineConfig
    {
        s32 uMosaicLine[192];
    } SpriteScanlineConfig;

    struct sCompositorConfig
    {
        u32 uBGPrio[4];
        u32 uEnableOBJ;
        u32 uEnable3D;
        u32 uBlendCnt;
        u32 uBlendEffect;
        u32 uBlendCoef[4];
    } CompositorConfig;

    int LastLine;

    bool UnitEnabled;

    u32 DispCnt;
    u8 LayerEnable;
    u8 OBJEnable;
    u8 ForcedBlank;
    u16 BGCnt[4];
    u16 BlendCnt;
    u8 EVA, EVB, EVY;

    u32 BGVRAMRange[4][4];

    bool LayerConfigDirty;

    int LastSpriteLine;
    u16 OAM[512];

    u32 SpriteDispCnt;
    bool SpriteConfigDirty;
    bool SpriteDirty;

    u16 TempPalBuffer[256 * (1 + (4*16))];

    bool IsScreenOn();

    void UpdateAndRender(int line);

    void UpdateScanlineConfig(int line);
    void UpdateLayerConfig();
    void UpdateOAM(int ystart, int yend);
    void UpdateCompositorConfig();

    void PrerenderSprites();
    void PrerenderLayer(int layer);

    void DoRenderSprites(int line);
    void RenderSprites(bool window, int ystart, int yend);

    void RenderScreen(int ystart, int yend);
};

}
