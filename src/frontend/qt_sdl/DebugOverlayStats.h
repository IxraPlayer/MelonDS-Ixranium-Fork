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

#ifndef DEBUGOVERLAYSTATS_H
#define DEBUGOVERLAYSTATS_H

// Lightweight, dependency-free sampler for the debug overlay (Settings >
// Debug settings > assign a hotkey to toggle it). Deliberately does NOT
// pull in any extra libraries - just OS-native calls already available
// wherever melonDS builds (Linux/BSD via /proc, Windows via psapi, macOS
// via mach/task_info).
//
// Numbers are process-level (this melonDS instance), not system-wide,
// since that's what's actually useful when hunting a slowdown.

#include <cstdio>
#include <cstring>

#if defined(__linux__) || defined(__FreeBSD__)
    #include <unistd.h>
#elif defined(_WIN32)
    #include <windows.h>
    #include <psapi.h>
#elif defined(__APPLE__)
    #include <mach/mach.h>
#endif

class DebugOverlayStats
{
public:
    // ramMB: resident memory of this process, in megabytes.
    // cpuPercent: CPU usage of this process since the previous call,
    // normalized to 0-100 for a single core (so a fully-pegged 4-thread
    // emu core can read >100, which is expected/useful, not a bug).
    // Returns false if a given stat couldn't be read on this platform;
    // callers should show "N/A" for it rather than a stale/zero number.
    static bool Sample(double& ramMB, double& cpuPercent)
    {
        bool gotRAM = SampleRAM(ramMB);
        bool gotCPU = SampleCPU(cpuPercent);
        return gotRAM || gotCPU;
    }

private:
#if defined(__linux__) || defined(__FreeBSD__)
    static bool SampleRAM(double& ramMB)
    {
        FILE* f = fopen("/proc/self/status", "r");
        if (!f) return false;

        char line[256];
        long rssKB = -1;
        while (fgets(line, sizeof(line), f))
        {
            if (strncmp(line, "VmRSS:", 6) == 0)
            {
                sscanf(line + 6, "%ld", &rssKB);
                break;
            }
        }
        fclose(f);

        if (rssKB < 0) return false;
        ramMB = rssKB / 1024.0;
        return true;
    }

    static bool SampleCPU(double& cpuPercent)
    {
        // utime+stime (in clock ticks) from /proc/self/stat, deltaed
        // against the previous sample and against wall-clock time.
        FILE* f = fopen("/proc/self/stat", "r");
        if (!f) return false;

        // Skip pid + comm (comm can contain spaces/parens) + state, then
        // utime is field 14, stime is field 15 (1-indexed).
        char* line = nullptr;
        size_t len = 0;
        ssize_t n = getline(&line, &len, f);
        fclose(f);
        if (n <= 0) { free(line); return false; }

        char* close = strrchr(line, ')');
        if (!close) { free(line); return false; }

        long utime = 0, stime = 0;
        // fields after "comm)" start at field 3 (state); utime is the
        // absolute field 14, stime is field 15 (per proc(5)).
        int field = 2;
        char* p = close + 1;
        while (*p && field < 15)
        {
            while (*p == ' ') p++;
            char* start = p;
            while (*p && *p != ' ') p++;
            field++;
            if (field == 14) utime = atol(start);
            else if (field == 15) { stime = atol(start); break; }
        }
        free(line);

        static long prevTicks = -1;
        static double prevWallMs = -1.0;

        long ticksPerSec = sysconf(_SC_CLK_TCK);
        long totalTicks = utime + stime;

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double wallMs = ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;

        if (prevTicks < 0)
        {
            prevTicks = totalTicks;
            prevWallMs = wallMs;
            cpuPercent = 0.0;
            return true;
        }

        double deltaTicksMs = (totalTicks - prevTicks) * (1000.0 / ticksPerSec);
        double deltaWallMs = wallMs - prevWallMs;

        cpuPercent = (deltaWallMs > 0.0) ? (100.0 * deltaTicksMs / deltaWallMs) : 0.0;

        prevTicks = totalTicks;
        prevWallMs = wallMs;
        return true;
    }
#elif defined(_WIN32)
    static bool SampleRAM(double& ramMB)
    {
        PROCESS_MEMORY_COUNTERS pmc;
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            return false;

        ramMB = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
        return true;
    }

    static bool SampleCPU(double& cpuPercent)
    {
        FILETIME creation, exitT, kernel, user;
        if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitT, &kernel, &user))
            return false;

        auto toU64 = [](const FILETIME& ft) -> unsigned long long
        {
            return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        };

        unsigned long long totalTicks = toU64(kernel) + toU64(user); // 100ns units

        FILETIME nowFT;
        GetSystemTimeAsFileTime(&nowFT);
        unsigned long long nowTicks = toU64(nowFT);

        static unsigned long long prevTotal = 0;
        static unsigned long long prevNow = 0;

        if (prevNow == 0)
        {
            prevTotal = totalTicks;
            prevNow = nowTicks;
            cpuPercent = 0.0;
            return true;
        }

        unsigned long long deltaProc = totalTicks - prevTotal;
        unsigned long long deltaWall = nowTicks - prevNow;

        cpuPercent = (deltaWall > 0) ? (100.0 * (double)deltaProc / (double)deltaWall) : 0.0;

        prevTotal = totalTicks;
        prevNow = nowTicks;
        return true;
    }
#elif defined(__APPLE__)
    static bool SampleRAM(double& ramMB)
    {
        task_basic_info_data_t info;
        mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
            return false;

        ramMB = info.resident_size / (1024.0 * 1024.0);
        return true;
    }

    static bool SampleCPU(double& cpuPercent)
    {
        // Not implemented for macOS yet - thread_info() based CPU sampling
        // needs per-thread iteration that isn't worth the complexity here.
        // The overlay shows "N/A" for CPU on this platform until someone
        // wants to add it properly.
        (void)cpuPercent;
        return false;
    }
#else
    static bool SampleRAM(double&) { return false; }
    static bool SampleCPU(double&) { return false; }
#endif
};

#endif // DEBUGOVERLAYSTATS_H
