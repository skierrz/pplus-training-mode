#pragma once

#include <types.h>

#include "main/settings.h"

// Pure P+ 3.2.1 Stage Options decision logic (presets, visibility, clamping,
// and exact CHR0 frame tables). Compiled into both the extra module and the
// host unit tests (tests/host/tests/test_stage_options.cpp).
namespace tm {
namespace extra {
    enum StagePreset {
        kPresetNone = 0,
        kPresetFod,           // Fountain of Dreams (+ alts on st_dxfountain_ht.rel)
        kPresetDreamLand,     // Dream Land (+ alts on st_dreamland64_ht.rel)
        kPresetSmashville,    // Smashville (+ alts on st_smashville_ht.rel)
        kPresetGreenHill,     // Green Hill Zone (+ alts on st_greenhill_ht.rel)
        kPresetDeadLine,      // Dead Line (+ alts on st_greenhill_ht.rel)
        kPresetLuigisMansion, // Luigi's Mansion base & _R Dry Bones (3600f Platforms)
        kPresetDelfinoSecret, // Delfino's Secret (3600f MAIN-2Phase)
        kPresetYoshisStory,   // Yoshi's Story (1199f Randall)
        kPresetGenericMoving, // Any other stage with animated COLL platforms
        kPresetYoshisIsland,  // Yoshi's Island Brawl (st_crayon_noshyguy_ht.rel, 2880f support clouds)
        kPresetCount
    };

    // Dynamically observed properties of the live loaded stage (no hardcoded
    // srStageKind checks, so alt-stage reskins work automatically and static
    // stages or non-matching alts stay safely disabled).
    struct StageDetectInput {
        bool modFod;              // st_dxfountain_ht loaded
        bool modDreamLand;        // st_dreamland64_ht loaded
        bool modSmashville;       // st_smashville_ht loaded
        bool modGreenHill;        // st_greenhill_ht loaded
        bool modCrayon;           // st_crayon_noshyguy_ht loaded
        bool hasSpout80;          // Ground m_mdlIndex == 80 present
        bool hasSpout81;          // Ground m_mdlIndex == 81 present
        bool hasWhispy2;          // Ground m_mdlIndex == 2 present
        bool hasAshiba80;         // Ground m_mdlIndex == 80 present
        bool hasAshiba81;         // Ground m_mdlIndex == 81 present
        bool hasGhzPendulum;      // Ground m_mdlIndex == 80 has bone "furiko3" or "dai"
        bool hasDeadLinePlats;    // Ground m_mdlIndex == 80 has bone "pCube2"
        bool hasCrayonClouds80;   // Ground m_mdlIndex == 80 has bones "AKumoAMKasure01" & "AKumoAMKasure01r"
        bool hasPlatformLeft;     // A Ground has bone "PlatformLeft" (3600f CHR0)
        bool hasPlatformCenter;   // Same Ground also has bone "PlatformCenter"
        bool hasRandall;          // A Ground has bone "Randall"
        bool hasGenericMovingColl; // At least one animated COLL-bound Ground
    };

    inline int classifyStagePreset(const StageDetectInput& in)
    {
        if (in.modFod && in.hasSpout80 && in.hasSpout81)
        {
            return kPresetFod;
        }
        if (in.modDreamLand && in.hasWhispy2)
        {
            return kPresetDreamLand;
        }
        if (in.modSmashville && in.hasAshiba80)
        {
            return kPresetSmashville;
        }
        if (in.modGreenHill && in.hasGhzPendulum)
        {
            return kPresetGreenHill;
        }
        if (in.modGreenHill && in.hasDeadLinePlats)
        {
            return kPresetDeadLine;
        }
        if (in.modCrayon && in.hasCrayonClouds80)
        {
            return kPresetYoshisIsland;
        }
        if (in.hasPlatformLeft && in.hasPlatformCenter)
        {
            return kPresetDelfinoSecret;
        }
        if (in.hasPlatformLeft)
        {
            return kPresetLuigisMansion;
        }
        if (in.hasRandall)
        {
            return kPresetYoshisStory;
        }
        if (in.hasGenericMovingColl)
        {
            return kPresetGenericMoving;
        }
        return kPresetNone;
    }

    inline bool presetHasHazards(int preset)
    {
        return preset == kPresetFod || preset == kPresetDreamLand || preset == kPresetSmashville
               || preset == kPresetGreenHill || preset == kPresetDeadLine || preset == kPresetYoshisIsland;
    }

    inline bool presetHasPlatforms(int preset)
    {
        return preset == kPresetSmashville || preset == kPresetGreenHill || preset == kPresetDeadLine
               || preset == kPresetLuigisMansion || preset == kPresetDelfinoSecret || preset == kPresetYoshisStory
               || preset == kPresetGenericMoving || preset == kPresetYoshisIsland;
    }

    inline bool presetHasFod(int preset)
    {
        return preset == kPresetFod;
    }

    inline bool presetAnyOption(int preset)
    {
        return preset != kPresetNone && preset < kPresetCount;
    }

    // Highest valid platformMode choice index for `preset` (0 if none).
    inline int presetPlatformMax(int preset)
    {
        switch (preset)
        {
        case kPresetGenericMoving:
            return 1; // Cycle, Freeze
        case kPresetLuigisMansion:
            return 3; // Cycle, Freeze, Split, Combined
        case kPresetDelfinoSecret:
            return 3; // Cycle, Freeze, Tri-Plat, Two-Plat
        case kPresetSmashville:
        case kPresetGreenHill:
        case kPresetDeadLine:
        case kPresetYoshisStory:
        case kPresetYoshisIsland:
            return 4; // Cycle, Freeze, + 3 positions
        default:
            return 0;
        }
    }

    inline int clampPlatformMode(int preset, int mode)
    {
        int hi = presetPlatformMax(preset);
        if (mode < 0 || hi <= 0)
        {
            return 0;
        }
        return mode > hi ? hi : mode;
    }

    inline bool resolveHazardOff(int setting, bool initialHazardOff)
    {
        if (setting == main::kStageHazardOn)
        {
            return false;
        }
        if (setting == main::kStageHazardOff)
        {
            return true;
        }
        return initialHazardOff;
    }

    // True when match exit or stage teardown must write `initialHazardOff` back
    // to gmGlobalModeMelee + 0x29 bit 0x20 so a Stage Options Hazards override
    // does not leak across matches or clobber non-hazard/uninitialized stages.
    inline bool shouldRestoreGlobalHazardOff(bool initialized, int preset, bool initialHazardOff, bool appliedHazardOff)
    {
        return initialized && presetHasHazards(preset) && appliedHazardOff != initialHazardOff;
    }

    // Exact P+ 3.2.1 STDT[20] frames for Fountain of Dreams side platforms
    // (StgDxFountain_Plat_Move, Y_world = 0.375 * frame - 0.75).
    // Returns < 0 for kFodDefault (stage default / no override).
    inline float fodFrameForChoice(int choice)
    {
        switch (choice)
        {
        case main::kFodHidden:
            return 0.0f;  // Y_world = -0.75 (collision disabled)
        case main::kFodLowest:
            return 39.0f; // Y_world = 13.875 (STDT + 124)
        case main::kFodLeftDefault:
            return 43.0f; // Y_world = 15.375 (STDT + 156)
        case main::kFodAverage:
            return 57.0f; // Y_world = 20.625 (STDT + 132)
        case main::kFodRightDefault:
            return 64.0f; // Y_world = 23.25  (STDT + 160)
        case main::kFodHighest:
            return 75.0f; // Y_world = 27.375 (STDT + 128)
        default:
            return -1.0f;
        }
    }

    inline float fodWorldYForFrame(float frame)
    {
        return 0.375f * frame - 0.75f;
    }

    // Target CHR0 frame for fixed platform positions (mode >= 2).
    // Returns < 0 for Cycle (0), Freeze (1), or presets without fixed frames.
    inline float platformTargetFrame(int preset, int mode, bool isHazardOff)
    {
        if (mode < 2)
        {
            return -1.0f;
        }
        switch (preset)
        {
        case kPresetLuigisMansion:
            // Platforms (3600f): f0..1500 Split (X = +-50.86), f1800..3300 Combined (X = +-17.94).
            if (mode == 2) return 0.0f;
            if (mode == 3) return 1800.0f;
            return -1.0f;
        case kPresetDelfinoSecret:
            // MAIN-2Phase (3600f): f0..1320 Tri-Plat (Y = 48.38), f1800..3120 Two-Plat (Y = -1.50).
            if (mode == 2) return 0.0f;
            if (mode == 3) return 2000.0f;
            return -1.0f;
        case kPresetSmashville:
            // StgVillageFuyuuAshiba / _Half (1200f): Center / Left / Right.
            // Center frames calibrated live in P+ 3.2.1 so ashiba world X = 0.0.
            if (mode == 2) return isHazardOff ? 291.0f : 304.0f;
            if (mode == 3) return 600.0f;
            if (mode == 4) return 0.0f;
            return -1.0f;
        case kPresetGreenHill:
            // HazardOn (602f: f0=0 deg, f151=-74.5 deg, f451=+74.5 deg);
            // HazardOff (460f: f0=-46.8 deg, f115=0 deg, f230=+46.8 deg).
            if (mode == 2) return isHazardOff ? 115.0f : 0.0f;
            if (mode == 3) return isHazardOff ? 230.0f : 451.0f;
            if (mode == 4) return isHazardOff ? 0.0f : 151.0f;
            return -1.0f;
        case kPresetDeadLine:
            // waaa (13500f): Close / Far / Mid.
            if (mode == 2) return 0.0f;
            if (mode == 3) return 3000.0f;
            if (mode == 4) return 6000.0f;
            return -1.0f;
        case kPresetYoshisStory:
            // Randall (1199f): Left Out / Right Out / Hidden.
            if (mode == 2) return 0.0f;
            if (mode == 3) return 600.0f;
            if (mode == 4) return 340.0f;
            return -1.0f;
        case kPresetYoshisIsland:
            // timedGhosts / BlarggwichIndicator (2880f, STGYOSHISISLAND.pac BRES[80]/[81]):
            // f1020 Left Cloud Out (X=-101.1, Y=-10.6), f2460 Right Cloud Out (X=+101.1, Y=-10.6),
            // f0 Both Hidden (Y=-177.1).
            if (mode == 2) return 1020.0f;
            if (mode == 3) return 2460.0f;
            if (mode == 4) return 0.0f;
            return -1.0f;
        default:
            return -1.0f;
        }
    }
}
}
