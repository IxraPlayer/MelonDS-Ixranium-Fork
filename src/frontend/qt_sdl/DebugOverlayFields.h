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

#ifndef DEBUGOVERLAYFIELDS_H
#define DEBUGOVERLAYFIELDS_H

// Every line the in-game debug overlay can show, one bit each. Settings >
// Debug settings lists one checkbox per field (built straight off this
// table, so adding a field here is the only step needed to also get it a
// checkbox) and stores the resulting bitmask in the global config key
// "DebugOverlay.Fields". ScreenPanel::updateDebugOverlayText() reads the
// same mask and only builds the lines that are turned on.
enum DebugOverlayField
{
    DBGOV_FPS = 0,
    DBGOV_FrameTime,
    DBGOV_TargetFPS,
    DBGOV_CPU,
    DBGOV_RAM,
    DBGOV_Resolution,
    DBGOV_Renderer,
    DBGOV_Console,
    DBGOV_FastForward,
    DBGOV_AudioSync,
    DBGOV_CartLabel,
    DBGOV_InstanceID,
    DBGOV_COUNT
};

struct DebugOverlayFieldInfo
{
    DebugOverlayField id;
    const char* label; // shown next to the checkbox in Debug settings
};

static const DebugOverlayFieldInfo kDebugOverlayFields[DBGOV_COUNT] =
{
    { DBGOV_FPS,         "FPS" },
    { DBGOV_FrameTime,   "Frame time" },
    { DBGOV_TargetFPS,   "Target FPS" },
    { DBGOV_CPU,         "CPU usage" },
    { DBGOV_RAM,         "RAM usage" },
    { DBGOV_Resolution,  "Screen resolution" },
    { DBGOV_Renderer,    "Renderer (OpenGL / software)" },
    { DBGOV_Console,     "Console type" },
    { DBGOV_FastForward, "Fast-forward state" },
    { DBGOV_AudioSync,   "Audio sync state" },
    { DBGOV_CartLabel,   "Loaded cartridge" },
    { DBGOV_InstanceID,  "Instance ID" },
};

// Matches the overlay's old fixed FPS/CPU/RAM/Resolution set, so upgrading
// doesn't silently change what existing users see until they open Debug
// settings and opt into the new fields themselves.
static const unsigned int kDebugOverlayDefaultMask =
    (1u << DBGOV_FPS) | (1u << DBGOV_CPU) | (1u << DBGOV_RAM) | (1u << DBGOV_Resolution);

#endif // DEBUGOVERLAYFIELDS_H
