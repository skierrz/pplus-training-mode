#pragma once

#include <string.h>
#include <types.h>

#include "extra/challenge_common_core.h"

// Pure host-testable Project+ 3.2.1 Situational Reaction Lab & Universal
// Techchase Escape backend cores.
// Verified P+ 3.2.1 status and subaction IDs (BrawlHeaders ft/fighter.h):
//  - Passive (0x60), Passive_Fb (0x61; subactions 0xBF/0xC0).
//  - Down (0x4A..0x4E), Down_Stand* (0x51..0x53), Damage_Fall (0x49).
//  - Capture_* (0x3D..0x42), Catch_Cut (0x3B), Throw (0x3C).
//  - Cliff_Wait (0x75) -> Cliff_Attack..Cliff_Jump2 (0x76..0x7A) or Fall (0x0E).
namespace tm {
namespace extra {
    enum ReactionCueKind {
        kCueNone = 0,
        kCueTechNeutral = 1,
        kCueTechRollLeft = 2,
        kCueTechRollRight = 3,
        kCueGetupAttack = 4,
        kCueGetupRoll = 5,
        kCueLedgeOption = 6,
        kCueAttackStartup = 7
    };

    enum TechchaseEscapeTool {
        kTcToolNone = 0,
        kTcToolSpotdodge = 1,
        kTcToolRoll = 2,
        kTcToolJump = 3,
        kTcToolDash = 4,
        kTcToolShield = 5,
        kTcToolAttack = 6,
        kTcToolAirdodge = 7
    };

    enum TechchasePhase {
        kTcPhaseIdle = 0,
        kTcPhaseVulnerable = 1,
        kTcPhaseActionableWait = 2
    };

    enum ReactionTechchaseConst {
        kReactionRingCap = 64,
        kReactionTimeoutFrames = 90,
        kTcEscapeTimeoutFrames = 30,

        kRtStDash = 0x03,
        kRtStRun = 0x04,
        kRtStTurnDash = 0x07,
        kRtStJumpSquat = 0x0A,
        kRtStFly = 0x0D,
        kRtStFall = 0x0E,
        kRtStGuardOn = 0x1A,
        kRtStGuard = 0x1B,
        kRtStEscape = 0x1E,
        kRtStEscapeF = 0x1F,
        kRtStEscapeB = 0x20,
        kRtStEscapeAir = 0x21,
        kRtStAttackFirst = 0x24,
        kRtStAttackAir = 0x33,
        kRtStCatch = 0x34,
        kRtStCatchDash = 0x36,
        kRtStCapturePulled = 0x3D,
        kRtStCaptureWait = 0x3E,
        kRtStCaptureCut = 0x40,
        kRtStCaptureThrown = 0x42,
        kRtStDamageFirst = 0x43,
        kRtStDamageFlyRoll = 0x46,
        kRtStDamageFall = 0x49,
        kRtStDownFirst = 0x4A,
        kRtStDownWaitCont = 0x4E,
        kRtStDownStand = 0x51,
        kRtStDownStandFb = 0x52,
        kRtStDownStandAttack = 0x53,
        kRtStDamageSong = 0x54,
        kRtStPassive = 0x60,
        kRtStPassiveFb = 0x61,
        kRtStCliffWait = 0x75,
        kRtStCliffAttack = 0x76,
        kRtStCliffJump2 = 0x7A,
        kRtStSpecialN = 0x112,
        kRtStFinal = 0x116,
        kRtMotPassiveStandF = 0xBF,
        kRtMotPassiveStandB = 0xC0
    };

    // 48-byte naturally aligned combined telemetry block mirrored into "TM4X".
    struct ReactionTechchaseStats {
        u32 rlCount;
        u16 rlMinFrames;
        u16 rlMaxFrames;
        float rlMeanFrames;
        float rlMedianFrames;
        u16 rlLastFrames;
        u8 rlLastCueKind;
        u8 rlCueActive;
        u32 tcEscapes;
        u32 tcPerfectEscapes;
        u32 tcCaughtCount;
        u16 tcStreak;
        u16 tcBestStreak;
        u16 tcLastTightness;
        u16 tcBestTightness;
        u8 tcLastTool;
        u8 tcPhase;
        u8 tcPad[2];
        u32 rlRingCount;
    };

    struct ReactionTechchaseState {
        ReactionTechchaseStats out;
        u16* ring;
        u16 ringCap;
        u16 ringHead;
        u8 cueKind;
        u8 hasBestTightness;
        u8 sampledThisFrame;
        u8 tcCompletedThisFrame;
        s32 cueFrame;
        s32 firstActionableFrame;
    };

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline u8 tcClassifyEscapeTool(s16 s)
    {
        struct ToolRange {
            s16 lo;
            s16 hi;
            u8 tool;
        };
        static const ToolRange kRanges[8] = {
            {kRtStEscape,      kRtStEscape,                           kTcToolSpotdodge},
            {kRtStEscapeF,     kRtStEscapeB,                          kTcToolRoll},
            {kRtStJumpSquat,   kRtStFly,                              kTcToolJump},
            {kRtStDash,        kRtStRun,                              kTcToolDash},
            {kRtStTurnDash,    kRtStTurnDash,                         kTcToolDash},
            {kRtStGuardOn,     kRtStGuard,                            kTcToolShield},
            {kRtStEscapeAir,   kRtStEscapeAir,                        kTcToolAirdodge},
            {kRtStAttackFirst, kRtStAttackAir,                        kTcToolAttack}
        };
        for (int i = 0; i < 8; i++)
        {
            if (chInRange(s, kRanges[i].lo, kRanges[i].hi))
            {
                return kRanges[i].tool;
            }
        }
        if (s == kRtStCatch || s == kRtStCatchDash
            || chInRange(s, kRtStSpecialN, static_cast<s16>(kRtStFinal - 1)))
        {
            return kTcToolAttack;
        }
        return kTcToolNone;
    }

    inline bool tcIsKnockdownOrTech(s16 s)
    {
        return chInRange(s, kRtStDownFirst, kRtStDownWaitCont)
               || chInRange(s, kRtStDownStand, kRtStDownStandAttack)
               || chInRange(s, kRtStPassive, kRtStPassiveFb);
    }

    inline bool tcIsReCatchStatus(s16 s)
    {
        return chInRange(s, kRtStCapturePulled, kRtStCaptureThrown)
               || chInRange(s, kRtStDamageFirst, kRtStDamageFlyRoll)
               || s == kRtStDamageSong;
    }

    inline bool tcIsVulnerableStatus(s16 s)
    {
        return chInRange(s, kRtStCapturePulled, kRtStCaptureThrown)
               || s == kRtStDamageFall
               || tcIsKnockdownOrTech(s);
    }

    inline u8 rtClassifyOpponentCue(s16 s, s16 prev, s16 mk, float lr, float vx)
    {
        if (s == kRtStPassive)
        {
            return kCueTechNeutral;
        }
        if (s == kRtStPassiveFb)
        {
            float dir = (mk == kRtMotPassiveStandF) ? lr : ((mk == kRtMotPassiveStandB) ? -lr : vx);
            return dir >= 0.0f ? static_cast<u8>(kCueTechRollRight) : static_cast<u8>(kCueTechRollLeft);
        }
        if (s == kRtStDownStandAttack)
        {
            return kCueGetupAttack;
        }
        if (chInRange(s, kRtStDownStand, kRtStDownStandFb))
        {
            return kCueGetupRoll;
        }
        if (chInRange(s, kRtStCliffAttack, kRtStCliffJump2) || (prev == kRtStCliffWait && s == kRtStFall))
        {
            return kCueLedgeOption;
        }
        return tcClassifyEscapeTool(s) == kTcToolAttack ? static_cast<u8>(kCueAttackStartup) : static_cast<u8>(kCueNone);
    }

    inline void reactionTechchaseResetTransient(ReactionTechchaseState& st)
    {
        st.out.rlCueActive = 0;
        st.out.tcPhase = kTcPhaseIdle;
        st.cueKind = kCueNone;
        st.sampledThisFrame = 0;
        st.tcCompletedThisFrame = 0;
        st.cueFrame = kChallengeNoFrame;
        st.firstActionableFrame = kChallengeNoFrame;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline void reactionTechchaseResetAll(ReactionTechchaseState& st, u16* ringBuf, u16 cap)
    {
        memset(&st, 0, sizeof(st));
        st.ring = ringBuf;
        st.ringCap = cap > kReactionRingCap ? static_cast<u16>(kReactionRingCap) : cap;
        reactionTechchaseResetTransient(st);
    }

    inline void reactionLabPushSample(ReactionTechchaseState& st, u16 frames, u8 cueKind)
    {
        if (frames < 1)
        {
            frames = 1;
        }
        st.out.rlCount++;
        st.out.rlLastFrames = frames;
        st.out.rlLastCueKind = cueKind;
        st.sampledThisFrame = 1;
        if (st.ring == NULL || st.ringCap == 0)
        {
            return;
        }
        st.ring[st.ringHead] = frames;
        if (++st.ringHead >= st.ringCap)
        {
            st.ringHead = 0;
        }
        if (st.out.rlRingCount < st.ringCap)
        {
            st.out.rlRingCount++;
        }

        u16 n = static_cast<u16>(st.out.rlRingCount);
        u16 sorted[kReactionRingCap];
        u32 sum = 0;
        for (u16 i = 0; i < n; i++)
        {
            u16 v = st.ring[i];
            sum += v;
            int j = static_cast<int>(i) - 1;
            while (j >= 0 && sorted[j] > v)
            {
                sorted[j + 1] = sorted[j];
                j--;
            }
            sorted[j + 1] = v;
        }
        st.out.rlMinFrames = sorted[0];
        st.out.rlMaxFrames = sorted[n - 1];
        st.out.rlMeanFrames = static_cast<float>(sum) / static_cast<float>(n);
        st.out.rlMedianFrames = 0.5f * static_cast<float>(static_cast<u32>(sorted[(n - 1) >> 1]) + sorted[n >> 1]);
    }

    inline void reactionTechchaseStep(ReactionTechchaseState& st, const ChallengeFrame* frames, int count, int selfIdx)
    {
        st.sampledThisFrame = 0;
        st.tcCompletedThisFrame = 0;
        const ChallengeFrame& in = frames[selfIdx];
        if (in.outOfPlay)
        {
            reactionTechchaseResetTransient(st);
            return;
        }

        const int evN = in.eventCount < kChallengeMaxEvents ? static_cast<int>(in.eventCount) : kChallengeMaxEvents;

        // --- Part A: Reaction Lab ---
        if (in.hitstun > 0)
        {
            st.out.rlCueActive = 0;
        }
        else
        {
            for (int j = 0; j < count; j++)
            {
                if (j == selfIdx || !frames[j].valid || frames[j].outOfPlay || frames[j].entryId == in.entryId)
                {
                    continue;
                }
                const ChallengeFrame& ov = frames[j];
                const int oevN = ov.eventCount < kChallengeMaxEvents ? static_cast<int>(ov.eventCount) : kChallengeMaxEvents;
                for (int k = 0; k < oevN; k++)
                {
                    // StatusEvent::motionKind is captured before Brawl sets the
                    // new status's subaction; prefer ov.motionKind when it matches.
                    s16 mk = (ov.events[k].status == ov.status && ov.motionKind != -1)
                                 ? ov.motionKind
                                 : ov.events[k].motionKind;
                    u8 cue = rtClassifyOpponentCue(ov.events[k].status, ov.events[k].prev, mk, ov.lr, ov.vx);
                    if (cue != kCueNone && (!st.out.rlCueActive || (cue != kCueAttackStartup && st.cueKind == kCueAttackStartup)))
                    {
                        st.out.rlCueActive = 1;
                        st.cueKind = cue;
                        st.cueFrame = in.frame;
                        break;
                    }
                }
            }

            if (st.out.rlCueActive)
            {
                if (in.frame - st.cueFrame > kReactionTimeoutFrames)
                {
                    st.out.rlCueActive = 0;
                }
                else if (in.frame > st.cueFrame)
                {
                    bool resp = (in.actionable && in.triggerButtons != 0);
                    for (int k = 0; k < evN && !resp; k++)
                    {
                        resp = (tcClassifyEscapeTool(in.events[k].status) != kTcToolNone
                                && tcClassifyEscapeTool(in.events[k].prev) == kTcToolNone);
                    }
                    if (resp)
                    {
                        st.out.rlCueActive = 0;
                        reactionLabPushSample(st, chClampU16(in.frame - st.cueFrame, 1), st.cueKind);
                    }
                }
            }
        }

        // --- Part B: Universal Techchase Escape ---
        bool enteredVuln = false;
        bool enteredReCatch = false;
        bool enteredKdTech = false;
        u8 tool = kTcToolNone;
        for (int k = 0; k < evN; k++)
        {
            s16 s = in.events[k].status;
            if (tcIsVulnerableStatus(s))
            {
                enteredVuln = true;
            }
            if (tcIsReCatchStatus(s))
            {
                enteredReCatch = true;
            }
            if (tcIsKnockdownOrTech(s))
            {
                enteredKdTech = true;
            }
            if (tool == kTcToolNone)
            {
                tool = tcClassifyEscapeTool(s);
            }
        }
        if (!enteredVuln && st.out.tcPhase == kTcPhaseIdle && tcIsVulnerableStatus(in.status))
        {
            enteredVuln = true;
        }
        if (!enteredReCatch && tcIsReCatchStatus(in.status))
        {
            enteredReCatch = true;
        }
        if (!enteredKdTech && tcIsKnockdownOrTech(in.status))
        {
            enteredKdTech = true;
        }

        if (st.out.tcPhase == kTcPhaseIdle && enteredVuln)
        {
            st.out.tcPhase = kTcPhaseVulnerable;
            st.firstActionableFrame = kChallengeNoFrame;
            return;
        }
        if (st.out.tcPhase == kTcPhaseVulnerable && in.hitstun == 0 && (!tcIsVulnerableStatus(in.status) || in.actionable))
        {
            st.out.tcPhase = kTcPhaseActionableWait;
            st.firstActionableFrame = in.frame;
        }
        if (st.out.tcPhase == kTcPhaseActionableWait)
        {
            bool caught = enteredReCatch || in.hitstun > 0;
            if (!caught && enteredKdTech)
            {
                // Landing from Damage_Fall into Down/Passive without being
                // hit or grabbed returns cleanly to Vulnerable (not caught).
                st.out.tcPhase = kTcPhaseVulnerable;
                st.firstActionableFrame = kChallengeNoFrame;
                return;
            }
            if (!caught && tool == kTcToolNone)
            {
                tool = tcClassifyEscapeTool(in.status);
            }
            if (!caught && tool != kTcToolNone)
            {
                u16 tf = chClampU16(in.frame - st.firstActionableFrame, 0);
                st.out.tcEscapes++;
                if (tf == 0)
                {
                    st.out.tcPerfectEscapes++;
                }
                chBumpStreak(st.out.tcStreak, st.out.tcBestStreak);
                st.out.tcLastTool = tool;
                st.out.tcLastTightness = tf;
                if (!st.hasBestTightness || tf < st.out.tcBestTightness)
                {
                    st.out.tcBestTightness = tf;
                    st.hasBestTightness = 1;
                }
                st.out.tcPhase = kTcPhaseIdle;
                st.tcCompletedThisFrame = 1;
            }
            else if (caught || in.frame - st.firstActionableFrame >= kTcEscapeTimeoutFrames)
            {
                st.out.tcCaughtCount++;
                st.out.tcStreak = 0;
                st.out.tcLastTool = kTcToolNone;
                st.out.tcPhase = (caught && (enteredVuln || tcIsVulnerableStatus(in.status)))
                                     ? static_cast<u8>(kTcPhaseVulnerable)
                                     : static_cast<u8>(kTcPhaseIdle);
                st.tcCompletedThisFrame = 1;
            }
            if (st.tcCompletedThisFrame)
            {
                st.firstActionableFrame = kChallengeNoFrame;
            }
        }
    }
}
}
