#ifndef GPU3D_TEXCACHE
#define GPU3D_TEXCACHE

#include "types.h"
#include "GPU.h"

#include <algorithm>
#include <assert.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
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

// Separate switch for JUST the 2D OBJ/sprite atlas upscale pass in
// GPU2D_OpenGL.cpp's PrerenderSprites() - added so "Ixranium Graphics
// Classic" (BG layers + 3D textures upscaled, sprites left native) and
// "Ixranium Sprites" (everything upscaled, the original all-on
// behaviour) can be toggled and compared independently. This matters
// because PrerenderSprites' upscale runs on the *whole* 1024x512 sprite
// atlas every time SpriteDirty is set, which for a fighting game's
// constantly-animating character sprites is close to every frame - a
// fundamentally different (and much more expensive, in practice) cost
// profile than the 3D texture cache's per-texture, per-actual-VRAM-
// write caching in this file. Only has any effect when
// IxraniumTexUpscaleEnabled is also on ("Sprites" is an addition on top
// of "Classic", not an independent mode) - see the two call sites in
// GPU2D_OpenGL.cpp's PrerenderSprites()/DoRenderSprites().
inline std::atomic<bool> IxraniumSpritesEnabled{true};

// Sprite atlas debug dump (see AtlasSettingsDialog / HK_DumpSpriteAtlas):
// set DumpSpriteAtlasRequested to trigger a one-shot readback of the
// current SpriteUpTex atlas on the render thread (the only thread that
// legally owns the GL context), which then calls AtlasDumpCallback with
// the raw RGBA pixels so the Qt frontend can save it as PNG without the
// core linking against Qt. Cleared back to false once handled.
inline std::atomic<bool> DumpSpriteAtlasRequested{false};
inline std::function<void(const u8* rgba, int width, int height, int engineNum)> AtlasDumpCallback = nullptr;

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

// How much channel-by-channel slack ColorsClose() allows. Compared
// directly against DecodingBuffer's native RGB6A5 channel values (6-bit
// RGB, 0-63 - see ConvertBitmapTexture<outputFmt_RGB6A5> etc.), NOT a
// 0-255 byte range despite channels being stored one-per-byte. Used to
// decide whether two neighbouring pixels are "the same" for the
// corner-detection logic below (EagleUpscale4x/GPUUpscaleSharpenSaturate)
// - NOT a general smoothing/blur amount. Was kept very tiny (2) on the
// assumption that anything bigger would blend two genuinely different
// palette entries placed next to each other for dithering - but that
// same tightness means a dithering pixel and its neighbour are always
// "not close", which can accidentally satisfy the corner-detection
// conditions meant for real shape corners (an "L" bend in solid
// colour), not per-pixel dithering noise. A false corner-fire there
// replicates whatever colour the dither pixel happens to be (e.g. a
// fine-shading colour that reads as blended only at native pixel
// density) across a whole 2x2 output block - a speckled colour fringe
// hugging otherwise-clean high-contrast edges. 3 (proportionally the
// same looseness as 12 would be against a 0-255 range) is loose enough
// to treat adjacent dithering steps as "the same" (defusing the false
// corner triggers) while staying well under the gap between two
// visually distinct palette entries, so real shape corners still
// upscale the same as before.
inline constexpr u32 kColorTolerance = 3;

// Persistent worker-thread pool for the row-parallel upscale/sharpen
// work below. Deliberately NOT spawning std::thread objects per call:
// this function runs once per cache-miss texture, and a scene with a
// lot of on-screen sprites/icons that keep re-uploading (a character
// select wheel, animated sprites) can hit dozens of cache-misses in a
// single frame - creating and joining N OS threads that many times per
// frame adds real overhead (thread creation is comparatively expensive)
// that can outweigh, or even net-negative, whatever the parallel pixel
// work saved. This pool's threads are created exactly once, sleep on a
// condition variable between jobs, and are reused for every call for
// the lifetime of the process.
class RowWorkerPool
{
public:
    static RowWorkerPool& Get()
    {
        static RowWorkerPool instance;
        return instance;
    }

    u32 ThreadCount() const { return (u32)Workers.size(); }

    // Runs `fn(start, end)` for each row-chunk in [firstChunk, numChunks)
    // on the pool's worker threads (chunk i covers
    // [i*rowsPerChunk, min((i+1)*rowsPerChunk, rowCount))) and returns
    // immediately without waiting - call WaitAll() after doing your own
    // work to block until they finish. Chunks before firstChunk are the
    // caller's own responsibility (see ParallelForRows, which runs
    // chunk 0 on the calling thread itself, concurrently with these).
    template <typename Fn>
    void DispatchChunks(u32 firstChunk, u32 numChunks, u32 rowCount, u32 rowsPerChunk, Fn&& fn)
    {
        u32 count = numChunks - firstChunk;
        if (count == 0) { Pending.store(0, std::memory_order_relaxed); return; }
        Pending.store(count, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(QueueMutex);
            for (u32 i = firstChunk; i < numChunks; i++)
            {
                u32 start = i * rowsPerChunk;
                u32 end = std::min(start + rowsPerChunk, rowCount);
                Jobs.push_back([&fn, start, end]() { fn(start, end); });
            }
        }
        QueueCV.notify_all();
    }

    // Blocks until every job from the most recent DispatchChunks() call
    // has completed. Simple spin-wait-with-yield rather than another
    // condvar - job durations here are sub-millisecond, so this keeps
    // things simple without adding a second synchronisation object.
    void WaitAll()
    {
        while (Pending.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
    }

private:
    RowWorkerPool()
    {
        u32 hw = std::max(1u, std::thread::hardware_concurrency());
        // Leave one hardware thread for the caller (which also takes a
        // share of the row range itself - see ParallelForRows) and for
        // melonDS's other threads (emu core, audio, etc).
        u32 numWorkers = hw > 1 ? hw - 1 : 0;
        for (u32 i = 0; i < numWorkers; i++)
            Workers.emplace_back([this]() { WorkerLoop(); });
    }

    ~RowWorkerPool()
    {
        {
            std::lock_guard<std::mutex> lock(QueueMutex);
            Stopping = true;
        }
        QueueCV.notify_all();
        for (auto& w : Workers) w.join();
    }

    void WorkerLoop()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(QueueMutex);
                QueueCV.wait(lock, [this]() { return Stopping || !Jobs.empty(); });
                if (Stopping && Jobs.empty())
                    return;
                job = std::move(Jobs.front());
                Jobs.pop_front();
            }
            job();
            Pending.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    std::vector<std::thread> Workers;
    std::deque<std::function<void()>> Jobs;
    std::mutex QueueMutex;
    std::condition_variable QueueCV;
    std::atomic<u32> Pending{0};
    bool Stopping = false;
};

// Splits [0, rowCount) into chunks and runs `fn(rowStart, rowEnd)` for
// each chunk on the shared RowWorkerPool, then waits for all of them -
// used to spread the row-independent upscale/sharpen work below across
// CPU cores instead of running single-threaded. Every row of
// EagleUpscale4x/TextureSharpenAndSaturate only reads its own
// neighbouring *source* rows and writes its own *destination* row, so
// splitting by row range is safe: no two workers ever write the same
// output pixel, and reads never race a write. Falls back to running on
// the calling thread directly when the work is small enough that
// dispatch overhead wouldn't pay off, or the machine only has one
// hardware thread.
// ==================== Ixranium performance profiler ====================
// Purpose: measure exactly where render-thread time is going in the
// upscale pipeline instead of guessing further from screenshots/CPU%
// alone. Every counter here is an atomic add per texture - no locking,
// no per-texture I/O (the old per-texture printf() elsewhere in this
// file was itself a big chunk of an earlier problem - see its comment).
// The table is written to stdout AND ixranium_profile.log only once
// every kProfileIntervalFrames frames, never per texture, so the
// profiler's own overhead stays negligible relative to what it's
// measuring.
struct IxraniumProfiler
{
    std::atomic<u64> CacheHits{0}, CacheMisses{0};
    std::atomic<u64> ContentCacheHits{0}, ContentCacheMisses{0};
    std::atomic<u64> NewGLArrayAllocs{0}, GLUploads{0};

    std::atomic<u64> DecodeNs{0};
    std::atomic<u64> UpscaleNs{0};
    std::atomic<u64> SharpenNs{0};
    std::atomic<u64> GLAllocNs{0};
    std::atomic<u64> GLUploadNs{0};

    // Whole-frame wall-clock timing - the gap between successive
    // Update() calls (Update() runs exactly once per emulated frame
    // regardless of whether Ixranium upscaling is on), which covers
    // EVERYTHING for that frame: CPU/ARM emulation, all of 2D/3D
    // rendering (not just this texture cache), present/swap, vsync
    // wait - not just the texture-cache costs the rest of this struct
    // measures. Recorded unconditionally (even with upscaling off),
    // because most of the profile windows so far showed 0 texture-cache
    // misses yet FPS stayed low regardless - meaning whatever is
    // actually capping FPS in those frames was never something this
    // struct's other counters could see at all. This is the fix for
    // that blind spot.
    std::chrono::high_resolution_clock::time_point LastFrameAt{};
    bool HaveLastFrameAt = false;
    std::atomic<u64> FrameTimeSumNs{0};
    std::atomic<u64> FrameTimeMaxNs{0};
    std::atomic<u64> FrameTimeMinNs{UINT64_MAX};
    std::atomic<u64> FrameTimeSamples{0};

    std::atomic<u64> FrameCount{0};
    std::atomic<u64> LiveCacheEntries{0};

    // Cumulative (never reset by Reset() - tracks the whole session)
    // total bytes ever requested via GenerateTexture(), i.e. every
    // array's uploadW*uploadH*4*layers at the moment it was allocated -
    // NOT current live usage (arrays are never individually freed
    // outside Reset(), see TexArrays'/FreeTextures' comments), this is
    // "how much GPU memory has this process asked the driver to
    // reserve for texture arrays, all-time". Added specifically to test
    // whether a large one-off allocation (or accumulation of many) is
    // what triggers the permanent post-transition slowdown, since the
    // live-entry-count-based LRU cap didn't stop it (12-15 entries is
    // nothing on its own - but each array's *layers* are pre-allocated
    // up front, up to 64 at once, regardless of how many are actually
    // used yet, so a handful of new arrays can still mean tens of MB
    // requested in one go).
    std::atomic<u64> TotalArrayBytesEverAllocated{0};
    // Same thing, but only this window's allocations (resets with the
    // rest of the per-window counters) - lets you see exactly how much
    // got requested in the window where the slowdown started.
    std::atomic<u64> NewArrayBytesThisWindow{0};
    // How many distinct (widthLog2, heightLog2) buckets have at least
    // one array right now - each bucket's first array is a "cold" 8MB-
    // or-less allocation; many distinct texture *sizes* showing up
    // (rather than many textures of a few common sizes) multiplies how
    // often that cold-allocation cost is paid.
    std::atomic<u64> LiveBucketsInUse{0};
    // Slow-frame counter: frames in this window whose wall-clock time
    // exceeded 33ms (i.e. would have missed even a 30 FPS target) -
    // avg/min/max can hide a lot; this says plainly how many frames out
    // of the window were actually bad.
    std::atomic<u64> SlowFrames{0};

    static constexpr u64 kProfileIntervalFrames = 20;

    static IxraniumProfiler& Get() { static IxraniumProfiler p; return p; }

    // RAII helper: `IxraniumProfiler::Timer t(target);` adds the elapsed
    // time to `target` when it goes out of scope. Keeps the call sites
    // below to one line each instead of manual now()/subtract pairs.
    struct Timer
    {
        std::atomic<u64>& Target;
        std::chrono::high_resolution_clock::time_point Start;
        explicit Timer(std::atomic<u64>& target)
            : Target(target), Start(std::chrono::high_resolution_clock::now()) {}
        ~Timer()
        {
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - Start).count();
            Target.fetch_add((u64)ns, std::memory_order_relaxed);
        }
    };

    // Call once per frame (see Texcache::Update, which already runs
    // exactly once per frame to check VRAM invalidation). Dumps and
    // resets the table every kProfileIntervalFrames frames. Frame
    // timing is recorded every call regardless of upscaleOn; the
    // detailed texture-pipeline counters only matter (and only get
    // reset) when upscaling is actually on, same as before.
    void OnFrame(bool upscaleOn, u64 liveCacheEntries)
    {
        LiveCacheEntries.store(liveCacheEntries, std::memory_order_relaxed);
        auto now = std::chrono::high_resolution_clock::now();
        if (HaveLastFrameAt)
        {
            u64 deltaNs = (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(now - LastFrameAt).count();
            FrameTimeSumNs.fetch_add(deltaNs, std::memory_order_relaxed);
            FrameTimeSamples.fetch_add(1, std::memory_order_relaxed);
            u64 prevMax = FrameTimeMaxNs.load(std::memory_order_relaxed);
            while (deltaNs > prevMax && !FrameTimeMaxNs.compare_exchange_weak(prevMax, deltaNs, std::memory_order_relaxed)) {}
            u64 prevMin = FrameTimeMinNs.load(std::memory_order_relaxed);
            while (deltaNs < prevMin && !FrameTimeMinNs.compare_exchange_weak(prevMin, deltaNs, std::memory_order_relaxed)) {}
            if (deltaNs > 33000000ULL) // 33ms - would miss even a 30 FPS target
                SlowFrames.fetch_add(1, std::memory_order_relaxed);
        }
        LastFrameAt = now;
        HaveLastFrameAt = true;

        if (!upscaleOn)
            return;
        if (FrameCount.fetch_add(1, std::memory_order_relaxed) + 1 < kProfileIntervalFrames)
            return;
        FrameCount.store(0, std::memory_order_relaxed);
        Dump();
        Reset();
    }

    void Dump()
    {
        auto ms = [](u64 ns) { return (double)ns / 1e6; };
        u64 hits = CacheHits.load(), misses = CacheMisses.load();
        u64 chits = ContentCacheHits.load(), cmisses = ContentCacheMisses.load();
        u64 fullRuns = cmisses; // pipeline only actually runs on a content-cache miss

        u64 fSamples = FrameTimeSamples.load();
        u64 fSum = FrameTimeSumNs.load();
        u64 fMax = FrameTimeMaxNs.load();
        u64 fMin = fSamples ? FrameTimeMinNs.load() : 0;
        double avgFrameMs = fSamples ? ms(fSum) / (double)fSamples : 0.0;
        double avgFps = avgFrameMs > 0.0 ? 1000.0 / avgFrameMs : 0.0;

        char buf[2048];
        int n = 0;
        n += snprintf(buf+n, sizeof(buf)-n, "==== Ixranium profile (last %llu frames) ====\n", (unsigned long long)kProfileIntervalFrames);
        n += snprintf(buf+n, sizeof(buf)-n, "pool worker threads: %u (0 = fallback, single-threaded)\n", RowWorkerPool::Get().ThreadCount());
        n += snprintf(buf+n, sizeof(buf)-n, "mode: %s\n",
            IxraniumTexUpscaleEnabled.load(std::memory_order_relaxed) ? "ON" : "OFF");
        n += snprintf(buf+n, sizeof(buf)-n, "--- whole-frame wall-clock (CPU emu + ALL rendering + present, not just this texture cache) ---\n");
        n += snprintf(buf+n, sizeof(buf)-n, "frame time avg/min/max ms: %.2f / %.2f / %.2f   (~%.1f FPS avg)\n",
            avgFrameMs, ms(fMin), ms(fMax), avgFps);
        n += snprintf(buf+n, sizeof(buf)-n, "slow frames (>33ms, i.e. <30fps) this window: %llu / %llu\n",
            (unsigned long long)SlowFrames.load(), (unsigned long long)kProfileIntervalFrames);
        n += snprintf(buf+n, sizeof(buf)-n, "live cache entries (permanently GPU-resident textures): %llu / %llu cap\n",
            (unsigned long long)LiveCacheEntries.load(), (unsigned long long)512);
        n += snprintf(buf+n, sizeof(buf)-n, "--- GPU texture-array memory (never individually freed until emu reset - see comments) ---\n");
        n += snprintf(buf+n, sizeof(buf)-n, "live buckets in use (distinct texture sizes with an array): %llu\n",
            (unsigned long long)LiveBucketsInUse.load());
        n += snprintf(buf+n, sizeof(buf)-n, "array bytes requested this window: %.2f MB\n",
            (double)NewArrayBytesThisWindow.load() / (1024.0*1024.0));
        n += snprintf(buf+n, sizeof(buf)-n, "array bytes requested all-time (session total): %.2f MB\n",
            (double)TotalArrayBytesEverAllocated.load() / (1024.0*1024.0));
        n += snprintf(buf+n, sizeof(buf)-n, "%-26s %10s %12s\n", "metric", "count", "total ms");
        n += snprintf(buf+n, sizeof(buf)-n, "%-26s %10llu %12s\n", "texcache hits",          (unsigned long long)hits,    "-");
        n += snprintf(buf+n, sizeof(buf)-n, "%-26s %10llu %12s\n", "texcache misses",        (unsigned long long)misses,  "-");
        n += snprintf(buf+n, sizeof(buf)-n, "%-26s %10llu %12s\n", "content-cache hits",     (unsigned long long)chits,   "-");
        n += snprintf(buf+n, sizeof(buf)-n, "%-26s %10llu %12s\n", "content-cache misses",   (unsigned long long)cmisses, "-");
        n += snprintf(buf+n, sizeof(buf)-n, "%-26s %10llu %12.2f\n", "  decode",              (unsigned long long)fullRuns, ms(DecodeNs.load()));
        n += snprintf(buf+n, sizeof(buf)-n, "%-26s %10llu %12.2f\n", "  eagle upscale 4x",    (unsigned long long)fullRuns, ms(UpscaleNs.load()));
        n += snprintf(buf+n, sizeof(buf)-n, "%-26s %10llu %12.2f\n", "  sharpen+saturate",    (unsigned long long)fullRuns, ms(SharpenNs.load()));
        n += snprintf(buf+n, sizeof(buf)-n, "%-26s %10llu %12.2f\n", "GL new-array allocs",   (unsigned long long)NewGLArrayAllocs.load(), ms(GLAllocNs.load()));
        n += snprintf(buf+n, sizeof(buf)-n, "%-26s %10llu %12.2f\n", "GL uploads",            (unsigned long long)GLUploads.load(), ms(GLUploadNs.load()));
        n += snprintf(buf+n, sizeof(buf)-n, "===============================================\n");
        (void)n;

        fputs(buf, stdout);
        fflush(stdout);
        FILE* f = fopen("ixranium_profile.log", "a");
        if (f) { fputs(buf, f); fclose(f); }
    }

    void Reset()
    {
        CacheHits = 0; CacheMisses = 0;
        ContentCacheHits = 0; ContentCacheMisses = 0;
        NewGLArrayAllocs = 0; GLUploads = 0;
        DecodeNs = 0; UpscaleNs = 0; SharpenNs = 0; GLAllocNs = 0; GLUploadNs = 0;
        FrameTimeSumNs = 0; FrameTimeMaxNs = 0; FrameTimeMinNs = UINT64_MAX; FrameTimeSamples = 0;
        SlowFrames = 0;
        NewArrayBytesThisWindow = 0;
        // NOTE: TotalArrayBytesEverAllocated and LiveBucketsInUse are
        // deliberately NOT reset here - they're cumulative/live-state
        // counters for the whole session, not per-window ones.
    }
};


template <typename Fn>
inline void ParallelForRows(u32 rowCount, Fn&& fn)
{
    RowWorkerPool& pool = RowWorkerPool::Get();
    u32 poolThreads = pool.ThreadCount();

    // Not worth dispatching for a handful of rows (small sprites,
    // icons) - queue/wait overhead alone would outweigh the work being
    // parallelised. Threshold is intentionally higher than a naive
    // per-call thread-spawn version would need, since this still has to
    // cross a mutex + condvar to reach the pool.
    if (poolThreads == 0 || rowCount < 64)
    {
        fn(0u, rowCount);
        return;
    }

    u32 numWorkerChunks = std::min(poolThreads + 1, rowCount); // +1: caller's own chunk too
    u32 rowsPerChunk = (rowCount + numWorkerChunks - 1) / numWorkerChunks;

    // Chunk 0 runs on the calling thread itself, concurrently with the
    // rest running on the pool - no reason to leave a whole hardware
    // thread idle-waiting.
    pool.DispatchChunks(1, numWorkerChunks, rowCount, rowsPerChunk, fn);
    fn(0u, std::min(rowsPerChunk, rowCount));
    pool.WaitAll();
}

// Strength of the post-upscale sharpening pass (TextureSharpen), in the
// same 0-1 range CAS used for the earlier (now-removed) screen-space
// sharpener - kept at a similarly moderate level for the same reason:
// enough to make edges/text read as crisper, not enough to grow a
// visible halo around them.
inline constexpr float kSharpenStrength = 0.2f;

// Saturation boost applied after sharpening (see ApplySaturationBoost).
// 1.0 = no change; 1.05 = +5%, kept deliberately subtle - this is meant
// to be barely noticeable on its own, not a stylistic colour-grade.
inline constexpr float kSaturationBoost = 1.05f;

// Scale3x (a.k.a. the 3x extension of the Eagle/Scale2x family): same
// copy-only, equality-test-based approach as EagleUpscale2x below - no
// colour blending anywhere, so it still can't introduce a colour that
// wasn't already in the original texture. Produces a 3x3 output block
// per source pixel instead of 2x2; the middle row/column additionally
// gets to inherit a neighbour's colour (not just the four corners),
// which is what catches a few more diagonal edges than the 2x version.
// Simple linear per-channel blend between two packed RGBA8 texels - our
// own plain weighted average, used to round the corners EagleUpscale3x
// below produces (see kCornerBlend/kEdgeBlend). t=0 returns `a` unchanged, t=1
// returns `b` unchanged; anything between is a straight
// a*(1-t) + b*t mix on each of the four channels independently,
// including alpha.
inline u32 BlendColors(u32 a, u32 b, float t)
{
    auto ch = [&](int shift) -> u32
    {
        int ca = (int)((a >> shift) & 0xFF);
        int cb = (int)((b >> shift) & 0xFF);
        int v = (int)((float)ca * (1.0f - t) + (float)cb * t + 0.5f);
        return (u32)std::clamp(v, 0, 255) << shift;
    };
    return ch(0) | ch(8) | ch(16) | ch(24);
}

// How much of the neighbour colour EagleUpscale3x's rounded corners use
// (see BlendColors above), where each sub-pixel would otherwise be a
// flat 100%-neighbour copy. 1.0 = the original hard-copy behaviour (a
// sharp, pixelated staircase); lower values blend more of the centre
// pixel back in.
//
// Two separate strengths (rather than one flat ratio everywhere) is
// what actually produces a smooth-looking curve instead of just a
// softer staircase: kCornerBlend is the strongest, applied at the
// actual diagonal corner (E0/E2/E6/E8) - closest to where the true edge
// sits, so it stays closest to the neighbour's colour. kEdgeBlend is
// weaker, applied one step further out (E1/E3/E5/E7) - since those
// sub-pixels are further from the corner, giving them the same full
// strength as the corner itself would just move the hard edge outward
// rather than soften it. The centre sub-pixel (E4) is never blended.
// The resulting strong->medium->unchanged falloff across three
// sub-pixel steps is what reads as a rounded curve rather than a
// uniformly-softened block.
inline constexpr float kCornerBlend = 0.85f;
inline constexpr float kEdgeBlend = 0.45f;

// 4x, single-pass (NOT 2x applied twice - see the "why 4x is risky"
// discussion this came out of: re-applying 2x compounds whatever small
// mismatches the first pass made into the second, on top of costing 16x
// the native pixel count for no reason beyond a texture already 4x'd
// once). This one-pass version costs the same 16x either way, but
// starts from the original, undistorted native pixels every time.
//
// Same corner/no-corner detection as EagleUpscale3x (D==B etc.) - what's
// different is the output block is 4x4 (16 cells) instead of 3x3 (9),
// which gives three full graduated steps within each corner's own
// quadrant instead of two: the true corner cell (kTier0, strongest),
// then the two cells one step away along each axis (kTier1), then the
// single cell two steps away, diagonally furthest from the corner
// within its quadrant (kTier2, weakest - closest to unchanged). Each of
// the four 2x2... rather 2-cell-deep quadrants is entirely independent,
// driven only by its own corner condition, so there's no shared/OR'd
// condition to reconcile the way EagleUpscale3x's edge cells needed.
inline constexpr float kTier0 = 1.00f; // the actual corner cell
inline constexpr float kTier1 = 0.97f; // one step away (two cells)
inline constexpr float kTier2 = 0.85f; // two steps away (one cell, deepest into the quadrant)

inline void EagleUpscale4x(const u32* src, u32 srcW, u32 srcH, u32* dst)
{
    u32 dstW = srcW * 4;

    auto at = [&](u32 x, u32 y) -> u32
    {
        if (x >= srcW) x = srcW - 1;
        if (y >= srcH) y = srcH - 1;
        return src[y * srcW + x];
    };
    auto Close = [](u32 a, u32 b) { return ColorsClose(a, b, kColorTolerance); };

    const float tiers[3] = { kTier0, kTier1, kTier2 };

    // Fills one 2x2 quadrant of the 4x4 output block. (cellRow, cellCol)
    // is the quadrant's position within the block (0 or 1 for top/left
    // vs bottom/right); (cornerRow, cornerCol) is which of that
    // quadrant's own two rows/cols is the side the true corner sits on
    // (0 = top/left side of the quadrant, 1 = bottom/right side) - e.g.
    // the top-left block quadrant's corner sits at its own local (0,0).
    auto fillQuadrant = [&](u32* dstBase, bool active, u32 neighbor, u32 centre,
                             int cornerRow, int cornerCol)
    {
        for (int lr = 0; lr < 2; lr++)
        {
            for (int lc = 0; lc < 2; lc++)
            {
                u32 out = centre;
                if (active)
                {
                    int dist = std::abs(lr - cornerRow) + std::abs(lc - cornerCol);
                    out = BlendColors(centre, neighbor, tiers[dist]);
                }
                dstBase[lr * dstW + lc] = out;
            }
        }
    };

    ParallelForRows(srcH, [&](u32 yStart, u32 yEnd)
    {
        for (u32 y = yStart; y < yEnd; y++)
        {
            for (u32 x = 0; x < srcW; x++)
            {
                u32 B = at(x, y-1);
                u32 D = at(x-1, y),   E = at(x, y),   F = at(x+1, y);
                u32 H = at(x, y+1);

                bool condTL = Close(D,B) && !Close(D,H) && !Close(B,F);
                bool condTR = Close(B,F) && !Close(B,D) && !Close(F,H);
                bool condBL = Close(D,H) && !Close(D,B) && !Close(H,F);
                bool condBR = Close(H,F) && !Close(H,D) && !Close(F,B);

                u32* block = dst + (y*4) * dstW + (x*4);
                fillQuadrant(block,                    condTL, D, E, 0, 0); // top-left quadrant, corner at its own (0,0)
                fillQuadrant(block + 2,                 condTR, F, E, 0, 1); // top-right quadrant, corner at its own (0,1)
                fillQuadrant(block + 2*dstW,             condBL, D, E, 1, 0); // bottom-left quadrant, corner at its own (1,0)
                fillQuadrant(block + 2*dstW + 2,          condBR, F, E, 1, 1); // bottom-right quadrant, corner at its own (1,1)
            }
        }
    });
}

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
    auto RoundCorner = [](u32 neighbor, u32 centre) { return BlendColors(centre, neighbor, kCornerBlend); };
    auto RoundEdge = [](u32 neighbor, u32 centre) { return BlendColors(centre, neighbor, kEdgeBlend); };

    for (u32 y = 0; y < srcH; y++)
    {
        for (u32 x = 0; x < srcW; x++)
        {
            u32 A = at(x-1, y-1), B = at(x, y-1), C = at(x+1, y-1);
            u32 D = at(x-1, y),   E = at(x, y),   F = at(x+1, y);
            u32 G = at(x-1, y+1), H = at(x, y+1), I = at(x+1, y+1);

            u32 E0 = (Close(D,B) && !Close(D,H) && !Close(B,F)) ? RoundCorner(D,E) : E;
            u32 E1 = ((Close(D,B) && !Close(D,H) && !Close(B,F) && !Close(E,C)) || (Close(B,F) && !Close(B,D) && !Close(F,H) && !Close(E,A))) ? RoundEdge(B,E) : E;
            u32 E2 = (Close(B,F) && !Close(B,D) && !Close(F,H)) ? RoundCorner(F,E) : E;
            u32 E3 = ((Close(D,B) && !Close(D,H) && !Close(B,F) && !Close(E,G)) || (Close(D,H) && !Close(D,B) && !Close(H,F) && !Close(E,A))) ? RoundEdge(D,E) : E;
            u32 E4 = E;
            u32 E5 = ((Close(B,F) && !Close(B,D) && !Close(F,H) && !Close(E,I)) || (Close(F,H) && !Close(F,B) && !Close(H,D) && !Close(E,C))) ? RoundEdge(F,E) : E;
            u32 E6 = (Close(D,H) && !Close(D,B) && !Close(H,F)) ? RoundCorner(D,E) : E;
            u32 E7 = ((Close(D,H) && !Close(D,B) && !Close(H,F) && !Close(E,I)) || (Close(H,F) && !Close(H,D) && !Close(F,B) && !Close(E,G))) ? RoundEdge(H,E) : E;
            u32 E8 = (Close(H,F) && !Close(H,D) && !Close(F,B)) ? RoundCorner(F,E) : E;

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

// Smooth (true bilinear) 2x upscale - unlike EagleUpscale2x/3x/4x
// above, this does NOT try to preserve flat pixel blocks or only round
// detected corners. Every output pixel is a genuine blend of its
// nearest source pixels, everywhere, so the result has no visible flat
// "blocks" anywhere - continuous curves/gradients instead of a
// pixel-art look. This is what you want when the goal is "don't see
// pixels at all" rather than "keep pixel art crisp but round its
// corners".
inline void SmoothUpscale2x(const u32* src, u32 srcW, u32 srcH, u32* dst)
{
    u32 dstW = srcW * 2, dstH = srcH * 2;

    auto at = [&](int x, int y) -> u32
    {
        if (x < 0) x = 0; if (x >= (int)srcW) x = srcW - 1;
        if (y < 0) y = 0; if (y >= (int)srcH) y = srcH - 1;
        return src[y * srcW + x];
    };
    auto channel = [](u32 c, int shift) -> int { return (int)((c >> shift) & 0xFF); };
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    for (u32 y = 0; y < dstH; y++)
    {
        // Sample position in source space, offset by half a source
        // pixel so the 2x2 output block straddles the source pixel
        // centre - this is what makes every output pixel a genuine
        // blend rather than 1/4 of them landing exactly on a source
        // sample (which would leave visible unblended pixels).
        float sy = (y + 0.5f) / 2.0f - 0.5f;
        int y0 = (int)std::floor(sy);
        float ty = sy - y0;

        for (u32 x = 0; x < dstW; x++)
        {
            float sx = (x + 0.5f) / 2.0f - 0.5f;
            int x0 = (int)std::floor(sx);
            float tx = sx - x0;

            u32 c00 = at(x0, y0),     c10 = at(x0+1, y0);
            u32 c01 = at(x0, y0+1),   c11 = at(x0+1, y0+1);

            u32 out = 0;
            for (int shift = 0; shift < 32; shift += 8)
            {
                float top = lerp((float)channel(c00, shift), (float)channel(c10, shift), tx);
                float bot = lerp((float)channel(c01, shift), (float)channel(c11, shift), tx);
                float v = lerp(top, bot, ty);
                out |= (u32)std::clamp((int)(v + 0.5f), 0, 255) << shift;
            }
            dst[y * dstW + x] = out;
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
//
// Adaptive strength: the push is scaled by how large the local contrast
// already is (edgeMag below) - a thin, high-contrast line (text/line-
// art) gets pushed harder than the requested base strength, while a
// wide, gentle gradient barely gets pushed at all. This is what keeps a
// strength high enough to make text crisp from also visibly staircasing
// smooth shading elsewhere, which a single flat strength applied
// everywhere can't do at the same time.
inline void TextureSharpen(const u32* src, u32 w, u32 h, u32* dst, float strength)
{
    auto at = [&](u32 x, u32 y) -> u32
    {
        if (x >= w) x = w - 1;
        if (y >= h) y = h - 1;
        return src[y * w + x];
    };

    auto channel = [](u32 c, int shift) -> int { return (int)((c >> shift) & 0xFF); };
    // See TextureSharpenAndSaturate's copy of this comment: DecodingBuffer
    // is native RGB6A5 (0-63 per RGB channel), and 3DRenderFS.glsl always
    // divides by 63 - clampByte must match that, not the full byte range.
    auto clampByte = [](int v) -> u32 { return (u32)std::clamp(v, 0, 63); };
    auto smoothstepf = [](float lo, float hi, float x) -> float
    {
        float t = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    for (u32 y = 0; y < h; y++)
    {
        for (u32 x = 0; x < w; x++)
        {
            u32 c = at(x, y);
            u32 n = at(x, y-1), s = at(x, y+1), wst = at(x-1, y), e = at(x+1, y);

            // A neighbour whose alpha doesn't match centre's may carry a
            // matte/backing colour that was never meant to be seen - see
            // TextureSharpenAndSaturate's copy of this fix for why.
            int alphaC = channel(c, 24);
            if (std::abs(channel(n, 24) - alphaC) > (int)kColorTolerance) n = (c & 0x00FFFFFF) | (n & 0xFF000000);
            if (std::abs(channel(s, 24) - alphaC) > (int)kColorTolerance) s = (c & 0x00FFFFFF) | (s & 0xFF000000);
            if (std::abs(channel(wst, 24) - alphaC) > (int)kColorTolerance) wst = (c & 0x00FFFFFF) | (wst & 0xFF000000);
            if (std::abs(channel(e, 24) - alphaC) > (int)kColorTolerance) e = (c & 0x00FFFFFF) | (e & 0xFF000000);

            // Local contrast, in the same 0-255 units as the channels
            // themselves: the largest single-channel gap between this
            // pixel and its neighbour average. A thin text stroke on a
            // flat background produces a large gap here; a soft
            // gradient produces a small one.
            // Local contrast, in luma (not per-channel) - see the fused
            // TextureSharpenAndSaturate's copy of this comment for why:
            // per-channel sharpening lets R/G/B overshoot by different
            // amounts at a hard edge, which shows up as colour fringing
            // rather than brightness fringing.
            auto luma = [&](u32 px) -> int
            {
                return (76*channel(px, 0) + 150*channel(px, 8) + 29*channel(px, 16)) >> 8;
            };
            int lumaC = luma(c);
            int lumaAvg = (luma(n) + luma(s) + luma(wst) + luma(e)) / 4;
            int edgeMag = std::abs(lumaC - lumaAvg);
            float edgeFactor = smoothstepf(4.0f, 28.0f, (float)edgeMag);
            float effectiveStrength = strength * (0.33f + edgeFactor * (1.5f - 0.33f));

            int lumaLo = std::min(lumaC, std::min(luma(n), std::min(luma(s), std::min(luma(wst), luma(e)))));
            int lumaHi = std::max(lumaC, std::max(luma(n), std::max(luma(s), std::max(luma(wst), luma(e)))));
            int lumaSharpened = std::clamp(lumaC + (int)((float)(lumaC - lumaAvg) * effectiveStrength), lumaLo, lumaHi);
            int delta = lumaSharpened - lumaC;

            u32 out = 0;
            for (int shift = 0; shift < 24; shift += 8) // R, G, B - not alpha (shift 24)
                out |= clampByte(channel(c, shift) + delta) << shift;
            out |= c & 0xFF000000; // alpha passed through unchanged

            dst[y * w + x] = out;
        }
    }
}

// Very light, luma-preserving saturation boost - pushes each channel
// away from the pixel's own brightness (luma) by a small factor, so a
// grey/desaturated pixel (channels already close to luma) barely moves
// while a strongly-coloured pixel gets a gentle nudge further from
// grey. Preserving luma (rather than just multiplying channels) is what
// keeps this from also brightening or darkening the image - only the
// colourfulness changes. In-place: unlike the two passes above, this
// never reads a neighbouring pixel, so there's no read/write ordering
// hazard to worry about.
inline void ApplySaturationBoost(u32* buf, u32 w, u32 h, float factor)
{
    auto channel = [](u32 c, int shift) -> int { return (int)((c >> shift) & 0xFF); };
    // Native RGB6A5 range (0-63) - see TextureSharpenAndSaturate's copy
    // of this comment.
    auto clampByte = [](float v) -> u32 { return (u32)std::clamp((int)(v + 0.5f), 0, 63); };

    for (u32 i = 0; i < w * h; i++)
    {
        u32 c = buf[i];
        float r = (float)channel(c, 0), g = (float)channel(c, 8), b = (float)channel(c, 16);
        float luma = 0.299f*r + 0.587f*g + 0.114f*b;

        u32 out = clampByte(luma + (r - luma) * factor)
                | (clampByte(luma + (g - luma) * factor) << 8)
                | (clampByte(luma + (b - luma) * factor) << 16)
                | (c & 0xFF000000); // alpha untouched

        buf[i] = out;
    }
}

// Combined sharpen+saturation single pass: same math as TextureSharpen
// followed by ApplySaturationBoost, but applies the saturation step to
// each pixel immediately after computing its sharpened value instead of
// making a second full read/write pass over the whole buffer. Halves
// memory traffic for the post-upscale finishing step (this is the part
// that actually runs per cache-miss texture - see GetTexture) with a
// byte-for-byte identical result, since the two passes never touch each
// other's inputs (saturation is per-pixel, sharpen already finished
// reading src's neighbours before dst[i] is written).
inline void TextureSharpenAndSaturate(const u32* src, u32 w, u32 h, u32* dst,
                                       float sharpenStrength, float satFactor)
{
    auto channel = [](u32 c, int shift) -> int { return (int)((c >> shift) & 0xFF); };
    // DecodingBuffer (this function's input) is native RGB6A5: 6-bit
    // RGB (0-63), packed one per byte. clampByte must match that
    // range, not the full 0-255 a byte can hold - 3DRenderFS.glsl
    // always divides CurTexture by (63,63,63,31), so any RGB channel
    // pushed above 63 here reads back out-of-range once normalized
    // for rendering (seen as colour distortion right at the
    // high-contrast edges where the sharpen delta is largest).
    auto clampByte = [](int v) -> u32 { return (u32)std::clamp(v, 0, 63); };
    const float invRange = 1.0f / (28.0f - 4.0f);
    auto smoothstepf = [invRange](float lo, float x) -> float
    {
        float t = std::clamp((x - lo) * invRange, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    // Fixed-point (Q12, i.e. scaled by 4096) rewrite of the per-pixel
    // math. Real hardware testing showed the branch-free rewrite above
    // this comment made ~no measurable difference on the actual
    // problem: the cost isn't the neighbour-fetch bounds checks, it's
    // the scalar float work itself (float<->int conversions, float
    // multiplies, a division) running on every single one of up to 16x
    // more pixels than the upscale step touches. Two things make an
    // integer rewrite possible here without touching the visible
    // result in any meaningful way:
    //  1. edgeMag (the max per-channel |pixel - neighbour average|) is
    //     mathematically bounded to 0-255 - it's a difference of two
    //     values already in that range. That means edgeFactor/
    //     effectiveStrength, which depend on edgeMag and the (per-call
    //     constant) sharpenStrength, only ever take 256 distinct
    //     values for this whole call. Precomputing all 256 up front
    //     (still using the exact same float smoothstep formula, just
    //     256 times instead of millions of times) turns the per-pixel
    //     work into a table lookup instead of a float computation.
    //  2. The luma weights (0.299/0.587/0.114) round to 76/150/29 out
    //     of 256 with sub-1/255 error - standard practice, invisible.
    // Net effect: the per-pixel loop below does zero floating-point
    // arithmetic. The only visible cost is up to ~1-unit-of-255
    // rounding differences versus the float version (fixed-point
    // truncation vs float round-to-nearest) - not something any eye
    // will pick out of a texture, in exchange for removing the actual
    // per-pixel bottleneck the profiler pointed at.
    int32_t effStrengthQ12[256];
    for (int e = 0; e <= 255; e++)
    {
        float edgeFactor = smoothstepf(4.0f, (float)e);
        float effectiveStrength = sharpenStrength * (0.33f + edgeFactor * (1.5f - 0.33f));
        effStrengthQ12[e] = (int32_t)std::lround(effectiveStrength * 4096.0f);
    }
    const int32_t satFactorQ12 = (int32_t)std::lround(satFactor * 4096.0f);

    // Shared per-pixel math (sharpen + saturate), given the pixel and
    // its 4 already-fetched neighbours. Used by both the branch-free
    // interior loop and the single-pixel border cases below, so the two
    // paths are guaranteed to produce identical output - only how the
    // neighbours are fetched differs.
    auto processPixel = [&](u32 c, u32 n, u32 s, u32 wst, u32 e) -> u32
    {
        int cc[3], nn[3], ss[3], ww[3], ee[3];
        for (int i = 0; i < 3; i++)
        {
            int shift = i * 8;
            cc[i] = channel(c, shift);
            nn[i] = channel(n, shift);
            ss[i] = channel(s, shift);
            ww[i] = channel(wst, shift);
            ee[i] = channel(e, shift);
        }

        // A neighbour whose alpha doesn't match centre's is on the far
        // side of a transparency boundary - its RGB may be a matte/
        // backing colour that was never meant to be seen (e.g. a pure
        // green fill behind an alpha-edged texture), not real image
        // content. Falling back to centre's own colour for it keeps
        // that matte colour out of the luma average/edge-detect below,
        // the same fix 2DBGUpscaleFS.glsl already applies for its
        // (binary on/off) transparency case.
        int alphaC = channel(c, 24);
        if (std::abs(channel(n, 24) - alphaC) > (int)kColorTolerance) { nn[0] = cc[0]; nn[1] = cc[1]; nn[2] = cc[2]; }
        if (std::abs(channel(s, 24) - alphaC) > (int)kColorTolerance) { ss[0] = cc[0]; ss[1] = cc[1]; ss[2] = cc[2]; }
        if (std::abs(channel(wst, 24) - alphaC) > (int)kColorTolerance) { ww[0] = cc[0]; ww[1] = cc[1]; ww[2] = cc[2]; }
        if (std::abs(channel(e, 24) - alphaC) > (int)kColorTolerance) { ee[0] = cc[0]; ee[1] = cc[1]; ee[2] = cc[2]; }

        // Sharpen in luma only, then apply the SAME delta to every
        // channel below - sharpening R/G/B independently let each
        // channel overshoot by a different amount at a high-contrast
        // edge (thick black outlines against saturated colour), which
        // reads as an actual colour shift (commonly green) rather than
        // the intended brightness fringing. A uniform luma delta keeps
        // hue intact while still sharpening.
        auto luma = [](int r, int g, int b) -> int { return (76*r + 150*g + 29*b) >> 8; };
        int lumaC = luma(cc[0], cc[1], cc[2]);
        int lumaN = luma(nn[0], nn[1], nn[2]);
        int lumaS = luma(ss[0], ss[1], ss[2]);
        int lumaW = luma(ww[0], ww[1], ww[2]);
        int lumaE = luma(ee[0], ee[1], ee[2]);
        int lumaAvg = (lumaN + lumaS + lumaW + lumaE) >> 2;

        int edgeMag = std::abs(lumaC - lumaAvg);
        int32_t strQ12 = effStrengthQ12[edgeMag];

        int lumaLo = std::min(lumaC, std::min(lumaN, std::min(lumaS, std::min(lumaW, lumaE))));
        int lumaHi = std::max(lumaC, std::max(lumaN, std::max(lumaS, std::max(lumaW, lumaE))));
        int lumaSharpened = std::clamp(lumaC + (((lumaC - lumaAvg) * strQ12) >> 12), lumaLo, lumaHi);
        int delta = lumaSharpened - lumaC;

        u32 sharpened = 0;
        for (int i = 0; i < 3; i++)
            sharpened |= clampByte(cc[i] + delta) << (i * 8);
        sharpened |= c & 0xFF000000;

        int r = channel(sharpened, 0), g = channel(sharpened, 8), b = channel(sharpened, 16);
        int satLuma = lumaSharpened;
        return clampByte(satLuma + ((r - satLuma) * satFactorQ12 >> 12))
             | (clampByte(satLuma + ((g - satLuma) * satFactorQ12 >> 12)) << 8)
             | (clampByte(satLuma + ((b - satLuma) * satFactorQ12 >> 12)) << 16)
             | (sharpened & 0xFF000000);
    };

    ParallelForRows(h, [&](u32 yStart, u32 yEnd)
    {
        for (u32 y = yStart; y < yEnd; y++)
        {
            // Row pointers resolved ONCE per row (clamped at the top/
            // bottom image edges here) instead of via a branchy at()
            // call for every single pixel - the old version paid two
            // bounds-check branches per neighbour fetch, 5 fetches per
            // pixel, on every pixel of the whole upscaled image. That's
            // the biggest chunk of why this pass was ~8x slower than
            // the upscale step for the same pixel count: this is a
            // genuinely heavier per-pixel algorithm (edge detection +
            // adaptive strength + saturation, all in float), and it was
            // also paying needless branch/bounds-check overhead on top
            // of that on every single one of those pixels.
            const u32* rowC = src + (size_t)y * w;
            // NOTE on border neighbours: the original at(x,y) clamped
            // via unsigned underflow for the *top/left* directions only
            // - at(x-1,y) for x=0 computes x-1 as UINT32_MAX, which its
            // own "if (x >= w) x = w-1" check then snaps to the *last*
            // column rather than back to column 0; same for at(x,y-1)
            // at row 0, which wraps to the last row. The *bottom/right*
            // directions don't underflow (x+1 == w, y+1 == h are just
            // plain positive values), so they hit that same clamp
            // branch normally and land on themselves (last column/row),
            // no wraparound. Replicating this exact (slightly odd,
            // asymmetric) behaviour here so this rewrite's output
            // matches the original pixel-for-pixel - this is a very
            // minor visual quirk on the outermost border pixels, not
            // something to silently "fix" as a side effect of a
            // performance change.
            const u32* rowN = src + (size_t)(y > 0 ? y - 1 : h - 1) * w;
            const u32* rowS = src + (size_t)(y + 1 < h ? y + 1 : h - 1) * w;
            u32* dstRow = dst + (size_t)y * w;

            if (w >= 2)
            {
                // Left border pixel (x=0): west neighbour wraps to the
                // last column (see NOTE above).
                dstRow[0] = processPixel(rowC[0], rowN[0], rowS[0], rowC[w-1], rowC[1]);

                // Interior: no bounds checks at all, direct indexing.
                for (u32 x = 1; x + 1 < w; x++)
                {
                    dstRow[x] = processPixel(rowC[x], rowN[x], rowS[x], rowC[x-1], rowC[x+1]);
                }

                // Right border pixel (x=w-1): east neighbour clamps to
                // w-1 in the original (x+1 == w hits the same ">= w"
                // branch normally, no underflow involved here).
                u32 last = w - 1;
                dstRow[last] = processPixel(rowC[last], rowN[last], rowS[last], rowC[last-1], rowC[last]);
            }
            else // w == 1: single column - x-1 underflows/wraps to itself (0), x+1 clamps to itself too
            {
                dstRow[0] = processPixel(rowC[0], rowN[0], rowS[0], rowC[0], rowC[0]);
            }
        }
    });
}



inline u32 TextureWidth(u32 texparam)
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
        {
            u64 liveBuckets = 0;
            for (u32 i = 0; i < 8; i++)
                for (u32 j = 0; j < 8; j++)
                    if (!TexArrays[i][j].empty())
                        liveBuckets++;
            IxraniumProfiler::Get().LiveBucketsInUse.store(liveBuckets, std::memory_order_relaxed);
        }
        IxraniumProfiler::Get().OnFrame(IxraniumTexUpscaleEnabled.load(std::memory_order_relaxed), Cache.size());
        UpscalesThisFrame = 0;
        CurrentFrame++;

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
            IxraniumProfiler::Get().CacheHits.fetch_add(1, std::memory_order_relaxed);
            it->second.LastUsedFrame = CurrentFrame;
            textureHandle = it->second.Texture.TextureID;
            layer = it->second.Texture.Layer;
            helper = &it->second.LastVariant;
            return;
        }
        IxraniumProfiler::Get().CacheMisses.fetch_add(1, std::memory_order_relaxed);

        u32 widthLog2 = (texParam >> 20) & 0x7;
        u32 heightLog2 = (texParam >> 23) & 0x7;
        u32 width = 8 << widthLog2;
        u32 height = 8 << heightLog2;

        u32 addr = (texParam & 0xFFFF) * 8;

        TexCacheEntry entry = {0};
        entry.LastUsedFrame = CurrentFrame;

        entry.TextureRAMStart[0] = addr;
        entry.WidthLog2 = widthLog2;
        entry.HeightLog2 = heightLog2;

        {
        IxraniumProfiler::Timer decodeTimer(IxraniumProfiler::Get().DecodeNs);
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
        } // decodeTimer scope

        for (int i = 0; i < 2; i++)
        {
            if (entry.TextureRAMSize[i])
                entry.TextureHash[i] = MaskedHash(GPU.VRAMFlat_Texture, sizeof(GPU.VRAMFlat_Texture),
                    entry.TextureRAMStart[i], entry.TextureRAMSize[i]);
        }
        if (entry.TexPalSize)
            entry.TexPalHash = MaskedHash(GPU.VRAMFlat_TexPal, sizeof(GPU.VRAMFlat_TexPal),
                entry.TexPalStart, entry.TexPalSize);

        // Content-based cache lookup: entry.TextureHash/TexPalHash are a
        // hash of the raw VRAM bytes this texture decodes from, which
        // is deterministic (same VRAM bytes -> same decode -> same
        // upscale result). Sprites that get re-uploaded to a *different*
        // VRAM address every frame (double-buffering, animation-frame
        // cycling) get a different `key`/cache-miss above every time,
        // even though their actual pixel content repeats - without this,
        // the full 4x upscale+sharpen+saturate pipeline (the expensive
        // part) reruns every single frame for those sprites. This skips
        // that CPU work whenever the content has been seen before,
        // regardless of which VRAM address it's sitting at right now.
        u32 uploadW = width, uploadH = height;
        u32* uploadData = DecodingBuffer;
        bool upscaleOn = IxraniumTexUpscaleEnabled.load(std::memory_order_relaxed);
        u64 contentKey = 0;
        bool gotFromContentCache = false;

        if (upscaleOn)
        {
            contentKey = entry.TextureHash[0] ^ (entry.TextureHash[1] * 0x9E3779B97F4A7C15ULL)
                       ^ (entry.TexPalHash * 0xC2B2AE3D27D4EB4FULL) ^ ((u64)fmt << 56);

            auto cit = UpscaleResultCache.find(contentKey);
            if (cit != UpscaleResultCache.end())
            {
                uploadW = width * 4;
                uploadH = height * 4;
                uploadData = cit->second.data();
                gotFromContentCache = true;
                IxraniumProfiler::Get().ContentCacheHits.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                IxraniumProfiler::Get().ContentCacheMisses.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Per-frame budget check (see kMaxUpscalesPerFrame's comment) -
        // content-cache hits above don't touch this budget at all
        // (they're already cheap, just a lookup), only genuine full
        // pipeline runs count against it. Now that the GPU path below
        // exists, a "full pipeline run" costs the CPU almost nothing
        // when the GPU path is available (the CPU only decodes at
        // native res and issues a handful of GL calls) - this budget
        // matters far more for the CPU-fallback case (compute mode, or
        // shader compile failure).
        bool overBudget = upscaleOn && !gotFromContentCache
                        && UpscalesThisFrame >= kMaxUpscalesPerFrame;

        // Whether this texture needs the full upscale+sharpen+saturate
        // treatment at all (as opposed to being served from the content
        // cache, or shown at native resolution because upscaling is off
        // or this frame's budget is already spent). Only sets
        // uploadW/uploadH here - the actual pixel production (GPU
        // shader, or CPU fallback) happens further down, AFTER storage
        // for this texture has been allocated, because the GPU path
        // needs to know which array/layer to render into.
        bool needsFullPipeline = upscaleOn && !gotFromContentCache && !overBudget;
        if (needsFullPipeline)
        {
            uploadW = width * 4;
            uploadH = height * 4;
        }

        auto& texArrays = TexArrays[widthLog2][heightLog2];
        auto& freeTextures = FreeTextures[widthLog2][heightLog2];

        if (freeTextures.size() == 0)
        {
            texArrays.resize(texArrays.size()+1);
            TexHandleT& array = texArrays[texArrays.size()-1];

            // Floor of 8 layers regardless of the byte budget above:
            // with Ixranium upscaling on, uploadW*uploadH is up to 16x
            // the native texel area (4x per axis), so the byte-budget
            // division alone can shrink this to just 1-2 layers for
            // mid/large textures. That empties the free-layer pool 16x
            // faster than at native res, which means this whole
            // GenerateTexture() branch - a synchronous glTexImage3D
            // allocation of a brand new GPU texture array - fires far
            // more often. That GL call is genuinely expensive (driver-
            // side VRAM allocation), and it runs on the render thread,
            // so it shows up as frame-time stalls with CPU usage that
            // looks fine (the thread is blocked waiting on the driver,
            // not doing CPU work) - exactly the "CPU 37%, FPS still low"
            // symptom. Keeping a minimum of 8 layers per array cuts how
            // often this path is hit for upscaled textures, at the cost
            // of a bit more VRAM held per array (bounded: still capped
            // at 64 layers max, same as before).
            u32 layers = std::min<u32>((8*1024*1024) / (uploadW*uploadH*4), 64);

            // allocate new array texture
            //printf("allocating new layer set for %d %d %d %d\n", uploadW, uploadH, texArrays.size()-1, array.ImageDescriptor);
            {
                IxraniumProfiler::Timer allocTimer(IxraniumProfiler::Get().GLAllocNs);
                array = TexLoader.GenerateTexture(uploadW, uploadH, layers);
            }
            IxraniumProfiler::Get().NewGLArrayAllocs.fetch_add(1, std::memory_order_relaxed);
            {
                u64 bytes = (u64)uploadW * uploadH * 4 * layers;
                IxraniumProfiler::Get().NewArrayBytesThisWindow.fetch_add(bytes, std::memory_order_relaxed);
                IxraniumProfiler::Get().TotalArrayBytesEverAllocated.fetch_add(bytes, std::memory_order_relaxed);
            }

            for (u32 i = 0; i < layers; i++)
            {
                freeTextures.push_back(TexArrayEntry{array, i});
            }
        }

        TexArrayEntry storagePlace = freeTextures[freeTextures.size()-1];
        freeTextures.pop_back();

        entry.Texture = storagePlace;

        // Now that we know exactly which array+layer this texture lands
        // in, actually produce its pixels:
        //  - content-cache hit: just re-upload the previously-computed
        //    CPU buffer (unchanged from before).
        //  - needs the full pipeline: try the GPU shader first (see
        //    GPUUpscaleSharpenSaturate's comment) - it renders straight
        //    into this array+layer, no separate CPU upload call needed
        //    at all. Only falls back to the old CPU EagleUpscale4x +
        //    TextureSharpenAndSaturate + upload chain if the GPU path
        //    isn't available (compute mode, or the shader failed to
        //    compile - see its own comment for why that's not silently
        //    papered over).
        //  - neither: native-resolution upload, same as always.
        bool didGPUPipeline = false;
        if (needsFullPipeline)
        {
            UpscalesThisFrame++;

            IxraniumProfiler::Timer upscaleTimer(IxraniumProfiler::Get().UpscaleNs);
            didGPUPipeline = TexLoader.GPUUpscaleSharpenSaturate(
                storagePlace.TextureID, width, height, storagePlace.Layer,
                DecodingBuffer, kSharpenStrength, kSaturationBoost);
        }

        if (didGPUPipeline)
        {
            // Shader wrote the final result directly into
            // storagePlace's layer - nothing left to do. Not populating
            // UpscaleResultCache here: that CPU-side cache existed to
            // skip the *expensive CPU* pipeline for repeat content: now
            // that the pipeline runs on the GPU (cheap), a repeat
            // content-cache miss just means running this same fast GPU
            // pass again, which is an acceptable trade against the
            // complexity of caching GPU-produced results.
        }
        else if (needsFullPipeline)
        {
            // GPU path unavailable (compute mode, or shader compile
            // failed) - fall back to the original CPU pipeline exactly
            // as before.
            //
            // NOTE: the old unconditional printf() here (once per
            // cache-miss texture) was a big FPS cost by itself - a
            // synchronous stdout write firing on every cache-miss.
            // Removed. If you need it back for debugging, guard it
            // behind a build flag, never ship it unconditional here.
            {
                IxraniumProfiler::Timer upscaleTimer(IxraniumProfiler::Get().UpscaleNs);
                EagleUpscale4x(DecodingBuffer, width, height, UpscaleBuffer);
            }

            // Sharpen + saturate combined into one pass over the buffer
            // instead of two (see TextureSharpenAndSaturate) - same
            // output, one less full read/write sweep over up to
            // 4096x4096 pixels per cache-miss texture.
            {
                IxraniumProfiler::Timer sharpenTimer(IxraniumProfiler::Get().SharpenNs);
                TextureSharpenAndSaturate(UpscaleBuffer, uploadW, uploadH, SharpenBuffer,
                                           kSharpenStrength, kSaturationBoost);
            }

            uploadData = SharpenBuffer;

            // Cache this result under its content hash so the next
            // cache-miss with the *same* underlying texture data (just
            // at a different VRAM address/texParam - the sprite
            // double-buffering/animation case) can skip straight to
            // reusing it instead of redoing the whole upscale pipeline.
            // Capped so a game with huge numbers of genuinely unique
            // textures can't grow this unbounded; simple over clever -
            // this is a perf cache, not a correctness requirement, so
            // just clear and start refilling once full.
            if (UpscaleResultCache.size() >= kUpscaleResultCacheCap)
                UpscaleResultCache.clear();
            UpscaleResultCache.emplace(contentKey,
                std::vector<u32>(uploadData, uploadData + (size_t)uploadW * uploadH));

            IxraniumProfiler::Timer uploadTimer(IxraniumProfiler::Get().GLUploadNs);
            TexLoader.UploadTexture(storagePlace.TextureID, uploadW, uploadH, storagePlace.Layer, uploadData);
            IxraniumProfiler::Get().GLUploads.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            // Content-cache hit, or no upscaling needed at all (off, or
            // this frame's budget was already spent) - straightforward
            // upload of uploadData/uploadW/uploadH as already set above.
            IxraniumProfiler::Timer uploadTimer(IxraniumProfiler::Get().GLUploadNs);
            TexLoader.UploadTexture(storagePlace.TextureID, uploadW, uploadH, storagePlace.Layer, uploadData);
            IxraniumProfiler::Get().GLUploads.fetch_add(1, std::memory_order_relaxed);
        }

        //printf("using storage place %d %d | %d %d (%d)\n", uploadW, uploadH, storagePlace.TexArrayIdx, storagePlace.LayerIdx, array.ImageDescriptor);

        textureHandle = storagePlace.TextureID;
        layer = storagePlace.Layer;

        // LRU eviction (see kMaxCacheEntries/LastUsedFrame's comments) -
        // done right before inserting the new entry, so the cache never
        // holds more than kMaxCacheEntries at once. A plain linear scan
        // for the oldest entry rather than a real LRU list/heap: this
        // only runs when the cache is already at its cap (a fairly high
        // bar), so it's an occasional cleanup pass, not a per-texture
        // cost - not worth the extra bookkeeping of a proper intrusive
        // LRU structure for a cap in the hundreds of entries.
        if (Cache.size() >= kMaxCacheEntries)
        {
            auto oldest = Cache.begin();
            for (auto cit = Cache.begin(); cit != Cache.end(); ++cit)
            {
                if (cit->second.LastUsedFrame < oldest->second.LastUsedFrame)
                    oldest = cit;
            }
            FreeTextures[oldest->second.WidthLog2][oldest->second.HeightLog2].push_back(oldest->second.Texture);
            Cache.erase(oldest);
        }

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
        UpscaleResultCache.clear();
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

        // Which frame this entry was last actually used to draw
        // something (see CurrentFrame / kMaxCacheEntries below) - used
        // to evict the least-recently-used entry once the cache holds
        // too many. Without this, an entry only ever gets freed when
        // the game overwrites its VRAM bytes - a texture the game
        // loaded once and never touches again (very common for static
        // sprites/portraits) sits resident forever. With Ixranium's 4x
        // upscale multiplying every such texture's GPU memory footprint
        // 16x, a scene with many one-off unique sprites (a character
        // select screen, say) can accumulate enough permanently-held
        // VRAM over a play session to start thrashing the driver -
        // degrading ALL rendering, not just this texture cache, for the
        // rest of the session, regardless of which screen you're on
        // afterward. That matches exactly what was reported: a
        // permanent slowdown that appeared once sprite-heavy content
        // had been on screen, persisting on every screen after,
        // present in both the CPU and GPU pipeline eras (it's a
        // property of how many *distinct* textures get permanently
        // cached, not of how their pixels get computed).
        u64 LastUsedFrame = 0;
    };
    std::unordered_map<u64, TexCacheEntry> Cache;

    // Cache-entry cap paired with LastUsedFrame above: once Cache holds
    // more than this many entries, GetTexture evicts the least-
    // recently-used one(s) back to FreeTextures before inserting a new
    // one, so total resident (and therefore VRAM-held) texture count
    // has a hard ceiling regardless of how many distinct textures a
    // long play session ends up showing. Deliberately generous (most
    // games won't get anywhere near this many simultaneously *useful*
    // cached textures) - this is a safety net against unbounded growth,
    // not a tight working-set size.
    static constexpr size_t kMaxCacheEntries = 512;
    u64 CurrentFrame = 0;

    // Content-hash -> already-upscaled pixel data (see the lookup in
    // GetTexture). Capped at kUpscaleResultCacheCap entries.
    std::unordered_map<u64, std::vector<u32>> UpscaleResultCache;
    static constexpr size_t kUpscaleResultCacheCap = 256;

    // Per-frame budget for how many textures get the full Ixranium
    // upscale+sharpen pipeline. A scene that suddenly needs many new
    // textures at once (character-select wheel populating, a big fight
    // scene loading in) can otherwise dump dozens of cache-misses into
    // a single frame - the profiler showed 29 misses costing 892ms in
    // one 60-frame window, i.e. that ONE frame briefly needed ~30ms of
    // CPU just for its own share of that, blowing the ~16.6ms/frame
    // budget for 60 FPS well past a single frame. Capping how many get
    // the expensive treatment per frame doesn't reduce the total work,
    // but it spreads a single 30ms+ frame-time spike (a visible stutter)
    // across several frames instead (each comfortably under budget).
    // Overflow textures are shown at native resolution for that frame
    // and retried in full on their next cache-miss - a few frames of a
    // less-upscaled texture during a sudden burst, not a freeze.
    static constexpr u32 kMaxUpscalesPerFrame = 6;
    u32 UpscalesThisFrame = 0;

    TexLoaderT TexLoader;

    std::vector<TexArrayEntry> FreeTextures[8][8];
    std::vector<TexHandleT> TexArrays[8][8];

    u32 DecodingBuffer[1024*1024];
    // Scratch space for the 3x-upscaled copy (see EagleUpscale3x / the
    // IxraniumTexUpscaleEnabled check in GetTexture). Sized for the
    // largest possible DS texture (1024x1024) upscaled 3x on each axis.
    // Scratch space for the 4x-upscaled copy (see EagleUpscale4x / the
    // IxraniumTexUpscaleEnabled check in GetTexture). Sized for the
    // largest possible DS texture (1024x1024) upscaled 4x on each axis.
    u32 UpscaleBuffer[4096*4096];
    // Second scratch buffer for the post-upscale sharpening pass (see
    // TextureSharpen) - needs to be separate from UpscaleBuffer since
    // sharpening reads each pixel's neighbours while writing, which an
    // in-place pass would corrupt (a pixel's neighbour may already have
    // been overwritten with its own sharpened value by the time it's
    // read). Same size as UpscaleBuffer for the same reason.
    u32 SharpenBuffer[4096*4096];
};

}

#endif