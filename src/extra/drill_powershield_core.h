#pragma once

#include <string.h>
#include <types.h>

// Pure P+ 3.2.1 Powershield and PS-Drop punish detection, grading, and
// telemetry core (no Brawl headers; host-testable on PC).
//
// Ground-truth P+ 3.2.1 references:
// - IC-Basic[23078] = 3 -> 2 active frames (RA-Basic[1] = 0x20000001 decrements
//   3->2 on Frame 1, 2->1 on Frame 2, 1->0 on Frame 3) for both physical PS and
//   0.76x projectile reflection: Source/Injects/Fighter/Constants.asm:121-122,
//   Source/Injects/MoveDef/PowerShield.asm:12-54, RSBE01.TXT:793-796.
// - Frame-2 light-to-hard press (IC-Basic[0] == 1.0): analog light shield on
//   Frame 1 (RA-Basic[1] == 0) + fresh digital hard press on Frame 2 sets
//   RA-Basic[1] = 2 on Frame 2 and 1 on Frame 3, enabling Frame-3 PS/reflect.
// - Physical PS indicator: LA-Bit[96] (0x12000060), PowerShield.asm:130-158.
// - Projectile reflect indicator: RA-Bit[10] (0x2200000A), RSBE01.TXT:1266-1281,
//   PowerShield.asm:56-105 (persists through frame-end, cleared next CIL tick).
// - 5-frame PS-Drop interrupt window: IC-Basic[23079] = 5 -> RA-Basic[3]
//   (0x20000003) set on physical PS Guard_Damage entry; entering Guard_Off
//   (0x1C) while RA-Basic[3] > 0 opens ground attack/special/item/Guard_On,
//   bypassing Guard_Off's 15-frame lag: Constants.asm:123-124,
//   PowerShield.asm:108-128, Misc.asm:561-563.
// - Fresh analog L/R press: LA-Basic[77] == 1 (0x1000004D), ButtonPresses.asm:433-457.
namespace tm {
namespace extra {
    enum PsConst {
        kPsStWait = 0x00,
        kPsStGuardOn = 0x1A,
        kPsStGuard = 0x1B,
        kPsStGuardOff = 0x1C,
        kPsStGuardDamage = 0x1D,
        kPsStAttackFirst = 0x24,
        kPsStAttackLast = 0x32,
        kPsStCatchFirst = 0x34,
        kPsStCatchLast = 0x38,
        kPsStDamageFirst = 0x43,
        kPsStDamageLast = 0x46,
        kPsStItemThrowFirst = 0x9B,
        kPsStItemSwingLast = 0xA5,
        kPsStDead = 0xBD,
        kPsStSpecialN = 0x112,
        kPsStFinal = 0x116,

        kPsVarRaActive = 0x20000001,  // RA-Basic[1]
        kPsVarRaDrop = 0x20000003,    // RA-Basic[3]
        kPsVarRaReflect = 0x2200000A, // RA-Bit[10]
        kPsVarLaPsBit = 0x12000060,   // LA-Bit[96]
        kPsVarLaAnalogLr = 0x1000004D,// LA-Basic[77]

        kPsBaseWindow = 2,
        kPsLightToHardWindow = 3,
        kPsEarlyMax = 10,
        kPsLateMax = 10,
        kPsGuardOffLag = 15,
        kPsMaxEvents = 8,
        kPsNoFrame = -1
    };

    enum PsOutcome {
        kPsNone = 0,
        kPsPhysical = 1,
        kPsReflect = 2,
        kPsMissBuffered = 3,
        kPsMissEarly = 4,
        kPsMissLate = 5
    };

    enum PsDropPhase {
        kPsDropIdle = 0,
        kPsDropInGuardDamage = 1,
        kPsDropInGuard = 2,
        kPsDropInGuardOff = 3
    };

    struct PsStatusEvent {
        s16 status;
        s16 prev;
    };

    struct PsInput {
        s32 frame;
        s16 status;
        s16 curHitlag;
        s16 prevHitlag;
        s32 raBasic1;      // RA-Basic[1]: active PS frames left (2, 1, 0)
        s32 raBasic3;      // RA-Basic[3]: 5-frame PS-drop window left
        s32 laBasic77;     // LA-Basic[77]: 1 = fresh analog L/R press (>= 0.3)
        u8 laBit96;        // LA-Bit[96]: physical PS sound/indicator bit
        u8 raBit10;        // RA-Bit[10]: projectile reflect latch
        u8 guardButton;    // 1 if digital shield button (kButtonGuard) is held
        u8 shieldReceived; // 1 if shield stun was received on this frame
        u8 eventCount;
        PsStatusEvent events[kPsMaxEvents];
    };

    struct PsStats {
        u32 attempts;
        u32 physicalHits;
        u32 reflectHits;
        u32 rePsHits;
        u32 lightToHardHits;
        u32 earlyMisses;
        u32 lateMisses;
        u32 bufferedMisses;
        u32 psDropCancels;
        u32 streak;
        u32 bestStreak;
    };

    struct PsResult {
        u8 outcome;          // PsOutcome
        u8 rePs;             // 1 if Guard_On entered from Guard_Off (0x1C)
        u8 lightToHard;      // 1 if Frame-2 light-to-hard press enabled window
        u8 psDropUsed;       // 1 if PS-Drop cancelled Guard_Off within 5f window
        u8 psDropChainedRePs;// 1 if PS-Drop entered Guard_On and landed a Re-PS
        s16 windowFrame;     // 1, 2, or 3 (if lightToHard)
        s16 offsetFrames;    // 0 on hit/buffered, -K on early, +K on late
        s16 psDropActFrame;  // 1-based frame of PS-Drop action out of Guard_Damage end
        s32 eventFrame;      // tracker frame of last outcome
    };

    struct PsSlot {
        // Guard / PS window transient state.
        bool guardArmed;
        bool guardRePs;
        bool lightToHard;
        u16 guardFrames;
        u8 windowEnd;
        s32 f1RaBasic1; // RA-Basic[1] observed at Guard_On Frame 1 (-1 if unknown)

        // Late-miss transient state.
        bool lateMissArmed;
        s32 lateMissHitFrame;
        u8 prevGuardButton;

        // 5-frame PS-Drop transient state.
        u8 psDropPhase; // PsDropPhase
        bool psDropWindowOpen;
        bool psDropPendingRePs;
        s32 psDropDamageEndFrame;
        u16 psDropGuardOffFrames;
        s32 prevRaBasic3;

        PsStats stats;
        PsResult last;
    };

    inline bool isPsDropActionStatus(int s)
    {
        return (s >= kPsStAttackFirst && s <= kPsStAttackLast)
               || (s >= kPsStCatchFirst && s <= kPsStCatchLast)
               || (s >= kPsStItemThrowFirst && s <= kPsStItemSwingLast)
               || (s >= kPsStSpecialN && s < kPsStFinal)
               || s == kPsStGuardOn;
    }

    inline bool isPsDamageStatus(int s)
    {
        return s >= kPsStDamageFirst && s <= kPsStDamageLast;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    // Clears in-flight tracking without erasing cumulative session stats
    // (called on savestate load or fighter change).
    inline void psResetTransient(PsSlot& st)
    {
        memset(&st, 0, reinterpret_cast<char*>(&st.stats) - reinterpret_cast<char*>(&st));
        st.windowEnd = kPsBaseWindow;
        st.f1RaBasic1 = -1;
        st.lateMissHitFrame = kPsNoFrame;
        st.psDropDamageEndFrame = kPsNoFrame;
    }

    inline void psResetAll(PsSlot& st)
    {
        memset(&st, 0, sizeof(st));
        st.last.eventFrame = kPsNoFrame;
        psResetTransient(st);
    }

    inline void recordPsSuccess(PsSlot& st, u8 outcome, s32 frame, s16 winFrame, bool rePs, bool lth)
    {
        st.stats.attempts++;
        if (outcome == kPsPhysical)
        {
            st.stats.physicalHits++;
        }
        else if (outcome == kPsReflect)
        {
            st.stats.reflectHits++;
        }
        if (rePs)
        {
            st.stats.rePsHits++;
        }
        if (lth)
        {
            st.stats.lightToHardHits++;
        }
        st.stats.streak++;
        if (st.stats.streak > st.stats.bestStreak)
        {
            st.stats.bestStreak = st.stats.streak;
        }

        bool chained = st.psDropPendingRePs && rePs;
        st.psDropPendingRePs = false;

        st.last.outcome = outcome;
        st.last.rePs = rePs ? 1 : 0;
        st.last.lightToHard = lth ? 1 : 0;
        st.last.windowFrame = winFrame;
        st.last.offsetFrames = 0;
        st.last.eventFrame = frame;
        if (chained)
        {
            st.last.psDropChainedRePs = 1;
        }
        else
        {
            st.last.psDropUsed = 0;
            st.last.psDropChainedRePs = 0;
            st.last.psDropActFrame = 0;
        }
    }

    inline void recordPsMiss(PsSlot& st, u8 outcome, s32 frame, s16 winFrame, s16 offset)
    {
        st.stats.attempts++;
        if (outcome == kPsMissBuffered)
        {
            st.stats.bufferedMisses++;
        }
        else if (outcome == kPsMissEarly)
        {
            st.stats.earlyMisses++;
        }
        else if (outcome == kPsMissLate)
        {
            st.stats.lateMisses++;
        }
        st.stats.streak = 0;
        st.psDropPendingRePs = false;

        st.last.outcome = outcome;
        st.last.rePs = 0;
        st.last.lightToHard = st.lightToHard ? 1 : 0;
        st.last.psDropUsed = 0;
        st.last.psDropChainedRePs = 0;
        st.last.windowFrame = winFrame;
        st.last.offsetFrames = offset;
        st.last.psDropActFrame = 0;
        st.last.eventFrame = frame;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline void stepPowershield(const PsInput& in, PsSlot& st)
    {
        const s32 f = in.frame;
        const bool inHitlag = in.prevHitlag > 0 && in.curHitlag > 0;

        // P+ RSBE01.TXT:793-796: PS counter does not decrement in hitlag.
        if (st.guardArmed && !inHitlag)
        {
            st.guardFrames++;
        }
        if (st.psDropPhase == kPsDropInGuardOff && !inHitlag)
        {
            st.psDropGuardOffFrames++;
            if (st.psDropGuardOffFrames >= kPsGuardOffLag)
            {
                st.psDropPhase = kPsDropIdle;
                st.psDropWindowOpen = false;
            }
        }

        const bool freshShieldPress = (in.guardButton != 0 && st.prevGuardButton == 0) || (in.laBasic77 == 1);

        // Check Late Miss before processing new hits on this frame (1..10f after f_hit,
        // including during hitlag).
        if (st.lateMissArmed)
        {
            s32 delta = f - st.lateMissHitFrame;
            if (delta > kPsLateMax)
            {
                st.lateMissArmed = false;
                st.lateMissHitFrame = kPsNoFrame;
            }
            else if (delta >= 1 && freshShieldPress)
            {
                st.lightToHard = false;
                recordPsMiss(st, kPsMissLate, f, 0, static_cast<s16>(delta));
                st.lateMissArmed = false;
                st.lateMissHitFrame = kPsNoFrame;
            }
        }

        const int evCount = in.eventCount <= kPsMaxEvents ? static_cast<int>(in.eventCount) : kPsMaxEvents;
        for (int k = 0; k < evCount; k++)
        {
            const PsStatusEvent& e = in.events[k];
            const int s = e.status;
            const int prev = e.prev;

            // --- PS-Drop 5-frame window tracking ---
            if (st.psDropPhase == kPsDropInGuardDamage && prev == kPsStGuardDamage)
            {
                if (s == kPsStGuardOff)
                {
                    bool open = (in.raBasic3 > 0) || (st.prevRaBasic3 > 0);
                    if (open)
                    {
                        st.psDropPhase = kPsDropInGuardOff;
                        st.psDropWindowOpen = true;
                        st.psDropDamageEndFrame = f;
                        st.psDropGuardOffFrames = 0;
                    }
                    else
                    {
                        st.psDropPhase = kPsDropIdle;
                        st.psDropWindowOpen = false;
                    }
                }
                else if (s == kPsStGuard)
                {
                    st.psDropPhase = kPsDropInGuard;
                    st.psDropDamageEndFrame = f;
                    st.psDropWindowOpen = (in.raBasic3 > 0) || (st.prevRaBasic3 > 0);
                }
                else if (s != kPsStGuardDamage)
                {
                    st.psDropPhase = kPsDropIdle;
                    st.psDropWindowOpen = false;
                }
            }
            else if (st.psDropPhase == kPsDropInGuard)
            {
                if (s == kPsStGuardOff && prev == kPsStGuard)
                {
                    bool open = (in.raBasic3 > 0) || (st.prevRaBasic3 > 0);
                    if (open)
                    {
                        st.psDropPhase = kPsDropInGuardOff;
                        st.psDropWindowOpen = true;
                        st.psDropGuardOffFrames = 0;
                    }
                    else
                    {
                        st.psDropPhase = kPsDropIdle;
                        st.psDropWindowOpen = false;
                    }
                }
                else if (s != kPsStGuard)
                {
                    st.psDropPhase = kPsDropIdle;
                    st.psDropWindowOpen = false;
                }
            }
            else if (st.psDropPhase == kPsDropInGuardOff)
            {
                if (prev == kPsStGuardOff && st.psDropWindowOpen && isPsDropActionStatus(s)
                    && st.psDropGuardOffFrames < kPsGuardOffLag)
                {
                    s32 endF = st.psDropDamageEndFrame != kPsNoFrame ? st.psDropDamageEndFrame : f;
                    s16 actF = static_cast<s16>(f - endF + 1);
                    if (actF < 1)
                    {
                        actF = 1;
                    }
                    st.stats.psDropCancels++;
                    st.last.psDropUsed = 1;
                    st.last.psDropActFrame = actF;
                    st.psDropPendingRePs = (s == kPsStGuardOn);
                    st.psDropPhase = kPsDropIdle;
                    st.psDropWindowOpen = false;
                }
                else if (s != kPsStGuardOff)
                {
                    st.psDropPhase = kPsDropIdle;
                    st.psDropWindowOpen = false;
                }
            }

            // --- Guard_On / Guard_Damage / Damage transitions ---
            if (s == kPsStGuardOn)
            {
                st.guardArmed = true;
                st.guardFrames = 1;
                st.guardRePs = (prev == kPsStGuardOff);
                st.lightToHard = false;
                st.windowEnd = kPsBaseWindow;
                st.f1RaBasic1 = -1;
                st.lateMissArmed = false;
                st.lateMissHitFrame = kPsNoFrame;
            }
            else if (s == kPsStGuardDamage && (prev == kPsStGuardOn || prev == kPsStGuard) && st.guardArmed)
            {
                if (in.laBit96 != 0)
                {
                    // Physical powershield! Check light-to-hard on Frame 2 or 3.
                    if (st.guardFrames >= 3 || (st.guardFrames == 2 && st.f1RaBasic1 == 0))
                    {
                        st.lightToHard = true;
                        st.windowEnd = kPsLightToHardWindow;
                    }
                    recordPsSuccess(st, kPsPhysical, f, static_cast<s16>(st.guardFrames),
                                    st.guardRePs, st.lightToHard);
                    st.psDropPhase = kPsDropInGuardDamage;
                    st.psDropWindowOpen = true;
                    st.psDropDamageEndFrame = kPsNoFrame;
                    st.psDropGuardOffFrames = 0;
                }
                else
                {
                    int earlyCap = st.lightToHard ? (kPsEarlyMax + 1) : kPsEarlyMax;
                    if (st.guardFrames <= st.windowEnd)
                    {
                        recordPsMiss(st, kPsMissBuffered, f, static_cast<s16>(st.guardFrames), 0);
                    }
                    else if (static_cast<int>(st.guardFrames) <= earlyCap)
                    {
                        s16 off = static_cast<s16>(-(static_cast<int>(st.guardFrames) - static_cast<int>(st.windowEnd)));
                        recordPsMiss(st, kPsMissEarly, f, static_cast<s16>(st.guardFrames), off);
                    }
                    else
                    {
                        st.psDropPendingRePs = false;
                    }
                }
                st.guardArmed = false;
                st.lateMissArmed = false;
                st.lateMissHitFrame = kPsNoFrame;
            }
            else if (st.guardArmed && s != kPsStGuard && s != kPsStGuardOn)
            {
                st.guardArmed = false;
                st.psDropPendingRePs = false;
            }

            if (isPsDamageStatus(s) && in.shieldReceived == 0)
            {
                st.lateMissArmed = true;
                st.lateMissHitFrame = f;
                st.psDropPhase = kPsDropIdle;
                st.psDropWindowOpen = false;
                st.psDropPendingRePs = false;
            }
            else if (s == kPsStDead)
            {
                st.guardArmed = false;
                st.lateMissArmed = false;
                st.lateMissHitFrame = kPsNoFrame;
                st.psDropPhase = kPsDropIdle;
                st.psDropWindowOpen = false;
                st.psDropPendingRePs = false;
            }
        }

        // Frame-end Guard_On / Guard checks: Frame-2 light-to-hard detection and
        // projectile reflection via RA-Bit[10] (verified frame-end reliable).
        if (st.guardArmed)
        {
            if (in.status == kPsStGuardOn || in.status == kPsStGuard)
            {
                if (st.guardFrames == 1 && st.f1RaBasic1 < 0)
                {
                    st.f1RaBasic1 = in.raBasic1;
                }
                else if (st.guardFrames == 2 && st.f1RaBasic1 == 0 && in.raBasic1 >= 2)
                {
                    st.lightToHard = true;
                    st.windowEnd = kPsLightToHardWindow;
                }
                else if (st.guardFrames == 3 && in.raBasic1 >= 1)
                {
                    st.lightToHard = true;
                    st.windowEnd = kPsLightToHardWindow;
                }

                if (in.raBit10 != 0)
                {
                    if (st.guardFrames >= 3 || (st.guardFrames == 2 && st.f1RaBasic1 == 0))
                    {
                        st.lightToHard = true;
                        st.windowEnd = kPsLightToHardWindow;
                    }
                    recordPsSuccess(st, kPsReflect, f, static_cast<s16>(st.guardFrames),
                                    st.guardRePs, st.lightToHard);
                    st.guardArmed = false;
                }
            }
            else
            {
                st.guardArmed = false;
                st.psDropPendingRePs = false;
            }
        }

        // Disarm PS-Drop in Guard once RA-Basic[3] expires.
        if (st.psDropPhase == kPsDropInGuard && in.status == kPsStGuard && in.raBasic3 <= 0)
        {
            st.psDropPhase = kPsDropIdle;
            st.psDropWindowOpen = false;
        }
        else if (st.psDropPhase == kPsDropInGuardDamage && in.status != kPsStGuardDamage)
        {
            st.psDropPhase = kPsDropIdle;
            st.psDropWindowOpen = false;
        }
        else if (st.psDropPhase == kPsDropInGuardOff && in.status != kPsStGuardOff)
        {
            st.psDropPhase = kPsDropIdle;
            st.psDropWindowOpen = false;
        }

        // Track RA-Basic[3] while inside guard statuses so same-frame transitions
        // out of Guard_Damage / Guard into Guard_Off -> Action still know it was > 0.
        if (in.status == kPsStGuardDamage || in.status == kPsStGuard || in.status == kPsStGuardOff)
        {
            st.prevRaBasic3 = in.raBasic3;
        }
        else
        {
            st.prevRaBasic3 = 0;
        }

        st.prevGuardButton = in.guardButton;
    }
}
}
