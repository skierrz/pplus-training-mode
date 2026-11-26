#pragma once

#include <string.h>
#include <types.h>

#include "extra/stage_geom_math.h"

// Pure host-testable Combo-Break Rush challenge engine: stage-aware 3D target
// capsule spawning, live attack hitbox intersection, combo multiplier x1..x8,
// chain decay timer, Timed (30s/60s) and Free Practice modes (zero Brawl headers).
namespace tm {
namespace extra {
    enum ComboBreakMode {
        kRushOff = 0,
        kRushTimed30 = 1,
        kRushTimed60 = 2,
        kRushFree = 3,
        kRushModeCount = 4
    };

    enum {
        kRushMaxTargets = 4,
        kRushActiveSlots = 3,
        kRushMaxMult = 8,
        kRushChainMaxFrames = 210, // 3.5 seconds at 60fps
        kRushTimed30Frames = 1800, // 30 seconds
        kRushTimed60Frames = 3600, // 60 seconds
        kRushBurstFrames = 14,
        kRushRespawnDelay = 10,
        kRushBestSlot = 17, // drills::kDrillComboBreakRush (drills::Session::best[17])
        kRushDebugMagic = 0x544D4252u, // "TMBR"
        kRushDebugVersion = 1,
        kRushCmdIdle = 0xFF,
        kRushCmdSnapTarget = 0xFE
    };

    inline u8 rushNextMode(u8 mode)
    {
        if (mode == kRushFree) return static_cast<u8>(kRushTimed30);
        if (mode == kRushTimed30) return static_cast<u8>(kRushTimed60);
        return static_cast<u8>(kRushFree);
    }

    struct RushTarget {
        float x;
        float y;
        float radius;
        u8 active;     // 0 = empty, 1 = live, 2 = breaking burst
        u8 tier;       // 0 = cyan floor/arc, 1 = emerald platform, 2 = gold bonus
        u8 animFrames; // spawn pop (0..12) when live, burst countdown (14..0) when breaking
        u8 cooldown;   // frames before respawning when empty
    };

    struct ComboBreakState {
        u8 mode;       // ComboBreakMode
        u8 finished;   // 1 when timed countdown reached 0
        u8 multiplier; // 1..kRushMaxMult
        u8 hitEvent;   // transient SFX cue: 0=none, 1=break, 2=milestone, 3=finish
        u32 score;
        u32 bestScore;
        u16 streak;
        u16 bestStreak;
        u16 breaks;
        u16 chainFrames;
        u16 timerFrames;
        u16 tickCount;
        u32 rng;
        RushTarget targets[kRushMaxTargets];
    };

    // RAM debug block exposed for live Dolphin verification ("TMBR").
    struct ComboBreakDebugBlock {
        u32 magic;          // 0x00 "TMBR"
        u32 version;        // 0x04 = 1
        u8 mode;            // 0x08 active ComboBreakMode
        u8 cmdMode;         // 0x09 0xFF idle; 0..3 switch mode; 0xFE snap target 0 in front of P1
        u8 active;          // 0x0A 1 if mode != kRushOff
        u8 finished;        // 0x0B 1 if timed round ended
        u8 multiplier;      // 0x0C 1..8
        u8 activeTargets;   // 0x0D count of live targets
        u8 stagePlatforms;  // 0x0E live platform count from StageGeometry
        u8 shapeOk;         // 0x0F collShapeSupported()
        u32 score;          // 0x10
        u32 bestScore;      // 0x14
        u16 streak;         // 0x18
        u16 breaks;         // 0x1A
        u16 chainFrames;    // 0x1C
        u16 timerFrames;    // 0x1E
        u32 draw3dCalls;    // 0x20
        u32 draw2dCalls;    // 0x24
        float targetX[kRushMaxTargets]; // 0x28
        float targetY[kRushMaxTargets]; // 0x38
        float targetR[kRushMaxTargets]; // 0x48
    };

#if defined(__MWERKS__) && !defined(TM_COMBO_BREAK_CORE_IMPL)
    void rushInit(ComboBreakState& st, u8 mode, u32 priorBest, u32 seed);
    bool rushHitboxHitsTarget(const float pos1[3], const float pos2[3], float hitRadius, const RushTarget& t);
    bool rushSpawnOne(ComboBreakState& st, int slot, const StageGeometry& g, float fighterX, float fighterY);
    u8 rushCountLive(const ComboBreakState& st);
    void rushBreakTarget(ComboBreakState& st, int slot);
    void rushStepFrame(ComboBreakState& st, const StageGeometry& g, float fighterX, float fighterY);
#else
#ifdef __MWERKS__
#define TM_RUSH_CORE_FN
#pragma force_active on
#pragma dont_inline on
#else
#define TM_RUSH_CORE_FN inline
#endif
    static inline float rushRandRange(u32& rng, float lo, float hi)
    {
        u32 x = rng != 0u ? rng : 0x6D2B79F5u;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rng = x;
        return lo + static_cast<float>(x & 0xFFFFu) * (hi - lo) * (1.0f / 65536.0f);
    }

    TM_RUSH_CORE_FN void rushInit(ComboBreakState& st, u8 mode, u32 priorBest, u32 seed)
    {
        memset(&st, 0, sizeof(st));
        st.mode = mode < kRushModeCount ? mode : static_cast<u8>(kRushOff);
        st.multiplier = 1;
        st.bestScore = priorBest;
        st.rng = seed != 0u ? seed : 0xC0B0B2E4u;
        st.timerFrames = st.mode == kRushTimed30 ? kRushTimed30Frames
                                                 : (st.mode == kRushTimed60 ? kRushTimed60Frames : 0);
    }

    // Exact 2D segment-to-circle intersection for swept hitbox capsule vs target sphere.
    TM_RUSH_CORE_FN bool rushHitboxHitsTarget(const float pos1[3], const float pos2[3], float hitRadius,
                                              const RushTarget& t)
    {
        if (t.active != 1 || hitRadius <= 0.0f)
        {
            return false;
        }
        float sumR = hitRadius + t.radius;
        float z = pos1[2] < 0.0f ? -pos1[2] : pos1[2];
        if (z > sumR + 10.0f)
        {
            return false;
        }
        float abx = pos1[0] - pos2[0];
        float aby = pos1[1] - pos2[1];
        float len2 = abx * abx + aby * aby;
        float u = 0.0f;
        if (len2 > 1e-5f)
        {
            u = ((t.x - pos2[0]) * abx + (t.y - pos2[1]) * aby) / len2;
            if (u < 0.0f) u = 0.0f;
            else if (u > 1.0f) u = 1.0f;
        }
        float dx = t.x - (pos2[0] + u * abx);
        float dy = t.y - (pos2[1] + u * aby);
        return (dx * dx + dy * dy) <= (sumR * sumR);
    }

    // Spawns one target capsule inside reachable live StageGeometry airspace/platform arcs.
    TM_RUSH_CORE_FN bool rushSpawnOne(ComboBreakState& st, int slot, const StageGeometry& g, float fighterX,
                                      float fighterY)
    {
        if (slot < 0 || slot >= kRushMaxTargets)
        {
            return false;
        }
        float floorL = g.floor.valid ? g.floor.leftX : -68.0f;
        float floorR = g.floor.valid ? g.floor.rightX : 68.0f;
        float floorY = g.floor.valid ? g.floor.centerY : 0.0f;
        float safeL = floorL + 10.0f;
        float safeR = floorR - 10.0f;
        if (g.blast.valid)
        {
            if (g.blast.left + 28.0f > safeL) safeL = g.blast.left + 28.0f;
            if (g.blast.right - 28.0f < safeR) safeR = g.blast.right - 28.0f;
        }
        if (safeR <= safeL + 8.0f) { safeL = floorL; safeR = floorR; }
        float maxY = floorY + 58.0f;
        if (g.blast.valid && g.blast.top - 32.0f < maxY) maxY = g.blast.top - 32.0f;

        int lim = g.platformCount > kMaxStagePlatforms ? kMaxStagePlatforms : g.platformCount;
        float cx = 0.5f * (safeL + safeR);
        float cy = floorY + 14.0f;
        u8 tier = 0;

        for (int attempt = 0; attempt < 8; attempt++)
        {
            float r0 = rushRandRange(st.rng, 0.0f, 1.0f);
            u32 roll = st.rng;
            int pi = (lim > 0 && (roll % 5u) < 3u) ? static_cast<int>((roll >> 3) % static_cast<u32>(lim)) : -1;
            if (pi >= 0 && g.platforms[pi].valid && g.platforms[pi].rightX > g.platforms[pi].leftX + 4.0f)
            {
                const StagePlatform& p = g.platforms[pi];
                float pl = p.leftX + 3.0f; if (pl < safeL) pl = safeL;
                float pr = p.rightX - 3.0f; if (pr > safeR) pr = safeR;
                if (pr < pl) { pl = safeL; pr = safeR; }
                cx = pl + r0 * (pr - pl);
                if (cx < safeL) cx = safeL;
                if (cx > safeR) cx = safeR;
                cy = rushRandRange(st.rng, p.y + 7.5f, p.y + 16.5f);
                tier = ((roll >> 8) % 5u == 0u) ? 2 : 1;
            }
            else
            {
                cx = safeL + r0 * (safeR - safeL);
                cy = rushRandRange(st.rng, floorY + 8.5f, floorY + 26.0f);
                tier = ((roll >> 8) % 6u == 0u) ? 2 : 0;
            }
            if (cy < floorY + 7.5f) cy = floorY + 7.5f;
            if (cy > maxY) cy = maxY;

            bool clear = true;
            for (int k = 0; k < kRushActiveSlots && clear; k++)
            {
                if (k != slot && st.targets[k].active == 1)
                {
                    float dx = cx - st.targets[k].x, dy = cy - st.targets[k].y;
                    if (dx * dx + dy * dy < 324.0f) clear = false;
                }
            }
            if (clear && attempt < 6)
            {
                float dx = cx - fighterX, dy = cy - (fighterY + 8.0f);
                if (dx * dx + dy * dy < 121.0f) clear = false;
            }
            if (clear) break;
        }

        RushTarget& t = st.targets[slot];
        t.x = cx;
        t.y = cy;
        t.radius = (tier == 2) ? 4.0f : 4.8f;
        t.active = 1;
        t.tier = tier;
        t.animFrames = 0;
        t.cooldown = 0;
        return true;
    }

    TM_RUSH_CORE_FN u8 rushCountLive(const ComboBreakState& st)
    {
        u8 n = 0;
        for (int i = 0; i < kRushMaxTargets; i++)
        {
            if (st.targets[i].active == 1) n++;
        }
        return n;
    }

    TM_RUSH_CORE_FN void rushBreakTarget(ComboBreakState& st, int slot)
    {
        if (slot < 0 || slot >= kRushMaxTargets || st.targets[slot].active != 1 || st.finished)
        {
            return;
        }
        RushTarget& t = st.targets[slot];
        st.multiplier = (st.chainFrames > 0)
                            ? static_cast<u8>(st.multiplier < kRushMaxMult ? st.multiplier + 1 : kRushMaxMult)
                            : 1;
        st.chainFrames = kRushChainMaxFrames;
        st.score += (t.tier == 2 ? 150u : 100u) * static_cast<u32>(st.multiplier);
        if (st.score > st.bestScore) st.bestScore = st.score;
        if (++st.streak > st.bestStreak) st.bestStreak = st.streak;
        st.breaks++;
        t.active = 2;
        t.animFrames = kRushBurstFrames;
        t.cooldown = 0;
        u8 ev = (st.multiplier == 4 || st.multiplier == kRushMaxMult) ? 2 : 1;
        if (ev > st.hitEvent) st.hitEvent = ev;
    }

    TM_RUSH_CORE_FN void rushStepFrame(ComboBreakState& st, const StageGeometry& g, float fighterX, float fighterY)
    {
        st.hitEvent = 0;
        if (st.mode == kRushOff) return;
        st.tickCount++;
        if (st.finished) return;

        if (st.chainFrames > 0 && --st.chainFrames == 0)
        {
            st.multiplier = 1;
            st.streak = 0;
        }
        if ((st.mode == kRushTimed30 || st.mode == kRushTimed60) && st.timerFrames > 0 && --st.timerFrames == 0)
        {
            st.finished = 1;
            st.hitEvent = 3;
            return;
        }

        for (int i = 0; i < kRushActiveSlots; i++)
        {
            RushTarget& t = st.targets[i];
            if (t.active == 1)
            {
                if (t.animFrames < 20) t.animFrames++;
            }
            else if (t.active == 2)
            {
                if (t.animFrames > 0 && --t.animFrames == 0)
                {
                    t.active = 0;
                    t.cooldown = kRushRespawnDelay;
                }
            }
            else if (t.cooldown > 0)
            {
                t.cooldown--;
            }
            else
            {
                rushSpawnOne(st, i, g, fighterX, fighterY);
            }
        }
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#pragma force_active reset
#endif
#undef TM_RUSH_CORE_FN
#endif
}
}
