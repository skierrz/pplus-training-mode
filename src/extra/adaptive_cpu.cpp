#include "extra/adaptive_cpu.h"

#include <string.h>

#include "extra/adaptive_cpu_core.h"
#include "extra/net_alloc.h"
#include "extra/stage_geom.h"
#include "main/cpu/actions.h"
#include "main/cpu/cpu_settings.h"
#include "main/game/fighters.h"
#include "main/settings.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    enum {
        kAdaptiveDebugMagic = 0x544D4143, // "TMAC"
        kNeutralBiasCooldown = 28
    };

    static const float kNeutralEngageRange = 65.0f;

    // RAM-inspectable telemetry block logged once at extra attach.
    struct AdaptiveDebugBlock {
        u32 magic;          // 0x00: kAdaptiveDebugMagic ("TMAC")
        u32 mode;           // 0x04: cpu.options.adaptiveMode (0=Off, 1=Track, 2=Adaptive)
        u32 samples;        // 0x08: total tracker samples recorded
        u32 p1CurrentMov;   // 0x0C: P1 current MovementTracker category (0..15)
        u32 p1RpsCount;     // 0x10: P1 total RPS events pushed into PatternManager
        u32 p1DominantRps;  // 0x14: P1 dominant RPS (0=Null, 1=Attack, 2=Grab, 3=Defend)
        float p1Confidence; // 0x18: P1 PatternManager confidence [0.0, 1.0]
        float p1AttackProb; // 0x1C: P1 MovementTracker approxChance(kMovAttack)
        float p1ShieldProb; // 0x20: P1 MovementTracker approxChance(kMovShield)
        float p1GrabProb;   // 0x24: P1 MovementTracker approxChance(kMovGrab)
        u32 biasAction;     // 0x28: last AdaptiveBiasAction selected (0..5)
        u32 biasTriggers;   // 0x2C: total adaptive bias activations
    };

    static MovementTracker* s_trackers = NULL;
    static PatternManager s_patterns;
    static int s_prevStatus[game::kMaxFighters];
    static u8 s_prevRps[game::kMaxFighters];
    static u16 s_neutralCooldown[game::kMaxFighters];
    static u32 s_mixCounter[game::kMaxFighters];
    static int s_primaryOpponent[game::kMaxFighters];
    static int s_telemetryPlayer = 0;
    static AdaptiveDebugBlock s_debug;

    static u8 clampU8Float(float v)
    {
        if (v <= 0.0f)
        {
            return 0;
        }
        if (v >= 255.0f)
        {
            return 255;
        }
        return static_cast<u8>(v);
    }

    static float absF(float v)
    {
        return v < 0.0f ? -v : v;
    }

    void adaptiveCpuReset()
    {
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            if (s_trackers != NULL)
            {
                s_trackers[i].reset();
            }
            s_prevStatus[i] = -1;
            s_prevRps[i] = kRpsNull;
            s_neutralCooldown[i] = 0;
            s_mixCounter[i] = 0;
            s_primaryOpponent[i] = -1;
        }
        s_patterns.clearAll();
        s_telemetryPlayer = 0;
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kAdaptiveDebugMagic;
    }

    void adaptiveCpuAttach()
    {
        freeNetPtr(s_trackers);
        s_trackers = allocNetArray<MovementTracker>(game::kMaxFighters);
        adaptiveCpuReset();
        TM_LOG("adaptive: debug block at %08x\n", &s_debug);
    }

    void adaptiveCpuDetach()
    {
        adaptiveCpuReset();
        freeNetPtr(s_trackers);
    }

    static void updateDebugTelemetry(u8 mode)
    {
        int p = s_telemetryPlayer;
        if (p < 0 || p >= game::kMaxFighters)
        {
            p = 0;
        }
        s_debug.magic = kAdaptiveDebugMagic;
        s_debug.mode = mode;
        s_debug.samples = s_trackers != NULL ? s_trackers[p].sampleCount() : 0;
        s_debug.p1CurrentMov = s_trackers != NULL ? s_trackers[p].currentMov() : 0;
        s_debug.p1RpsCount = s_patterns.count(p);
        s_debug.p1DominantRps = s_patterns.dominantRps(p);
        s_debug.p1Confidence = s_patterns.confidence(p);
        s_debug.p1AttackProb = s_trackers != NULL ? s_trackers[p].approxChance(kMovAttack) : 0.0f;
        s_debug.p1ShieldProb = s_trackers != NULL ? s_trackers[p].approxChance(kMovShield) : 0.0f;
        s_debug.p1GrabProb = s_trackers != NULL ? s_trackers[p].approxChance(kMovGrab) : 0.0f;
    }

    void adaptiveCpuFrame()
    {
        u8 mode = main::settings().cpu.options.adaptiveMode;
        if (mode == cpu::kAdaptiveOff || s_trackers == NULL)
        {
            s_debug.mode = cpu::kAdaptiveOff;
            return;
        }

        game::FighterRef refs[game::kMaxFighters];
        game::FighterState states[game::kMaxFighters];
        bool valid[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        int humanSlot = -1;

        for (int i = 0; i < game::kMaxFighters; i++)
        {
            valid[i] = false;
            if (s_neutralCooldown[i] > 0)
            {
                s_neutralCooldown[i]--;
            }
        }

        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p < 0 || p >= game::kMaxFighters)
            {
                continue;
            }
            if (game::readState(refs[i], states[p]))
            {
                valid[p] = true;
                if (!refs[i].cpu && humanSlot < 0)
                {
                    humanSlot = p;
                }
            }
        }

        if (humanSlot >= 0)
        {
            s_telemetryPlayer = humanSlot;
        }

        bool telemetryDirty = (s_debug.mode != mode);

        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p < 0 || p >= game::kMaxFighters || !valid[p])
            {
                continue;
            }
            const game::FighterState& st = states[p];
            if (game::outOfPlay(st.status, true))
            {
                s_prevStatus[p] = st.status;
                s_prevRps[p] = kRpsNull;
                continue;
            }

            // Advance duration timer; only perform rayCheckGround + updateData
            // on an actual status transition or 254-frame timer rollover.
            bool rolled = s_trackers[p].incrementTimer();
            bool transitioned = (st.status != s_prevStatus[p]);
            if (!transitioned && !rolled)
            {
                continue;
            }

            // Pick nearest active opponent (preferring a human fighter for CPUs).
            int opp = -1;
            float bestDist = 100000.0f;
            for (int pass = 0; pass < 2 && opp < 0; pass++)
            {
                for (int j = 0; j < n; j++)
                {
                    int op = refs[j].playerNo;
                    if (op < 0 || op >= game::kMaxFighters || op == p || !valid[op])
                    {
                        continue;
                    }
                    if (pass == 0 && refs[i].cpu && refs[j].cpu)
                    {
                        continue;
                    }
                    float d = absF(states[op].x - st.x);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        opp = op;
                    }
                }
            }
            s_primaryOpponent[p] = opp;

            u8 ownAtk = clampU8Float(s_trackers[p].cachedChance(kMovAttack) * 200.0f);
            u8 ownShd = clampU8Float(s_trackers[p].cachedChance(kMovShield) * 200.0f);

            if (transitioned)
            {
                s_trackers[p].trackStatus(st.status);
                u8 rps = statusToRps(st.status);
                if (rps != kRpsNull && rps != s_prevRps[p])
                {
                    s_patterns.pushNew(p, rps);
                }
                s_prevRps[p] = rps;
                s_prevStatus[p] = st.status;
            }

            float hitY = 0.0f;
            bool hasFloor = rayCheckGround(st.x, st.y + 4.0f, 120.0f, hitY, true);
            u8 yDist = hasFloor ? clampU8Float(st.y - hitY) : 255;
            u8 xPosByte = clampU8Float(st.x + 128.0f);
            u8 xDistByte = opp >= 0 ? clampU8Float(bestDist) : 255;
            s8 oppMov = opp >= 0 ? static_cast<s8>(s_trackers[opp].currentMov()) : static_cast<s8>(-1);

            s_trackers[p].updateData(yDist, xPosByte, xDistByte, ownAtk, ownShd, oppMov);
            if (p == s_telemetryPlayer)
            {
                telemetryDirty = true;
            }
        }

        if (telemetryDirty)
        {
            updateDebugTelemetry(mode);
        }
    }

    int adaptiveCpuBias(int cpuPlayerNo, int list, bool inCounter, float opponentDist, bool commit)
    {
        u8 mode = main::settings().cpu.options.adaptiveMode;
        if (mode != cpu::kAdaptiveActive || cpuPlayerNo < 0 || cpuPlayerNo >= game::kMaxFighters)
        {
            return -1;
        }
        int opp = s_primaryOpponent[cpuPlayerNo];
        if (opp < 0 || opp >= game::kMaxFighters || opp == cpuPlayerNo)
        {
            opp = s_telemetryPlayer;
        }
        if (opp < 0 || opp >= game::kMaxFighters || opp == cpuPlayerNo)
        {
            return -1;
        }

        u8 dom = s_patterns.dominantRps(opp);
        float conf = s_patterns.confidence(opp);
        return selectAdaptiveAction(mode, dom, conf, list, inCounter, false, opponentDist, kNeutralEngageRange, commit,
                                    s_mixCounter[cpuPlayerNo], s_neutralCooldown[cpuPlayerNo], kNeutralBiasCooldown,
                                    s_debug.biasAction, s_debug.biasTriggers);
    }
}
}
