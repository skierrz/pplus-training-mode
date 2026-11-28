#pragma once

#include <string.h>
#include <types.h>

// Pure P+ 3.2.1 Hover / Float-Cancel and Double-Jump Cancel (DJC) detection,
// grading, and telemetry core (no Brawl headers; host-testable on PC).
//
// Ground-truth P+ 3.2.1 references:
// - Fighter kinds: lib/BrawlHeaders/Brawl/Include/ft/ft_entry.h and
//   Source/ProjectM/CloneEngine.asm:50-55 (Yoshi=0x04, Ness=0x0A, Peach=0x0C,
//   Lucas=0x1A, Mewtwo=0x26).
// - Peach float (0x11E): FitPeach.pac entry 0x11E @ 0x23504, CIL @ 0x11398
//   (LA-Bit[62]=0x1200003E, RA-Basic[2]=0x20000002 up to 150f). Float aerials
//   change subaction to Attack_Air_N..Lw (0x62..0x66) inside status 0x11E and
//   land into Landing (0x16) on ground contact (float cancel).
// - Mewtwo hover (0x120): FitMewtwo.pac entry 0x120 @ 0x10C60, CIL @ 0x284A4
//   (LA-Bit[97]=0x12000061, LA-Bit[115]=0x12000073, RA-Basic[2]=0x20000002 up
//   to 60f; hover exit transitions to 0x11E then Fall 0x0E). Hover aerials
//   change subaction to 0x62..0x66 inside 0x120 and land into Landing (0x16).
// - DJC: Source/Project+/Physics.asm:134-264 (RA-Bit[33]=0x22000021 on
//   Jump_Aerial 0x0C -> Attack_Air 0x33).
namespace tm {
namespace extra {
    enum FloatDjcConst {
        kDrillFtYoshi = 0x04,
        kDrillFtNess = 0x0A,
        kDrillFtPeach = 0x0C,
        kDrillFtGanondorf = 0x14,
        kDrillFtLucas = 0x1A,
        kDrillFtMewtwo = 0x26,

        kFdStWait = 0x00,
        kFdStWalk = 0x01,
        kFdStWalkBrake = 0x02,
        kFdStDash = 0x03,
        kFdStTurn = 0x06,
        kFdStTurnDash = 0x07,
        kFdStJumpSquat = 0x0A,
        kFdStJump = 0x0B,
        kFdStJumpAerial = 0x0C,
        kFdStFly = 0x0D,
        kFdStFall = 0x0E,
        kFdStFallAerial = 0x0F,
        kFdStFallSpecial = 0x10,
        kFdStSquat = 0x11,
        kFdStSquatWait = 0x12,
        kFdStSquatRv = 0x15,
        kFdStLanding = 0x16,
        kFdStLandingLight = 0x17,
        kFdStLandingAttackAir = 0x18,
        kFdStLandingFallSpecial = 0x19,
        kFdStGuardOn = 0x1A,
        kFdStGuardOff = 0x1C,
        kFdStEscape = 0x1E,
        kFdStEscapeAir = 0x21,
        kFdStAttack = 0x24,
        kFdStAttackAir = 0x33,
        kFdStCatch = 0x34,
        kFdStCatchDash = 0x36,
        kFdStDamage = 0x43,
        kFdStDamageFlyRoll = 0x46,
        kFdStPass = 0x72,
        kFdStDead = 0xBD,
        kFdStSpecialN = 0x112,
        kFdStFinal = 0x116,
        kFdStPeachFloat = 0x11E,
        kFdStMewtwoHoverExit = 0x11E,
        kFdStMewtwoHover = 0x120,

        kFdMotionAttackAirN = 0x62,
        kFdMotionAttackAirLw = 0x66,

        kFdVarPeachFloatUsed = 0x1200003E,    // LA-Bit[62]
        kFdVarMewtwoHoverActive = 0x12000061, // LA-Bit[97]
        kFdVarMewtwoHoverUsed = 0x12000073,   // LA-Bit[115]
        kFdVarGanonFloatCount = 0x10000062,  // LA-Basic[98] (Dead Man's Float frames)
        kFdVarFloatFrames = 0x20000002,       // RA-Basic[2]
        kFdVarDjcBit = 0x22000021,            // RA-Bit[33]

        kFdMaxEvents = 8,
        kFdNoFrame = -1,
        kFdDroppedFloatWindow = 24,
        kFdActGiveUp = 30,
        kFdDjcInstantMax = 2,
        kFdDjcLowRisingMax = 6
    };

    enum FcOutcome {
        kFcNone = 0,
        kFcSuccess = 1,
        kFcMissFullLag = 2
    };

    enum DjcGrade {
        kDjcNone = 0,
        kDjcInstant = 1,
        kDjcLowRising = 2,
        kDjcLate = 3
    };

    enum DjcLandOutcome {
        kDjcLandPending = 0,
        kDjcLandAutocancel = 1,
        kDjcLandLCancelOk = 2,
        kDjcLandLCancelMiss = 3,
        kDjcLandAirborneEnd = 4
    };

    struct FloatDjcStatusEvent {
        s16 status;
        s16 prev;
        s16 motionKind;
        s16 lCancel;
        u8 airborne;
    };

    struct FloatDjcInput {
        s32 frame;
        int ftKind;
        s16 status;
        s16 motionKind;
        s16 hitlag;
        s16 hitstun;
        u8 airborne;
        u8 actionable;
        u8 hoverActive;   // Mewtwo LA-Bit[97] (or 0 for others)
        u8 floatUsed;     // Peach LA-Bit[62] / Mewtwo LA-Bit[115]
        u8 djcBit;        // P+ RA-Bit[33]
        s32 floatRem;     // RA-Basic[2] frames remaining
        float y;
        float vy;
        u8 eventCount;
        FloatDjcStatusEvent events[kFdMaxEvents];
    };

    struct FloatDjcStats {
        u32 fcAttempts;
        u32 fcSuccesses;
        u32 fcStreak;
        u32 fcBestStreak;
        u32 djcAttempts;
        u32 djcInstant;
        u32 djcStreak;
        u32 djcBestStreak;
        s16 bestActFrame; // lowest nonzero actOoFcFrame / actOoHcFrame, 0 if none
    };

    struct FloatDjcResult {
        // Latest Float / Hover Cancel landing result.
        u8 fcOutcome;           // FcOutcome
        u8 fcIsHover;           // 0 = Peach Float, 1 = Mewtwo Hover
        u8 fcLandingLCancelled; // for kFcMissFullLag: 1 if L-cancelled
        s16 fcDurationFrames;
        s16 fcAerialFrame;      // 1-based float/hover frame when aerial began
        s16 fcActFrame;         // 1-based first actionable frame out of 0x16/0x17
        s16 fcMotionKind;       // 0x62..0x66
        s32 fcEventFrame;       // tracker frame of landing
        float fcEntryY;
        float fcMinY;

        // Latest DJC result.
        u8 djcGrade;            // DjcGrade
        u8 djcLandOutcome;      // DjcLandOutcome
        u8 djcLandedClean;      // 1 if autocancelled or L-cancelled
        u8 djcHadBit33;         // 1 if RA-Bit[33] was set
        s16 djcFrame;           // f_aerial - f_jumpAerial + 1 (1 = instant)
        s16 djcMotionKind;      // aerial motionKind
        s32 djcEventFrame;      // tracker frame of DJC start
        float djcVyBefore;
        float djcVyAfter;
        float djcHeight;
    };

    struct FloatDjcSlot {
        int lastFtKind;

        // Float / Hover transient tracking.
        bool inFloat;
        bool isHover;
        bool groundedFloatStatus;
        bool floatAerialActive;
        bool floatAerialVia33;
        bool droppedFloatAerial;
        s32 floatStartFrame;
        s32 floatAerialStartFrame;
        s32 droppedFloatFrame;
        s16 floatAerialMotion;
        s16 floatDuration;
        float floatEntryY;
        float minFloatY;

        // Act-out-of-FC/HC transient tracking (after 0x16/0x17 landing).
        bool fcActArmed;
        s32 fcActLandFrame;
        s32 fcActFirstFrame;

        // DJC transient tracking.
        bool inJumpAerial;
        bool djcLandingArmed;
        bool djcVySamplePending;
        bool jumpAerialSawBit33;
        s32 jumpAerialStartFrame;
        s32 djcStartFrame;
        float prevVy;
        bool havePrevVy;

        FloatDjcStats stats;
        FloatDjcResult last;
    };

    inline bool isFloatCapable(int ftKind)
    {
        return ftKind == kDrillFtPeach || ftKind == kDrillFtMewtwo || ftKind == kDrillFtGanondorf;
    }

    inline bool isDjcCapable(int ftKind)
    {
        return ftKind == kDrillFtYoshi || ftKind == kDrillFtNess || ftKind == kDrillFtPeach
               || ftKind == kDrillFtLucas || ftKind == kDrillFtMewtwo;
    }

    inline int floatStatusFor(int ftKind)
    {
        if (ftKind == kDrillFtPeach)
        {
            return kFdStPeachFloat;
        }
        if (ftKind == kDrillFtMewtwo)
        {
            return kFdStMewtwoHover;
        }
        if (ftKind == kDrillFtGanondorf)
        {
            return kFdStSpecialN;
        }
        return -1;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline bool isAttackAirMotion(s32 mk)
    {
        return mk >= kFdMotionAttackAirN && mk <= kFdMotionAttackAirLw;
    }

    inline bool isFcLandingStatus(int s)
    {
        return s == kFdStLanding || s == kFdStLandingLight;
    }

    inline bool isFcActLookThrough(int s)
    {
        return s == kFdStWait || s == kFdStWalkBrake || s == kFdStTurn || s == kFdStSquat
               || s == kFdStSquatWait || s == kFdStSquatRv;
    }

    inline bool isFcActStatus(int s)
    {
        return s == kFdStWalk || s == kFdStDash || s == kFdStTurnDash || s == kFdStJumpSquat
               || s == kFdStGuardOn || (s >= kFdStEscape && s <= kFdStEscapeAir)
               || (s >= kFdStAttack && s <= kFdStCatchDash) || s == kFdStPass
               || (s >= kFdStSpecialN && s < kFdStFinal);
    }

    inline bool isFloatDropAirStatus(int ftKind, int s)
    {
        if (s == kFdStFall || s == kFdStFallAerial || s == kFdStFallSpecial)
        {
            return true;
        }
        return ftKind == kDrillFtMewtwo && s == kFdStMewtwoHoverExit;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline bool isHitOrDeadStatus(int s)
    {
        return (s >= kFdStDamage && s <= kFdStDamageFlyRoll) || s == kFdStDead;
    }

    inline u8 classifyDjc(s16 djcFrame)
    {
        if (djcFrame <= kFdDjcInstantMax)
        {
            return kDjcInstant;
        }
        if (djcFrame <= kFdDjcLowRisingMax)
        {
            return kDjcLowRising;
        }
        return kDjcLate;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    // Clears in-flight tracking without erasing cumulative session stats
    // (called on savestate load or fighter change).
    inline void floatDjcResetTransient(FloatDjcSlot& st)
    {
        memset(&st.inFloat, 0, reinterpret_cast<char*>(&st.stats) - reinterpret_cast<char*>(&st.inFloat));
        st.floatStartFrame = kFdNoFrame;
        st.floatAerialStartFrame = kFdNoFrame;
        st.droppedFloatFrame = kFdNoFrame;
        st.floatAerialMotion = -1;
        st.fcActLandFrame = kFdNoFrame;
        st.fcActFirstFrame = kFdNoFrame;
        st.jumpAerialStartFrame = kFdNoFrame;
        st.djcStartFrame = kFdNoFrame;
    }

    inline void floatDjcResetAll(FloatDjcSlot& st)
    {
        memset(&st, 0, sizeof(st));
        st.lastFtKind = -1;
        st.last.fcMotionKind = -1;
        st.last.fcEventFrame = kFdNoFrame;
        st.last.djcMotionKind = -1;
        st.last.djcEventFrame = kFdNoFrame;
        floatDjcResetTransient(st);
    }

    inline void recordFcOutcome(FloatDjcSlot& st, u8 outcome, s32 frame, bool lCancelled)
    {
        st.stats.fcAttempts++;
        if (outcome == kFcSuccess)
        {
            st.stats.fcSuccesses++;
            st.stats.fcStreak++;
            if (st.stats.fcStreak > st.stats.fcBestStreak)
            {
                st.stats.fcBestStreak = st.stats.fcStreak;
            }
        }
        else
        {
            st.stats.fcStreak = 0;
        }

        st.last.fcOutcome = outcome;
        st.last.fcIsHover = st.isHover ? 1 : 0;
        st.last.fcLandingLCancelled = lCancelled ? 1 : 0;
        st.last.fcDurationFrames = st.floatDuration > 0 ? st.floatDuration : 1;
        if (st.floatStartFrame != kFdNoFrame && st.floatAerialStartFrame != kFdNoFrame
            && st.floatAerialStartFrame >= st.floatStartFrame)
        {
            st.last.fcAerialFrame = static_cast<s16>(st.floatAerialStartFrame - st.floatStartFrame + 1);
        }
        else
        {
            st.last.fcAerialFrame = 0;
        }
        st.last.fcActFrame = 0;
        st.last.fcMotionKind = st.floatAerialMotion;
        st.last.fcEventFrame = frame;
        st.last.fcEntryY = st.floatEntryY;
        st.last.fcMinY = st.minFloatY;

        st.fcActArmed = (outcome == kFcSuccess);
        st.fcActLandFrame = st.fcActArmed ? frame : kFdNoFrame;
        st.fcActFirstFrame = kFdNoFrame;
    }

    inline void clearFloatFlight(FloatDjcSlot& st)
    {
        st.inFloat = false;
        st.floatAerialActive = false;
        st.floatAerialVia33 = false;
        st.droppedFloatAerial = false;
        st.droppedFloatFrame = kFdNoFrame;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline void stepFloatDjc(const FloatDjcInput& in, FloatDjcSlot& st)
    {
        if (st.lastFtKind != -1 && st.lastFtKind != in.ftKind)
        {
            floatDjcResetTransient(st);
        }
        st.lastFtKind = in.ftKind;

        const s32 f = in.frame;
        const bool hit = in.hitstun > 0 || in.hitlag > 0;
        const int floatSt = floatStatusFor(in.ftKind);
        const bool canFloat = floatSt >= 0;
        const bool canDjc = isDjcCapable(in.ftKind);

        // Update act-out-of-FC/HC frame tracking on status events.
        if (st.fcActArmed && hit)
        {
            st.fcActArmed = false;
        }

        // Sample post-DJC velocity one frame after Attack_Air start while still airborne.
        if (st.djcVySamplePending && f > st.djcStartFrame)
        {
            if (in.status == kFdStAttackAir && in.airborne != 0)
            {
                st.last.djcVyAfter = in.vy;
            }
            st.djcVySamplePending = false;
        }

        const int evCount = in.eventCount <= kFdMaxEvents ? static_cast<int>(in.eventCount) : kFdMaxEvents;
        for (int k = 0; k < evCount; k++)
        {
            const FloatDjcStatusEvent& e = in.events[k];
            const int s = e.status;
            const int prev = e.prev;

            // --- Act-out-of-FC/HC detection ---
            if (st.fcActArmed && !hit)
            {
                if (isFcActLookThrough(s))
                {
                    if (st.fcActFirstFrame == kFdNoFrame)
                    {
                        st.fcActFirstFrame = f;
                    }
                }
                else if (isFcActStatus(s))
                {
                    if (st.fcActFirstFrame == kFdNoFrame)
                    {
                        st.fcActFirstFrame = f;
                    }
                    s16 actF = static_cast<s16>(f - st.fcActFirstFrame + 1);
                    if (actF < 1)
                    {
                        actF = 1;
                    }
                    st.last.fcActFrame = actF;
                    if (st.stats.bestActFrame == 0 || actF < st.stats.bestActFrame)
                    {
                        st.stats.bestActFrame = actF;
                    }
                    st.fcActArmed = false;
                }
                else if (!isFcLandingStatus(s))
                {
                    st.fcActArmed = false;
                }
            }

            // --- Float / Hover Cancel detection (Peach 0x11E, Mewtwo 0x120, Ganondorf airborne 0x112) ---
            if (canFloat)
            {
                if (s == floatSt)
                {
                    if (in.ftKind == kDrillFtGanondorf && in.airborne == 0 && e.airborne == 0)
                    {
                        // Grounded Warlock Punch shares status 0x112; never grade as Dead Man's Float.
                        clearFloatFlight(st);
                        st.groundedFloatStatus = true;
                    }
                    else
                    {
                        st.groundedFloatStatus = false;
                        st.inFloat = true;
                        st.isHover = (in.ftKind == kDrillFtMewtwo);
                        st.floatStartFrame = f;
                        st.floatAerialStartFrame = kFdNoFrame;
                        st.droppedFloatFrame = kFdNoFrame;
                        st.floatAerialActive = false;
                        st.floatAerialVia33 = false;
                        st.droppedFloatAerial = false;
                        st.floatAerialMotion = -1;
                        st.floatDuration = 1;
                        st.floatEntryY = in.y;
                        st.minFloatY = in.y;
                    }
                }
                else if (st.inFloat && prev == floatSt)
                {
                    // Catch same-frame float-aerial -> landing where motionKind on the
                    // status-change event still holds Attack_Air_N..Lw (0x62..0x66).
                    if (!st.floatAerialActive && isAttackAirMotion(e.motionKind))
                    {
                        st.floatAerialActive = true;
                        st.floatAerialStartFrame = f;
                        st.floatAerialMotion = e.motionKind;
                    }

                    if (isFcLandingStatus(s))
                    {
                        if (st.floatAerialActive)
                        {
                            recordFcOutcome(st, kFcSuccess, f, false);
                        }
                        clearFloatFlight(st);
                    }
                    else if (s == kFdStLandingAttackAir)
                    {
                        if (st.floatAerialActive)
                        {
                            recordFcOutcome(st, kFcMissFullLag, f, e.lCancel > 0);
                        }
                        clearFloatFlight(st);
                    }
                    else if (s == kFdStAttackAir)
                    {
                        // Direct transition 0x11E/0x120 -> 0x33.
                        st.inFloat = false;
                        st.floatAerialActive = true;
                        st.floatAerialVia33 = true;
                        st.droppedFloatAerial = false;
                        st.floatAerialStartFrame = f;
                        st.floatAerialMotion = isAttackAirMotion(in.motionKind) ? in.motionKind : e.motionKind;
                    }
                    else if (isFloatDropAirStatus(in.ftKind, s))
                    {
                        if (st.floatAerialActive)
                        {
                            // Float aerial finished in midair without landing.
                            clearFloatFlight(st);
                        }
                        else
                        {
                            // Dropped float/hover before starting an aerial.
                            st.inFloat = false;
                            st.droppedFloatFrame = f;
                        }
                    }
                    else
                    {
                        clearFloatFlight(st);
                    }
                }
                else if (st.floatAerialVia33)
                {
                    if (isFcLandingStatus(s))
                    {
                        recordFcOutcome(st, kFcSuccess, f, false);
                        clearFloatFlight(st);
                    }
                    else if (s == kFdStLandingAttackAir)
                    {
                        recordFcOutcome(st, kFcMissFullLag, f, e.lCancel > 0);
                        clearFloatFlight(st);
                    }
                    else if (s != kFdStAttackAir)
                    {
                        clearFloatFlight(st);
                    }
                }
                else if (st.droppedFloatAerial)
                {
                    if (s == kFdStLandingAttackAir)
                    {
                        recordFcOutcome(st, kFcMissFullLag, f, e.lCancel > 0);
                        clearFloatFlight(st);
                    }
                    else if (s != kFdStAttackAir)
                    {
                        clearFloatFlight(st);
                    }
                }
                else if (st.droppedFloatFrame != kFdNoFrame)
                {
                    if (s == kFdStAttackAir && (f - st.droppedFloatFrame) <= kFdDroppedFloatWindow)
                    {
                        // Started aerial after dropping float/hover early.
                        st.droppedFloatAerial = true;
                        st.floatAerialStartFrame = f;
                        st.floatAerialMotion = isAttackAirMotion(in.motionKind) ? in.motionKind : e.motionKind;
                    }
                    else if (!isFloatDropAirStatus(in.ftKind, s))
                    {
                        clearFloatFlight(st);
                    }
                }
            }

            // --- DJC detection (Yoshi, Ness, Peach, Lucas, Mewtwo) ---
            if (canDjc)
            {
                if (st.djcLandingArmed)
                {
                    if (isFcLandingStatus(s))
                    {
                        st.last.djcLandOutcome = kDjcLandAutocancel;
                        st.last.djcLandedClean = 1;
                        st.djcLandingArmed = false;
                    }
                    else if (s == kFdStLandingAttackAir)
                    {
                        bool ok = e.lCancel > 0;
                        st.last.djcLandOutcome = ok ? kDjcLandLCancelOk : kDjcLandLCancelMiss;
                        st.last.djcLandedClean = ok ? 1 : 0;
                        st.djcLandingArmed = false;
                    }
                    else if (s != kFdStAttackAir)
                    {
                        st.last.djcLandOutcome = kDjcLandAirborneEnd;
                        st.last.djcLandedClean = 0;
                        st.djcLandingArmed = false;
                    }
                }

                if (s == kFdStJumpAerial)
                {
                    st.inJumpAerial = true;
                    st.jumpAerialStartFrame = f;
                    st.jumpAerialSawBit33 = (in.djcBit != 0);
                }
                else if (s == kFdStAttackAir && (prev == kFdStJumpAerial || st.inJumpAerial))
                {
                    s32 startF = st.jumpAerialStartFrame != kFdNoFrame ? st.jumpAerialStartFrame : f;
                    s16 djcF = static_cast<s16>(f - startF + 1);
                    if (djcF < 1)
                    {
                        djcF = 1;
                    }
                    u8 grade = classifyDjc(djcF);
                    st.stats.djcAttempts++;
                    if (grade == kDjcInstant)
                    {
                        st.stats.djcInstant++;
                        st.stats.djcStreak++;
                        if (st.stats.djcStreak > st.stats.djcBestStreak)
                        {
                            st.stats.djcBestStreak = st.stats.djcStreak;
                        }
                    }
                    else
                    {
                        st.stats.djcStreak = 0;
                    }

                    st.last.djcGrade = grade;
                    st.last.djcLandOutcome = kDjcLandPending;
                    st.last.djcLandedClean = 0;
                    st.last.djcHadBit33 = (st.jumpAerialSawBit33 || in.djcBit != 0) ? 1 : 0;
                    st.last.djcFrame = djcF;
                    st.last.djcMotionKind = isAttackAirMotion(in.motionKind) ? in.motionKind : e.motionKind;
                    st.last.djcEventFrame = f;
                    st.last.djcVyBefore = st.havePrevVy ? st.prevVy : in.vy;
                    st.last.djcVyAfter = in.vy;
                    st.last.djcHeight = in.y;

                    st.inJumpAerial = false;
                    st.jumpAerialStartFrame = kFdNoFrame;
                    st.djcLandingArmed = true;
                    st.djcStartFrame = f;
                    st.djcVySamplePending = true;
                }
                else if (st.inJumpAerial && s != kFdStJumpAerial)
                {
                    st.inJumpAerial = false;
                    st.jumpAerialStartFrame = kFdNoFrame;
                    st.jumpAerialSawBit33 = false;
                }
            }

            if (isHitOrDeadStatus(s))
            {
                clearFloatFlight(st);
                st.inJumpAerial = false;
                st.djcLandingArmed = false;
                st.djcVySamplePending = false;
                st.fcActArmed = false;
            }
        }

        // Frame-end updates while floating / hovering inside 0x11E (Peach), 0x120 (Mewtwo), or airborne 0x112 (Ganon).
        if (canFloat && in.status == floatSt)
        {
            if (in.ftKind == kDrillFtGanondorf && in.airborne == 0)
            {
                if (!st.inFloat)
                {
                    st.groundedFloatStatus = true;
                }
            }
            else if (!st.inFloat && !st.groundedFloatStatus)
            {
                // Fallback if already in float status without a start event.
                st.inFloat = true;
                st.isHover = (in.ftKind == kDrillFtMewtwo);
                st.floatStartFrame = f;
                st.floatEntryY = in.y;
                st.minFloatY = in.y;
                st.floatDuration = 1;
            }
            else if (st.inFloat)
            {
                if (in.y < st.minFloatY)
                {
                    st.minFloatY = in.y;
                }
                s32 dur = f - st.floatStartFrame + 1;
                if (dur > st.floatDuration)
                {
                    st.floatDuration = static_cast<s16>(dur);
                }
            }
            if (isAttackAirMotion(in.motionKind))
            {
                if (!st.floatAerialActive || st.floatAerialMotion != in.motionKind)
                {
                    st.floatAerialActive = true;
                    st.floatAerialStartFrame = f;
                    st.floatAerialMotion = in.motionKind;
                }
            }
            else if (st.floatAerialActive && in.motionKind >= 0)
            {
                st.floatAerialActive = false;
                st.floatAerialStartFrame = kFdNoFrame;
                st.floatAerialMotion = -1;
            }
        }
        else if (canFloat && in.status != floatSt)
        {
            if (st.inFloat)
            {
                // Left float status without a tracked event (e.g. abrupt reset).
                clearFloatFlight(st);
            }
            st.groundedFloatStatus = false;
        }

        // Keep motionKind updated on float/dropped-float/DJC aerials if status event
        // reported the prior subaction on the entry frame.
        if ((st.floatAerialVia33 || st.droppedFloatAerial) && in.status == kFdStAttackAir
            && !isAttackAirMotion(st.floatAerialMotion) && isAttackAirMotion(in.motionKind))
        {
            st.floatAerialMotion = in.motionKind;
        }
        if (st.djcLandingArmed && in.status == kFdStAttackAir
            && !isAttackAirMotion(st.last.djcMotionKind) && isAttackAirMotion(in.motionKind))
        {
            st.last.djcMotionKind = in.motionKind;
        }

        // Track RA-Bit[33] while inside Jump_Aerial (0x0C).
        if (st.inJumpAerial && in.status == kFdStJumpAerial && in.djcBit != 0)
        {
            st.jumpAerialSawBit33 = true;
        }

        // Expire dropped-float window if grounded or timed out.
        if (st.droppedFloatFrame != kFdNoFrame && !st.droppedFloatAerial)
        {
            if (in.airborne == 0 || (f - st.droppedFloatFrame) > kFdDroppedFloatWindow)
            {
                st.droppedFloatFrame = kFdNoFrame;
            }
        }

        // Mark first actionable frame during Landing (0x16/0x17) for actOoFcFrame.
        if (st.fcActArmed && !hit)
        {
            if (st.fcActFirstFrame == kFdNoFrame && f != st.fcActLandFrame && in.actionable != 0)
            {
                st.fcActFirstFrame = f;
            }
            if ((st.fcActFirstFrame != kFdNoFrame && (f - st.fcActFirstFrame) > kFdActGiveUp)
                || (f - st.fcActLandFrame) > kFdActGiveUp + 10)
            {
                st.fcActArmed = false;
            }
        }

        st.prevVy = in.vy;
        st.havePrevVy = true;
    }
}
}
