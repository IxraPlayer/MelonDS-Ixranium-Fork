#ifndef GPU3D_TEXCACHE
#define GPU3D_TEXCACHE

#include "types.h"
#include "GPU.h"

#include <algorithm>
#include <assert.h>
#include <atomic>
#include <cstdlib>
#include <unordered_map>
#include <vector>

#define XXH_STATIC_LINKING_ONLY
#include "xxhash/xxhash.h"

namespace melonDS
{

// "Ixranium Graphics": toggled from the UI (see Window.cpp's hamburger
// menu). Deliberately scoped to ONLY this texture-decode step - it
// changes the pixel data that gets uploaded as a game's textures,
// nothing about how the already-rendered 3D scene or the final screen
// image gets processed afterwards (no post-process pass exists here at
// all), which is what keeps this from affecting perceived depth/3D the
// way a screen-space filter can.
//
// A plain global rather than a per-renderer member: Texcache is a
// header-only template instantiated separately for the OpenGL and
// Compute renderers (see GPU3D_TexcacheOpenGL.h / GPU3D_Compute.h), and
// this setting should apply identically to both without duplicating a
// setter through each one.
//
// Toggling this at runtime changes the *physical pixel dimensions* of
// every texture uploaded afterwards, so anything already cached at the
// old dimensions must not be mixed with newly-uploaded ones under the
// same size bucket (see TexArrays in Texcache below) - Window.cpp's
// toggle handler forces a full renderer reinit when this changes,
// which recreates the Texcache (and therefore this) from empty.
inline std::atomic<bool> IxraniumTexUpscaleEnabled{false};

// Small-tolerance colour comparison used by the upscale equality tests
// below instead of a strict ==. Compares each of the four packed 8-bit
// channels independently and allows them to differ by up to
// `tolerance` - kept genuinely small (see kColorTolerance) specifically
// so it only smooths over minor rounding/precision noise between two
// pixels a person would call "the same colour", not DS titles' own
// intentional dithering (which relies on genuinely different palette
// entries placed next to each other, a much bigger gap than this).
inline bool ColorsClose(u32 a, u32 b, u32 tolerance)
{
    if (tolerance == 0)
        return a == b;

    for (int shift = 0; shift < 32; shift += 8)
    {
        int ca = (int)((a >> shift) & 0xFF);
        int cb = (int)((b >> shift) & 0xFF);
        if (std::abs(ca - cb) > (int)tolerance)
            return false;
    }
    return true;
}

// How much channel-by-channel slack ColorsClose() allows, in the same
// 0-255 units the packed texel channels are stored in. Deliberately
// tiny - big enough to catch a pixel that's "basically" the same
// colour, nowhere near big enough to catch two genuinely different
// palette entries placed next to each other for dithering. Change this
// single constant to adjust; 0 falls back to the exact-match behaviour
// this project started with.
inline constexpr u32 kColorTolerance = 2;

// Strength of the post-upscale sharpening pass (TextureSharpen), in the
// same 0-1 range CAS used for the earlier (now-removed) screen-space
// sharpener - kept at a similarly moderate level for the same reason:
// enough to make edges/text read as crisper, not enough to grow a
// visible halo around them.
inline constexpr float kSharpenStrength = 0.3f;

// Scale3x (a.k.a. the 3x extension of the Eagle/Scale2x family): same
// copy-only, equality-test-based approach as EagleUpscale2x below - no
// colour blending anywhere, so it still can't introduce a colour that
// wasn't already in the original texture. Produces a 3x3 output block
// per source pixel instead of 2x2; the middle row/column additionally
// gets to inherit a neighbour's colour (not just the four corners),
// which is what catches a few more diagonal edges than the 2x version.
inline void EagleUpscale3x(const u32* src, u32 srcW, u32 srcH, u32* dst)
{
    u32 dstW = srcW * 3;

    auto at = [&](u32 x, u32 y) -> u32
    {
        if (x >= srcW) x = srcW - 1;
        if (y >= srcH) y = srcH - 1;
        return src[y * srcW + x];
    };
    auto Close = [](u32 a, u32 b) { return ColorsClose(a, b, kColorTolerance); };

    for (u32 y = 0; y < srcH; y++)
    {
        for (u32 x = 0; x < srcW; x++)
        {
            u32 A = at(x-1, y-1), B = at(x, y-1), C = at(x+1, y-1);
            u32 D = at(x-1, y),   E = at(x, y),   F = at(x+1, y);
            u32 G = at(x-1, y+1), H = at(x, y+1), I = at(x+1, y+1);

            u32 E0 = (Close(D,B) && !Close(D,H) && !Close(B,F)) ? D : E;
            u32 E1 = ((Close(D,B) && !Close(D,H) && !Close(B,F) && !Close(E,C)) || (Close(B,F) && !Close(B,D) && !Close(F,H) && !Close(E,A))) ? B : E;
            u32 E2 = (Close(B,F) && !Close(B,D) && !Close(F,H)) ? F : E;
            u32 E3 = ((Close(D,B) && !Close(D,H) && !Close(B,F) && !Close(E,G)) || (Close(D,H) && !Close(D,B) && !Close(H,F) && !Close(E,A))) ? D : E;
            u32 E4 = E;
            u32 E5 = ((Close(B,F) && !Close(B,D) && !Close(F,H) && !Close(E,I)) || (Close(F,H) && !Close(F,B) && !Close(H,D) && !Close(E,C))) ? F : E;
            u32 E6 = (Close(D,H) && !Close(D,B) && !Close(H,F)) ? D : E;
            u32 E7 = ((Close(D,H) && !Close(D,B) && !Close(H,F) && !Close(E,I)) || (Close(H,F) && !Close(H,D) && !Close(F,B) && !Close(E,G))) ? H : E;
            u32 E8 = (Close(H,F) && !Close(H,D) && !Close(F,B)) ? F : E;

            u32* row0 = dst + (y*3+0) * dstW + (x*3);
            u32* row1 = dst + (y*3+1) * dstW + (x*3);
            u32* row2 = dst + (y*3+2) * dstW + (x*3);
            row0[0] = E0; row0[1] = E1; row0[2] = E2;
            row1[0] = E3; row1[1] = E4; row1[2] = E5;
            row2[0] = E6; row2[1] = E7; row2[2] = E8;
        }
    }
}

// Classic "Eagle" 2x upscale: for each source pixel, each of the four
// output sub-pixels either copies the centre pixel or one diagonal
// neighbour, chosen by simple equality tests - never blends/averages
// colour values. That's deliberate: this only ever copies pixel values
// that already exist in the source texture, so it can't introduce a
// colour that wasn't already somewhere in the original artwork, and it
// works on the raw packed RGBA8 texel (u32) directly - no need to
// unpack channels, so it's agnostic to whichever of RGB6A5/RGBA8/BGRA8
// the decode step above produced.
inline void EagleUpscale2x(const u32* src, u32 srcW, u32 srcH, u32* dst)
{
    u32 dstW = srcW * 2;

    auto at = [&](u32 x, u32 y) -> u32
    {
        // Clamp to edge - there is no pixel outside the texture, and
        // clamping (rather than wrapping) matches how the texture's own
        // edges already look, so it never invents a fake seam.
        if (x >= srcW) x = srcW - 1;
        if (y >= srcH) y = srcH - 1;
        return src[y * srcW + x];
    };
    auto Close = [](u32 a, u32 b) { return ColorsClose(a, b, kColorTolerance); };

    for (u32 y = 0; y < srcH; y++)
    {
        for (u32 x = 0; x < srcW; x++)
        {
            u32 A = at(x-1, y-1), B = at(x, y-1), C = at(x+1, y-1);
            u32 D = at(x-1, y),   E = at(x, y),   F = at(x+1, y);
            u32 G = at(x-1, y+1), H = at(x, y+1), I = at(x+1, y+1);
            (void)A; (void)C; (void)G; (void)I; // unused corners of the 3x3 window, kept for clarity of the pattern above

            u32 topLeft     = (Close(D,B) && !Close(D,H) && !Close(B,F)) ? D : E;
            u32 topRight    = (Close(B,F) && !Close(B,D) && !Close(F,H)) ? F : E;
            u32 bottomLeft  = (Close(H,D) && !Close(H,F) && !Close(D,B)) ? D : E;
            u32 bottomRight = (Close(F,H) && !Close(F,B) && !Close(H,D)) ? H : E;

            u32* out = dst + (y*2) * dstW + (x*2);
            out[0] = topLeft;
            out[1] = topRight;
            out[dstW + 0] = bottomLeft;
            out[dstW + 1] = bottomRight;
        }
    }
}

// Texture-space sharpening, applied AFTER the upscale above (never
// before it - sharpening first would exaggerate small contrast
// differences into what looks like a hard edge, and the upscale pass
// would then "helpfully" treat that as a real diagonal to merge,
// compounding the two into artifacts neither pass would cause alone).
//
// Plain unsharp-mask: for each pixel, compare it to the average of its
// four orthogonal neighbours and push it further in whatever direction
// it already differs - brightens the light side of an edge and darkens
// the dark side, which is what makes text/line-art edges read as
// crisper without changing anything about flat, already-uniform areas
// (their neighbour average equals themselves, so the push there is
// zero). Alpha is left untouched - sharpening transparency edges only
// risks visible fringing on cutout sprites for no readability benefit.
inline void TextureSharpen(const u32* src, u32 w, u32 h, u32* dst, float strength)
{
    auto at = [&](u32 x, u32 y) -> u32
    {
        if (x >= w) x = w - 1;
        if (y >= h) y = h - 1;
        return src[y * w + x];
    };

    auto channel = [](u32 c, int shift) -> int { return (int)((c >> shift) & 0xFF); };
    auto clampByte = [](int v) -> u32 { return (u32)std::clamp(v, 0, 255); };

    for (u32 y = 0; y < h; y++)
    {
        for (u32 x = 0; x < w; x++)
        {
            u32 c = at(x, y);
            u32 n = at(x, y-1), s = at(x, y+1), wst = at(x-1, y), e = at(x+1, y);

            u32 out = 0;
            for (int shift = 0; shift < 24; shift += 8) // R, G, B - not alpha (shift 24)
            {
                int cc = channel(c, shift);
                int avg = (channel(n, shift) + channel(s, shift) + channel(wst, shift) + channel(e, shift)) / 4;
                int sharpened = cc + (int)((float)(cc - avg) * strength);
                out |= clampByte(sharpened) << shift;
            }
            out |= c & 0xFF000000; // alpha passed through unchanged

            dst[y * w + x] = out;
        }
    }
}


{
    return 8 << ((texparam >> 20) & 0x7);
}

inline u32 TextureHeight(u32 texparam)
{
    return 8 << ((texparam >> 23) & 0x7);
}

enum
{
    outputFmt_RGB6A5,
    outputFmt_RGBA8,
    outputFmt_BGRA8
};

template <int outputFmt>
void ConvertBitmapTexture(u32 width, u32 height, u32* output, u32 addr, GPU& gpu);
template <int outputFmt>
void ConvertCompressedTexture(u32 width, u32 height, u32* output, u32 addr, u32 addrAux, u32 palAddr, GPU& gpu);
template <int outputFmt, int X, int Y>
void ConvertAXIYTexture(u32 width, u32 height, u32* output, u32 addr, u32 palAddr, GPU& gpu);
template <int outputFmt, int colorBits>
void ConvertNColorsTexture(u32 width, u32 height, u32* output, u32 addr, u32 palAddr, bool color0Transparent, GPU& gpu);

template <typename TexLoaderT, typename TexHandleT>
class Texcache
{
public:
    Texcache(melonDS::GPU& gpu, const TexLoaderT& texloader)
        : GPU(gpu), TexLoader(texloader) // probably better if this would be a move constructor???
    {}

    u64 MaskedHash(u8* vram, u32 vramSize, u32 addr, u32 size)
    {
        u64 hash = 0;

        while (size > 0)
        {
            u32 pieceSize;
            if (addr + size > vramSize)
                // wraps around, only do the part inside
                pieceSize = vramSize - addr;
            else
                // fits completely inside
                pieceSize = size;

            hash = XXH64(&vram[addr], pieceSize, hash);

            addr += pieceSize;
            addr &= (vramSize - 1);
            assert(size >= pieceSize);
            size -= pieceSize;
        }

        return hash;
    }

    bool CheckInvalid(u32 start, u32 size, u64 oldHash, u64* dirty, u8* vram, u32 vramSize)
    {
        u32 startBit = start / VRAMDirtyGranularity;
        u32 bitsCount = ((start + size + VRAMDirtyGranularity - 1) / VRAMDirtyGranularity) - startBit;
    
        u32 startEntry = startBit >> 6;
        u64 entriesCount = ((startBit + bitsCount + 0x3F) >> 6) - startEntry;
        for (u32 j = startEntry; j < startEntry + entriesCount; j++)
        {
            if (GetRangedBitMask(j, startBit, bitsCount) & dirty[j & ((vramSize / VRAMDirtyGranularity)-1)])
            {
                if (MaskedHash(vram, vramSize, start, size) != oldHash)
                    return true;
            }
        }

        return false;
    }

    bool Update(u8& clrBitmapDirty)
    {
        auto textureDirty = GPU.VRAMDirty_Texture.DeriveState(GPU.VRAMMap_Texture, GPU);
        auto texPalDirty = GPU.VRAMDirty_TexPal.DeriveState(GPU.VRAMMap_TexPal, GPU);

        bool textureChanged = GPU.MakeVRAMFlat_TextureCoherent(textureDirty);
        bool texPalChanged = GPU.MakeVRAMFlat_TexPalCoherent(texPalDirty);

        clrBitmapDirty = 0;

        if (textureChanged || texPalChanged)
        {
            // check if slots 2 and 3 are dirty (for the clear bitmap)
            for (u32 j = (0x40000/(VRAMDirtyGranularity*64)); j < (0x60000/(VRAMDirtyGranularity*64)); j++)
            {
                if (textureDirty.Data[j])
                {
                    clrBitmapDirty |= (1<<0);
                    break;
                }
            }
            for (u32 j = (0x60000/(VRAMDirtyGranularity*64)); j < (0x80000/(VRAMDirtyGranularity*64)); j++)
            {
                if (textureDirty.Data[j])
                {
                    clrBitmapDirty |= (1<<1);
                    break;
                }
            }

            //printf("check invalidation %d\n", TexCache.size());
            for (auto it = Cache.begin(); it != Cache.end();)
            {
                TexCacheEntry& entry = it->second;
                if (textureChanged)
                {
                    for (u32 i = 0; i < 2; i++)
                    {
                        if (CheckInvalid(entry.TextureRAMStart[i], entry.TextureRAMSize[i],
                                entry.TextureHash[i],
                                textureDirty.Data,
                                GPU.VRAMFlat_Texture, sizeof(GPU.VRAMFlat_Texture)))
                            goto invalidate;
                    }
                }

                if (texPalChanged && entry.TexPalSize > 0)
                {
                    if (CheckInvalid(entry.TexPalStart, entry.TexPalSize,
                            entry.TexPalHash,
                            texPalDirty.Data,
                            GPU.VRAMFlat_TexPal, sizeof(GPU.VRAMFlat_TexPal)))
                        goto invalidate;
                }

                it++;
                continue;
            invalidate:
                FreeTextures[entry.WidthLog2][entry.HeightLog2].push_back(entry.Texture);

                //printf("invalidating texture %d\n", entry.ImageDescriptor);

                it = Cache.erase(it);
            }

            return true;
        }

        return false;
    }

    void GetTexture(u32 texParam, u32 palBase, TexHandleT& textureHandle, u32& layer, u32*& helper)
    {
        // remove sampling and texcoord gen params
        texParam &= ~0xC00F0000;

        u32 fmt = (texParam >> 26) & 0x7;
        u64 key = texParam;
        if (fmt != 7)
        {
            key |= (u64)palBase << 32;
            if (fmt == 5)
                key &= ~((u64)1 << 29);
        }
        //printf("%" PRIx64 " %" PRIx32 " %" PRIx32 "\n", key, texParam, palBase);

        assert(fmt != 0 && "no texture is not a texture format!");

        auto it = Cache.find(key);

        if (it != Cache.end())
        {
            textureHandle = it->second.Texture.TextureID;
            layer = it->second.Texture.Layer;
            helper = &it->second.LastVariant;
            return;
        }

        u32 widthLog2 = (texParam >> 20) & 0x7;
        u32 heightLog2 = (texParam >> 23) & 0x7;
        u32 width = 8 << widthLog2;
        u32 height = 8 << heightLog2;

        u32 addr = (texParam & 0xFFFF) * 8;

        TexCacheEntry entry = {0};

        entry.TextureRAMStart[0] = addr;
        entry.WidthLog2 = widthLog2;
        entry.HeightLog2 = heightLog2;

        // apparently a new texture
        if (fmt == 7)
        {
            entry.TextureRAMSize[0] = width*height*2;

            ConvertBitmapTexture<outputFmt_RGB6A5>(width, height, DecodingBuffer, addr, GPU);
        }
        else if (fmt == 5)
        {
            u32 slot1addr = 0x20000 + ((addr & 0x1FFFC) >> 1);
            if (addr >= 0x40000)
                slot1addr += 0x10000;

            entry.TextureRAMSize[0] = width*height/16*4;
            entry.TextureRAMStart[1] = slot1addr;
            entry.TextureRAMSize[1] = width*height/16*2;
            entry.TexPalStart = palBase*16;
            entry.TexPalSize = 0x10000;

            ConvertCompressedTexture<outputFmt_RGB6A5>(width, height, DecodingBuffer, addr, slot1addr, entry.TexPalStart, GPU);
        }
        else
        {
            u32 texSize, palAddr = palBase*16, numPalEntries;
            switch (fmt)
            {
            case 1: texSize = width*height; numPalEntries = 32; break;
            case 6: texSize = width*height; numPalEntries = 8; break;
            case 2: texSize = width*height/4; numPalEntries = 4; palAddr >>= 1; break;
            case 3: texSize = width*height/2; numPalEntries = 16; break;
            case 4: texSize = width*height; numPalEntries = 256; break;
            }

            palAddr &= 0x1FFFF;

            /*printf("creating texture | fmt: %d | %dx%d | %08x | %08x\n", fmt, width, height, addr, palAddr);
            svcSleepThread(1000*1000);*/

            entry.TextureRAMSize[0] = texSize;
            entry.TexPalStart = palAddr;
            entry.TexPalSize = numPalEntries*2;

            //assert(entry.TexPalStart+entry.TexPalSize <= 128*1024*1024);

            bool color0Transparent = texParam & (1 << 29);

            switch (fmt)
            {
            case 1: ConvertAXIYTexture<outputFmt_RGB6A5, 3, 5>(width, height, DecodingBuffer, addr, palAddr, GPU); break;
            case 6: ConvertAXIYTexture<outputFmt_RGB6A5, 5, 3>(width, height, DecodingBuffer, addr, palAddr, GPU); break;
            case 2: ConvertNColorsTexture<outputFmt_RGB6A5, 2>(width, height, DecodingBuffer, addr, palAddr, color0Transparent, GPU); break;
            case 3: ConvertNColorsTexture<outputFmt_RGB6A5, 4>(width, height, DecodingBuffer, addr, palAddr, color0Transparent, GPU); break;
            case 4: ConvertNColorsTexture<outputFmt_RGB6A5, 8>(width, height, DecodingBuffer, addr, palAddr, color0Transparent, GPU); break;
            }
        }

        for (int i = 0; i < 2; i++)
        {
            if (entry.TextureRAMSize[i])
                entry.TextureHash[i] = MaskedHash(GPU.VRAMFlat_Texture, sizeof(GPU.VRAMFlat_Texture),
                    entry.TextureRAMStart[i], entry.TextureRAMSize[i]);
        }
        if (entry.TexPalSize)
            entry.TexPalHash = MaskedHash(GPU.VRAMFlat_TexPal, sizeof(GPU.VRAMFlat_TexPal),
                entry.TexPalStart, entry.TexPalSize);

        // "Ixranium Graphics": upscale the just-decoded texel data 3x
        // before it goes anywhere near the GPU or the array-bucket
        // system below. uploadW/uploadH/uploadData (not width/height/
        // DecodingBuffer) are what the rest of this function actually
        // stores and uploads from here on - width/height above stay
        // exactly as they were for the VRAM-address decode math, which
        // must stay tied to the DS's real, native texture dimensions.
        u32 uploadW = width, uploadH = height;
        u32* uploadData = DecodingBuffer;
        if (IxraniumTexUpscaleEnabled.load(std::memory_order_relaxed))
        {
            EagleUpscale3x(DecodingBuffer, width, height, UpscaleBuffer);
            uploadW = width * 3;
            uploadH = height * 3;
            uploadData = UpscaleBuffer;

            // Sharpening always runs immediately after (never before -
            // see TextureSharpen's own comment) the upscale, on its
            // output - both are part of the one "Ixranium Graphics"
            // toggle rather than a separate switch.
            TextureSharpen(UpscaleBuffer, uploadW, uploadH, SharpenBuffer, kSharpenStrength);
            uploadData = SharpenBuffer;
        }

        auto& texArrays = TexArrays[widthLog2][heightLog2];
        auto& freeTextures = FreeTextures[widthLog2][heightLog2];

        if (freeTextures.size() == 0)
        {
            texArrays.resize(texArrays.size()+1);
            TexHandleT& array = texArrays[texArrays.size()-1];

            u32 layers = std::min<u32>((8*1024*1024) / (uploadW*uploadH*4), 64);

            // allocate new array texture
            //printf("allocating new layer set for %d %d %d %d\n", uploadW, uploadH, texArrays.size()-1, array.ImageDescriptor);
            array = TexLoader.GenerateTexture(uploadW, uploadH, layers);

            for (u32 i = 0; i < layers; i++)
            {
                freeTextures.push_back(TexArrayEntry{array, i});
            }
        }

        TexArrayEntry storagePlace = freeTextures[freeTextures.size()-1];
        freeTextures.pop_back();

        entry.Texture = storagePlace;

        TexLoader.UploadTexture(storagePlace.TextureID, uploadW, uploadH, storagePlace.Layer, uploadData);
        //printf("using storage place %d %d | %d %d (%d)\n", uploadW, uploadH, storagePlace.TexArrayIdx, storagePlace.LayerIdx, array.ImageDescriptor);

        textureHandle = storagePlace.TextureID;
        layer = storagePlace.Layer;
        helper = &Cache.emplace(std::make_pair(key, entry)).first->second.LastVariant;
    }

    void Reset()
    {
        for (u32 i = 0; i < 8; i++)
        {
            for (u32 j = 0; j < 8; j++)
            {
                for (u32 k = 0; k < TexArrays[i][j].size(); k++)
                    TexLoader.DeleteTexture(TexArrays[i][j][k]);
                TexArrays[i][j].clear();
                FreeTextures[i][j].clear();
            }
        }
        Cache.clear();
    }

private:
    melonDS::GPU& GPU;

    struct TexArrayEntry
    {
        TexHandleT TextureID;
        u32 Layer;
    };

    struct TexCacheEntry
    {
        u32 LastVariant; // very cheap way to make variant lookup faster

        u32 TextureRAMStart[2], TextureRAMSize[2];
        u32 TexPalStart, TexPalSize;
        u8 WidthLog2, HeightLog2;
        TexArrayEntry Texture;

        u64 TextureHash[2];
        u64 TexPalHash;
    };
    std::unordered_map<u64, TexCacheEntry> Cache;

    TexLoaderT TexLoader;

    std::vector<TexArrayEntry> FreeTextures[8][8];
    std::vector<TexHandleT> TexArrays[8][8];

    u32 DecodingBuffer[1024*1024];
    // Scratch space for the 3x-upscaled copy (see EagleUpscale3x / the
    // IxraniumTexUpscaleEnabled check in GetTexture). Sized for the
    // largest possible DS texture (1024x1024) upscaled 3x on each axis.
    u32 UpscaleBuffer[3072*3072];
    // Second scratch buffer for the post-upscale sharpening pass (see
    // TextureSharpen) - needs to be separate from UpscaleBuffer since
    // sharpening reads each pixel's neighbours while writing, which an
    // in-place pass would corrupt (a pixel's neighbour may already have
    // been overwritten with its own sharpened value by the time it's
    // read). Same size as UpscaleBuffer for the same reason.
    u32 SharpenBuffer[3072*3072];
};

}

#endif