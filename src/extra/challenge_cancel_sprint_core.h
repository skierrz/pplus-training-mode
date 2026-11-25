#pragma once

#include <string.h>
#include <types.h>

#include "extra/challenge_common_core.h"

// Pure host-testable Project+ 3.2.1 Cancel Sprint backend core.
// Verified P+ 3.2.1 status IDs and cancel windows:
//  - Fox (0x06, FitFox.pac 0x11e58/0x27610): 0x115 -> 0x11A (groups 0x7/0x12).
//  - Falco (0x13, FitFalco.pac 0x2842c): 0x115 enables groups 0x7/0x12 at f4.
//  - Wolf (0x2C, FitWolf.pac 0x128d8/0x29154): 0x115 -> 0x11A (groups 0x7/0x12).
//  - Ness (0x0A, FitNess.pac 0x7328/0x8100): 0x115 -> 0x11E (f3) / 0x120 (f1).
//  - Lucas (0x1A, FitLucas.pac 0x281d4): 0x115 -> 0x11F -> 0x121 (f2).
//  - Mewtwo (0x26, FitMewtwo.pac 0x10c60): Hover 0x120 + aerial 0x62..0x66 -> 0x16.
//  - Lucario (0x21, FitLucario.pac 0x11630/0x27628/0x2bdf0): 0x117/0x118 -> 0x11E/0x1F/0x20.
namespace tm {
namespace extra {
    enum CancelSprintKind {
        kCancelKindNone = 0,
        kCancelKindShine = 1,
        kCancelKindMagnet = 2,
        kCancelKindHover = 3,
        kCancelKindAsc = 4
    };

    enum CancelSprintConst {
        kCsGroupJumpGround = 0x07,
        kCsGroupJumpAir = 0x12,
        kCsFtFox = 0x06,
        kCsFtNess = 0x0A,
        kCsFtFalco = 0x13,
        kCsFtLucas = 0x1A,
        kCsFtLucario = 0x21,
        kCsFtMewtwo = 0x26,
        kCsFtWolf = 0x2C,
        kCsStDash = 0x03,
        kCsStTurnDash = 0x07,
        kCsStJumpSquat = 0x0A,
        kCsStJump = 0x0B,
        kCsStJumpAerial = 0x0C,
        kCsStFly = 0x0D,
        kCsStLanding = 0x16,
        kCsStLandingLight = 0x17,
        kCsStLandingAttackAir = 0x18,
        kCsStGuardOn = 0x1A,
        kCsStEscapeF = 0x1F,
        kCsStEscapeB = 0x20,
        kCsStSpecialN = 0x112,
        kCsStSpecialLw = 0x115,
        kCsStLucarioCharge = 0x117,
        kCsStLucarioChargeMax = 0x118,
        kCsStLucarioShoot = 0x119,
        kCsStLucarioAscCancel = 0x11E,
        kCsStFalcoShineHit = 0x118,
        kCsStSpacieShineLoop = 0x11A,
        kCsStSpacieShineHit = 0x11B,
        kCsStSpacieShineEnd = 0x11C,
        kCsStNessMagnetHold = 0x11E,
        kCsStNessMagnetHit = 0x11F,
        kCsStNessMagnetEnd = 0x120,
        kCsStLucasMagnetHold = 0x11F,
        kCsStLucasMagnetHit = 0x120,
        kCsStLucasMagnetEnd = 0x121,
        kCsStMewtwoHoverExit = 0x11E,
        kCsStMewtwoHover = 0x120,
        kCsMotAttackAirN = 0x62,
        kCsMotAttackAirLw = 0x66
    };

    // 28-byte naturally aligned telemetry block mirrored directly into "TM4X".
    struct CancelSprintStats {
        u32 attempts;
        u32 totalCancels;
        u32 perfectCount;
        u32 sumFrames;
        u16 bestFrames;
        u16 lastFrames;
        u16 streak;
        u16 bestStreak;
        u8 lastKind;
        u8 lastSuccess;
        u8 active;
        u8 pad;
    };

    struct CancelSprintState {
        CancelSprintStats out;
        u8 activeKind;
        u8 sawAerialInHover;
        u8 windowOpen;
        u8 completedThisFrame;
        s32 windowOpenFrame;
        s32 statusEnterFrame;
        s16 trackedStatus;
        s16 lastFtKind;
    };

    struct CancelSpec {
        u8 ftKind;
        u8 kind;
        u16 st[4];
        u16 winSt;
        u8 winMinAge;
        u8 winAltIdx;
    };

    inline const CancelSpec* cancelSprintSpec(int ftKind)
    {
        static const CancelSpec kSpecs[7] = {
            {kCsFtFox,     kCancelKindShine,  {kCsStSpecialLw, kCsStSpacieShineLoop, kCsStSpacieShineHit, 0},              kCsStSpacieShineLoop, 0, 0xFF},
            {kCsFtFalco,   kCancelKindShine,  {kCsStSpecialLw, kCsStFalcoShineHit, 0, 0},                                  kCsStSpecialLw,       3, 0xFF},
            {kCsFtWolf,    kCancelKindShine,  {kCsStSpecialLw, kCsStSpacieShineLoop, kCsStSpacieShineHit, 0},              kCsStSpacieShineLoop, 0, 0xFF},
            {kCsFtNess,    kCancelKindMagnet, {kCsStSpecialLw, kCsStNessMagnetHold, kCsStNessMagnetHit, kCsStNessMagnetEnd}, kCsStNessMagnetHold, 2, 3},
            {kCsFtLucas,   kCancelKindMagnet, {kCsStSpecialLw, kCsStLucasMagnetHold, kCsStLucasMagnetHit, kCsStLucasMagnetEnd}, kCsStLucasMagnetEnd, 1, 0xFF},
            {kCsFtMewtwo,  kCancelKindHover,  {kCsStMewtwoHover, 0, 0, 0},                                                 kCsStMewtwoHover,     0, 0xFF},
            {kCsFtLucario, kCancelKindAsc,    {kCsStLucarioCharge, kCsStLucarioChargeMax, 0, 0},                           kCsStLucarioCharge,   0, 1}
        };
        for (int i = 0; i < 7; i++)
        {
            if (static_cast<int>(kSpecs[i].ftKind) == ftKind)
            {
                return &kSpecs[i];
            }
        }
        return NULL;
    }

    inline void cancelSprintResetTransient(CancelSprintState& st)
    {
        st.out.active = 0;
        st.activeKind = kCancelKindNone;
        st.sawAerialInHover = 0;
        st.windowOpen = 0;
        st.completedThisFrame = 0;
        st.windowOpenFrame = kChallengeNoFrame;
        st.statusEnterFrame = kChallengeNoFrame;
        st.trackedStatus = -1;
    }

    inline void cancelSprintResetAll(CancelSprintState& st)
    {
        memset(&st, 0, sizeof(st));
        st.lastFtKind = -1;
        cancelSprintResetTransient(st);
    }

    inline u32 cancelSprintRatePct(const CancelSprintState& st)
    {
        return st.out.attempts != 0 ? ((st.out.totalCancels * 100u) / st.out.attempts) : 0u;
    }

    inline float cancelSprintAvgFrames(const CancelSprintState& st)
    {
        return st.out.totalCancels != 0 ? (static_cast<float>(st.out.sumFrames) / static_cast<float>(st.out.totalCancels)) : 0.0f;
    }

    inline bool cancelSprintStep(CancelSprintState& st, const ChallengeFrame& in)
    {
        st.completedThisFrame = 0;
        const CancelSpec* sp = cancelSprintSpec(in.ftKind);
        const bool bad = (sp == NULL || in.outOfPlay || in.hitstun > 0);
        if (bad || st.lastFtKind != static_cast<s16>(in.ftKind))
        {
            cancelSprintResetTransient(st);
            st.lastFtKind = static_cast<s16>(in.ftKind);
            if (bad)
            {
                return false;
            }
        }

        const u8 kind = sp->kind;
        const int evN = in.eventCount < kChallengeMaxEvents ? static_cast<int>(in.eventCount) : kChallengeMaxEvents;
        const u32 jumpMask = (1u << kCsGroupJumpGround) | (1u << kCsGroupJumpAir);

        for (int k = 0; k <= evN; k++)
        {
            const bool isEnd = (k == evN);
            const s16 s = isEnd ? in.status : in.events[k].status;
            const s16 mk = isEnd ? in.motionKind : in.events[k].motionKind;

            if (s != st.trackedStatus)
            {
                st.trackedStatus = s;
                st.statusEnterFrame = in.frame;
            }

            // Only start a new attempt from true entry statuses (st[0], plus
            // 0x118 max-charge for Lucario). Continuation/hit statuses such as
            // Falco 0x118 (shared with Fire Bird End) only continue an active attempt.
            bool isEntry = (static_cast<s16>(sp->st[0]) == s)
                           || (kind == kCancelKindAsc && static_cast<s16>(sp->st[1]) == s);
            bool inMove = isEntry;
            if (!inMove && st.out.active)
            {
                for (int m = 1; m < 4 && !inMove; m++)
                {
                    inMove = (sp->st[m] != 0 && static_cast<s16>(sp->st[m]) == s);
                }
            }

            if (inMove && !st.out.active)
            {
                st.out.active = 1;
                st.activeKind = kind;
                st.sawAerialInHover = 0;
                st.windowOpen = 0;
                st.windowOpenFrame = kChallengeNoFrame;
            }
            if (st.out.active && kind == kCancelKindHover && !st.sawAerialInHover
                && chInRange(mk, kCsMotAttackAirN, kCsMotAttackAirLw))
            {
                st.sawAerialInHover = 1;
                st.windowOpen = 1;
                st.windowOpenFrame = in.frame;
            }

            if (inMove)
            {
                if (kind != kCancelKindHover && !st.windowOpen)
                {
                    s32 age = st.statusEnterFrame != kChallengeNoFrame ? (in.frame - st.statusEnterFrame) : 0;
                    bool altHit = (sp->winAltIdx < 4 && static_cast<s16>(sp->st[sp->winAltIdx]) == s);
                    bool fb = (static_cast<s16>(sp->winSt) == s && age >= static_cast<s32>(sp->winMinAge)) || altHit;
                    bool grp = (kind != kCancelKindAsc) && ((in.groups & jumpMask) != 0);
                    if (kind == kCancelKindAsc || grp || ((in.groups == 0 || !isEnd) && fb))
                    {
                        st.windowOpen = 1;
                        st.windowOpenFrame = in.frame;
                    }
                }
            }
            else if (st.out.active)
            {
                if (kind == kCancelKindHover && !st.sawAerialInHover)
                {
                    st.out.active = 0;
                    st.activeKind = kCancelKindNone;
                    continue;
                }
                bool ok = (kind == kCancelKindHover)
                              ? (s == kCsStLanding || s == kCsStLandingLight)
                              : (chInRange(s, kCsStJumpSquat, kCsStFly)
                                 || (kind == kCancelKindAsc
                                     && (s == kCsStLucarioAscCancel || s == kCsStGuardOn
                                         || chInRange(s, kCsStEscapeF, kCsStEscapeB)
                                         || s == kCsStDash || s == kCsStTurnDash)));
                st.out.attempts++;
                st.out.lastKind = kind;
                st.out.lastSuccess = ok ? 1 : 0;
                st.completedThisFrame = 1;
                if (ok)
                {
                    u16 fd = chClampU16((st.windowOpenFrame != kChallengeNoFrame ? (in.frame - st.windowOpenFrame) : 0) + 1, 1);
                    st.out.totalCancels++;
                    if (fd == 1)
                    {
                        st.out.perfectCount++;
                    }
                    st.out.sumFrames += fd;
                    st.out.lastFrames = fd;
                    if (st.out.bestFrames == 0 || fd < st.out.bestFrames)
                    {
                        st.out.bestFrames = fd;
                    }
                    chBumpStreak(st.out.streak, st.out.bestStreak);
                }
                else
                {
                    st.out.lastFrames = 0;
                    st.out.streak = 0;
                }
                st.out.active = 0;
                st.activeKind = kCancelKindNone;
            }
        }
        return st.completedThisFrame != 0;
    }
}
}
