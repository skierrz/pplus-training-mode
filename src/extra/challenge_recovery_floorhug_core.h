#pragma once

#include <string.h>
#include <types.h>

#include "extra/challenge_common_core.h"
#include "extra/drill_floorhug_core.h"
#include "extra/stage_geom_math.h"

// Pure host-testable Project+ 3.2.1 Universal Recovery Sweetspot & Floorhug /
// ASDI-Down Tech Challenge backend cores.
// Verified P+ 3.2.1 rules:
//  - 5-ledge-grab intangibility limit: LA-Basic[79] (Ledge.asm:495-513).
//  - Floorhug challenge composes directly with FloorhugState (drill_floorhug_core.h).
namespace tm {
namespace extra {
    enum RecoveryGrade {
        kRecGradeNone = 0,
        kRecGradeSweetspot = 1,
        kRecGradeHighOvershoot = 2,
        kRecGradeNoInvulnLimit = 3,
        kRecGradeSd = 4
    };

    enum RecoveryChallengeConst {
        kRecGrabLimit = 5,
        kRecStCliffCatchMove = 0x73,
        kRecStCliffCatch = 0x74,
        kRecStCliffWait = 0x75,
        kRecStDead = 0xBD,
        // Verified P+ 3.2.1 Meta Knight sticky taunt guard constants:
        // FitMetaKnight.pac AppealHi (0x1BC/0x1BD) hides body/hurtbox frames
        // 21..75 and uses ExitOverride 0x10C @ 0x1FE20; ft/fighter.h Appeal = 0x10C.
        kFhFtMetaKnight = 0x16,
        kFhStAppeal = 0x10C,
        kFhStSmashAppeal = 0x10D,
        kFhTauntGuardFrames = 6
    };

    const float kRecSweetspotMaxPeakAboveLip = 8.0f;
    const float kRecNearLedgeHorizWindow = 18.0f;
    const float kRecOffstageMargin = 2.0f;

    // Pure host-testable 3D GX world-space guide bounds for one ledge:
    //  - grabMinX..grabMaxX x grabMinY..grabMaxY: live cliff-hang snap pocket
    //    derived from grCollStatus +0x3C..+0x48 (Ledge.asm:16-52).
    //  - sweetMinX..sweetMaxX at sweetCeilY: horizontal sweetspot ceiling line
    //    (lipY + kRecSweetspotMaxPeakAboveLip across kRecNearLedgeHorizWindow).
    struct RecoveryLedgeGuide {
        float lipX;
        float lipY;
        float outward;
        float grabMinX;
        float grabMaxX;
        float grabMinY;
        float grabMaxY;
        float sweetMinX;
        float sweetMaxX;
        float sweetCeilY;
    };

    struct RecoveryGuideGeom {
        u8 valid;
        RecoveryLedgeGuide sides[2]; // 0 = left ledge, 1 = right ledge
    };

    inline bool floorhugIsTauntStatus(s16 status)
    {
        return status == kFhStAppeal || status == kFhStSmashAppeal;
    }

    // Returns true when a fighter should be cleanly interrupted out of Appeal
    // across a reset window (prevents Meta Knight's AppealHi vanish / sticky
    // taunt loop across Floorhug drill resets without disturbing intentional
    // non-Floorhug taunt savestates for other fighters).
    inline bool floorhugNeedsTauntInterrupt(int ftKind, s16 status, u8 guardFrames, bool floorhugActive,
                                            bool wasInTaunt)
    {
        if (guardFrames == 0 || !floorhugIsTauntStatus(status))
        {
            return false;
        }
        if (floorhugActive)
        {
            return ftKind >= 0;
        }
        return ftKind == kFhFtMetaKnight && wasInTaunt;
    }

    // Returns true when a reset needs to repair Meta Knight's AppealHi
    // visibility/hurtbox modules even if savestate load already reverted
    // status to Wait (never touches non-taunt Meta Knight savestates outside
    // Floorhug).
    inline bool floorhugNeedsMetaKnightModuleRestore(int ftKind, bool onReset, bool floorhugActive, bool wasInTaunt)
    {
        return onReset && ftKind == kFhFtMetaKnight && (floorhugActive || wasInTaunt);
    }

    inline bool computeRecoveryGuideGeom(const StageGeometry& g, float cliffOffX, float cliffOffY, float cliffW,
                                         float cliffH, RecoveryGuideGeom& out)
    {
        (void)cliffOffX;
        memset(&out, 0, sizeof(out));
        if (!g.floor.valid || !g.hasLedges)
        {
            return false;
        }
        float w = (cliffW >= 4.0f && cliffW <= 30.0f) ? cliffW : 10.0f;
        float offY = (cliffOffY >= 1.0f && cliffOffY <= 24.0f) ? cliffOffY : 9.0f;
        float h = (cliffH >= 4.0f && cliffH <= 30.0f) ? cliffH : 10.0f;
        if (w < 6.0f) w = 6.0f;
        if (w > 22.0f) w = 22.0f;

        for (int s = 0; s < 2; s++)
        {
            const LedgeInfo& lip = (s == 0) ? g.leftLedge : g.rightLedge;
            RecoveryLedgeGuide& d = out.sides[s];
            float outDir = (lip.dir >= 0.0f) ? -1.0f : 1.0f;
            float gx0 = lip.x;
            float gx1 = lip.x + outDir * w;
            float sx0 = lip.x;
            float sx1 = lip.x + outDir * kRecNearLedgeHorizWindow;
            d.lipX = lip.x;
            d.lipY = lip.y;
            d.outward = outDir;
            d.grabMinX = gx0 < gx1 ? gx0 : gx1;
            d.grabMaxX = gx0 < gx1 ? gx1 : gx0;
            d.grabMinY = lip.y - (offY + h);
            d.grabMaxY = lip.y - offY;
            d.sweetMinX = sx0 < sx1 ? sx0 : sx1;
            d.sweetMaxX = sx0 < sx1 ? sx1 : sx0;
            d.sweetCeilY = lip.y + kRecSweetspotMaxPeakAboveLip;
        }
        out.valid = 1;
        return true;
    }

    // 48-byte naturally aligned combined telemetry block mirrored into "TM4X".
    struct RecoveryFloorhugStats {
        u32 recAttempts;
        u32 recSweetspots;
        u32 recHighCount;
        u32 recNoInvulnCount;
        u32 recSdCount;
        u16 recStreak;
        u16 recBestStreak;
        u8 recLastGrade;
        u8 recLastGrabs;
        s16 recLastXlu;
        float recLastHeightDelta;
        u32 fhReps;
        u32 fhSuccesses;
        u32 fhTruePunishes;
        u16 fhStreak;
        u16 fhBestStreak;
    };

    struct RecoveryFloorhugState {
        RecoveryFloorhugStats out;
        u8 offstageActive;
        u8 sawPeakNearLip;
        u8 recCompletedThisFrame;
        u8 fhCompletedThisFrame;
        float maxHeightAboveLip;
        u32 lastSeenHitCount;
        u32 lastSeenPunishCount;
        u32 totalHitstunSaved;
        s16 lastHitstunSaved;
        s16 lastPunishActFrame;
        u8 lastFhOutcome;
        u8 lastAsdiFromCStick;
    };

    inline void recoveryFloorhugResetTransient(RecoveryFloorhugState& st)
    {
        st.offstageActive = 0;
        st.sawPeakNearLip = 0;
        st.recCompletedThisFrame = 0;
        st.fhCompletedThisFrame = 0;
        st.maxHeightAboveLip = -1000.0f;
        st.lastSeenHitCount = 0;
        st.lastSeenPunishCount = 0;
    }

    inline void recoveryFloorhugResetAll(RecoveryFloorhugState& st)
    {
        memset(&st, 0, sizeof(st));
        recoveryFloorhugResetTransient(st);
    }

    inline bool recoverySweetspotStep(RecoveryFloorhugState& st, const StageGeometry& g, const ChallengeFrame& in)
    {
        st.recCompletedThisFrame = 0;
        if (!g.floor.valid || !g.hasLedges)
        {
            return false;
        }

        const int evN = in.eventCount < kChallengeMaxEvents ? static_cast<int>(in.eventCount) : kChallengeMaxEvents;
        // Ledge.asm updates LA-Basic[79] and m_xluFrameGlobal on Cliff_Wait
        // (0x75), after Cliff_Catch_Move (0x73) and Cliff_Catch (0x74).
        bool onLedge = chInRange(in.status, kRecStCliffCatchMove, kRecStCliffWait);
        bool grabbed = (in.status == kRecStCliffWait);
        bool died = (in.status == kRecStDead);
        for (int k = 0; k < evN; k++)
        {
            s16 es = in.events[k].status;
            onLedge = onLedge || chInRange(es, kRecStCliffCatchMove, kRecStCliffWait);
            grabbed = grabbed || (es == kRecStCliffWait);
            died = died || (es == kRecStDead);
        }

        const LedgeInfo& lip = (in.x < 0.5f * (g.leftLedge.x + g.rightLedge.x)) ? g.leftLedge : g.rightLedge;

        if (!st.offstageActive)
        {
            if (!in.outOfPlay && !died && !onLedge && in.airborne
                && (in.x < g.floor.leftX - kRecOffstageMargin || in.x > g.floor.rightX + kRecOffstageMargin
                    || in.y < g.floor.centerY - 2.0f))
            {
                st.offstageActive = 1;
                st.sawPeakNearLip = 0;
                st.maxHeightAboveLip = -1000.0f;
            }
            return false;
        }

        float dx = in.x - lip.x;
        float dy = in.y - lip.y;
        // Only grade the final actionable approach into the lip window: clear
        // peak height while launched in hitstun or outside the 18-unit zone.
        if (in.hitstun > 0 || (in.airborne && !onLedge && dx * dx > kRecNearLedgeHorizWindow * kRecNearLedgeHorizWindow))
        {
            st.sawPeakNearLip = 0;
            st.maxHeightAboveLip = -1000.0f;
        }
        else if (!onLedge && dx * dx <= kRecNearLedgeHorizWindow * kRecNearLedgeHorizWindow
                 && (!st.sawPeakNearLip || dy > st.maxHeightAboveLip))
        {
            st.sawPeakNearLip = 1;
            st.maxHeightAboveLip = dy;
        }

        u8 grade = kRecGradeNone;
        float delta = st.sawPeakNearLip ? st.maxHeightAboveLip : dy;
        s16 xlu = in.xluFrames;
        if (died)
        {
            grade = kRecGradeSd;
            delta = dy;
            xlu = 0;
        }
        else if (in.outOfPlay)
        {
            st.offstageActive = 0;
            return false;
        }
        else if (grabbed)
        {
            grade = (in.ledgeGrabs > kRecGrabLimit && xlu <= 0)
                        ? static_cast<u8>(kRecGradeNoInvulnLimit)
                        : (delta > kRecSweetspotMaxPeakAboveLip ? static_cast<u8>(kRecGradeHighOvershoot)
                                                               : static_cast<u8>(kRecGradeSweetspot));
        }
        else if (!onLedge && !in.airborne && in.x >= g.floor.leftX && in.x <= g.floor.rightX)
        {
            grade = kRecGradeHighOvershoot;
        }
        else
        {
            return false;
        }

        st.out.recAttempts++;
        st.out.recLastGrade = grade;
        st.out.recLastHeightDelta = delta;
        st.out.recLastGrabs = in.ledgeGrabs;
        st.out.recLastXlu = xlu;
        switch (grade)
        {
        case kRecGradeSweetspot:
            st.out.recSweetspots++;
            chBumpStreak(st.out.recStreak, st.out.recBestStreak);
            break;
        case kRecGradeHighOvershoot:
            st.out.recHighCount++;
            st.out.recStreak = 0;
            break;
        case kRecGradeNoInvulnLimit:
            st.out.recNoInvulnCount++;
            st.out.recStreak = 0;
            break;
        default:
            st.out.recSdCount++;
            st.out.recStreak = 0;
            break;
        }
        st.offstageActive = 0;
        st.recCompletedThisFrame = 1;
        return true;
    }

    inline bool floorhugChallengeObserve(RecoveryFloorhugState& st, const FloorhugState& fh)
    {
        st.fhCompletedThisFrame = 0;
        if (fh.hitCount < st.lastSeenHitCount)
        {
            st.lastSeenHitCount = fh.hitCount;
            st.lastSeenPunishCount = fh.truePunishCount;
            return false;
        }
        bool updated = false;
        if (fh.hitCount > st.lastSeenHitCount)
        {
            st.lastSeenHitCount = fh.hitCount;
            st.out.fhReps++;
            u8 out = fh.last.outcome;
            st.lastFhOutcome = out;
            st.lastHitstunSaved = fh.last.hitstunSaved;
            st.lastPunishActFrame = fh.last.punishActFrame;
            st.lastAsdiFromCStick = fh.last.asdiFromCStick;
            if (out == kFhCcFloorhugCancel || out == kFhStandingFloorhugCancel || out == kFhAsdiDownGroundTech)
            {
                st.out.fhSuccesses++;
                if (fh.last.hitstunSaved > 0)
                {
                    st.totalHitstunSaved += static_cast<u32>(fh.last.hitstunSaved);
                }
                chBumpStreak(st.out.fhStreak, st.out.fhBestStreak);
            }
            else
            {
                st.out.fhStreak = 0;
            }
            st.fhCompletedThisFrame = 1;
            updated = true;
        }
        if (fh.truePunishCount > st.lastSeenPunishCount)
        {
            st.out.fhTruePunishes += (fh.truePunishCount - st.lastSeenPunishCount);
            st.lastSeenPunishCount = fh.truePunishCount;
            st.lastPunishActFrame = fh.last.punishActFrame;
            updated = true;
        }
        return updated;
    }
}
}
