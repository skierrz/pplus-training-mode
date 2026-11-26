#pragma once

#include <types.h>

#include "main/settings.h"

// Pure host-testable Combo Trainer state machine and 7-meter calculation core
// adapted from fudgepop01's SuperTraining (Codes/SuperTraining/main.cpp &
// FudgeMenu.h). Zero Brawl headers so all logic compiles and runs on PC via
// `make check-host`.
namespace tm {
namespace extra {
    enum {
        kCorrectNone = 0,            // soGroundShapeImpl::Correct_None
        kCorrectGroundCliffStop = 2, // soGroundShapeImpl::Correct_Ground_Cliff_Stop
        kCorrectAir = 5,             // soGroundShapeImpl::Correct_Air
        kRecoveryGraceFrames = 28    // grace delay after actionable onstage / ledge / KO before next rep
    };

    // True only when grounded on the main stage floor AND truly actionable in a
    // neutral/defensive ground status (Wait/Walk/Dash/Run/Turn/Squat/Guard) or
    // Teeter (Ottoto 0x7C / Ottoto_Wait 0x7D). Never true during Landing
    // (0x16..0x18), LandingFallSpecial (0x19), hitstun, or hitlag.
    inline bool isLandedSafeOnStage(int status, bool air, bool actionable, int hitstun, int hitlag, float x,
                                    float floorLeftX, float floorRightX)
    {
        if (air || hitstun > 0 || hitlag > 0 || x < floorLeftX || x > floorRightX)
        {
            return false;
        }
        if (status >= 0x16 && status <= 0x19)
        {
            return false;
        }
        if (status == 0x7C || status == 0x7D)
        {
            return true;
        }
        if (!actionable)
        {
            return false;
        }
        return (status >= 0x00 && status <= 0x09) || (status >= 0x11 && status <= 0x15)
               || status == 0x1A || status == 0x1B;
    }

    struct ComboSlotState {
        float anchorX;
        float anchorY;
        bool anchorValid;
        bool wasInHitstun;
        bool postResetArmed;
        bool restoreCorrect;    // explicit 1-frame flag: restores ground/air correct on next frame
        u8 restoreCorrectKind;  // kCorrectGroundCliffStop (grounded) or kCorrectAir (airborne)
        int comboTimer;
        int postResetTimer;
        int recoveryAirFrames;
        u8 recoveryGraceFrames; // countdown before next recovery rep warps
        int maxHitlag;
        int maxHitstun;
        int maxShieldStun;
        int maxIasa;
        int maxLedgeIntang;
        int maxIntang;
        float diX;
        float diY;
        u32 resetCount;
        u32 recoveryResets;
        u8 lastCorrectWritten; // kCorrectNone, kCorrectAir, or 0xFF
        bool hadLedgeIntang;
    };

    struct ComboStepInput {
        int status;
        int hitlag;
        int hitstun;
        int shieldStun;
        int framesUntilIasa; // from extra::frameDataActIn (-1 if unknown, 0 if actionable)
        int xluFrames;       // game::Body::xluFrames (ledge / respawn intangibility)
        int invFrames;       // game::Body::invFrames
        bool bodyIntangible; // game::Body::intangible (spotdodge, roll, airdodge, invincible moves)
        bool onLedge;        // CliffCatch / CliffWait / Cliff*
        bool air;
        bool outOfPlay;
        bool belowRecoveryReset;
        bool landedSafeOnStage;
        float x, y;
        float stickX, stickY;
    };

    struct ComboStepAction {
        bool setCorrectNone;
        bool restoreCorrect;
        u8 restoreCorrectKind;
        bool doWarp;
        bool warpFalling;
        bool resetP1;
        bool pickRandomPos;
        bool pickOffstagePos;
        bool pickRandomDamage;
        bool applyFixedDamage;
    };

    inline int comboResetDelayFrames(u8 delayMode)
    {
        static const u8 kDelays[main::kComboDelayCount] = {0, 15, 30, 45, 60};
        return delayMode < main::kComboDelayCount ? static_cast<int>(kDelays[delayMode]) : 30;
    }

    struct ComboMeterValues {
        u8 activeMask;         // bits from main::ComboMeterBit currently active
        float hitlagRatio;     // 0..1
        float hitstunRatio;    // 0..1
        float shieldStunRatio; // 0..1
        float diX;             // -1..1
        float diY;             // -1..1
        float iasaRatio;       // 0..1 (remaining / max)
        float ledgeIntangRatio;// 0..1
        float intangRatio;     // 0..1
        s16 hitlagFrames;
        s16 hitstunFrames;
        s16 shieldStunFrames;
        s16 iasaFrames;
        s16 ledgeIntangFrames;
        s16 intangFrames;
    };

    inline void comboSlotReset(ComboSlotState& st)
    {
        st.anchorX = 0.0f;
        st.anchorY = 0.0f;
        st.anchorValid = false;
        st.wasInHitstun = false;
        st.postResetArmed = false;
        st.restoreCorrect = false;
        st.restoreCorrectKind = kCorrectAir;
        st.comboTimer = 0;
        st.postResetTimer = 0;
        st.recoveryAirFrames = 0;
        st.recoveryGraceFrames = 0;
        st.maxHitlag = 0;
        st.maxHitstun = 0;
        st.maxShieldStun = 0;
        st.maxIasa = 0;
        st.maxLedgeIntang = 0;
        st.maxIntang = 0;
        st.diX = 0.0f;
        st.diY = 0.0f;
        st.resetCount = 0;
        st.recoveryResets = 0;
        st.lastCorrectWritten = 0xFF;
        st.hadLedgeIntang = false;
    }

    inline float ratioClamp(int cur, int peak)
    {
        if (cur <= 0 || peak <= 0)
        {
            return 0.0f;
        }
        float r = static_cast<float>(cur) / static_cast<float>(peak);
        return r > 1.0f ? 1.0f : (r < 0.0f ? 0.0f : r);
    }

    inline bool isDownOrGrabStatus(int status)
    {
        // Down..Tech (0x4A..0x64), CapturePulled..CaptureThrown (0x3D..0x42),
        // and Yoshi Capture (0x107..0x10A).
        return (status >= 0x4A && status <= 0x64) || (status >= 0x3D && status <= 0x42)
               || (status >= 0x107 && status <= 0x10A);
    }

    // Updates peak denominators and computes all 7 floating state meters gated
    // by `enabledMeters` (main::ComboMeterBit).
    inline void computeComboMeters(u8 enabledMeters, const ComboStepInput& in, ComboSlotState& st,
                                   ComboMeterValues& out)
    {
        out.activeMask = 0;
        out.hitlagRatio = 0.0f;
        out.hitstunRatio = 0.0f;
        out.shieldStunRatio = 0.0f;
        out.diX = 0.0f;
        out.diY = 0.0f;
        out.iasaRatio = 0.0f;
        out.ledgeIntangRatio = 0.0f;
        out.intangRatio = 0.0f;
        out.hitlagFrames = static_cast<s16>(in.hitlag > 0 ? in.hitlag : 0);
        out.hitstunFrames = static_cast<s16>(in.hitstun > 0 ? in.hitstun : 0);
        out.shieldStunFrames = static_cast<s16>(in.shieldStun > 0 ? in.shieldStun : 0);
        out.iasaFrames = static_cast<s16>(in.framesUntilIasa > 0 ? in.framesUntilIasa : 0);

        // Hitlag peak
        if (in.hitlag > 0)
        {
            if (in.hitlag > st.maxHitlag)
            {
                st.maxHitlag = in.hitlag;
            }
            if ((enabledMeters & main::kMeterHitlag) != 0)
            {
                out.hitlagRatio = ratioClamp(in.hitlag, st.maxHitlag);
                out.activeMask |= main::kMeterHitlag;
            }
        }
        else
        {
            st.maxHitlag = 0;
        }

        // Hitstun peak + DI stick capture
        if (in.hitstun > 0)
        {
            if (in.hitstun > st.maxHitstun)
            {
                st.maxHitstun = in.hitstun;
            }
            if (in.hitlag > 0 || (st.diX == 0.0f && st.diY == 0.0f))
            {
                st.diX = in.stickX;
                st.diY = in.stickY;
            }
            if ((enabledMeters & main::kMeterHitstun) != 0)
            {
                out.hitstunRatio = ratioClamp(in.hitstun, st.maxHitstun);
                out.activeMask |= main::kMeterHitstun;
            }
            if ((enabledMeters & main::kMeterDi) != 0)
            {
                out.diX = st.diX;
                out.diY = st.diY;
                out.activeMask |= main::kMeterDi;
            }
        }
        else
        {
            st.maxHitstun = 0;
            st.diX = 0.0f;
            st.diY = 0.0f;
        }

        // Shieldstun peak
        if (in.shieldStun > 0)
        {
            if (in.shieldStun > st.maxShieldStun)
            {
                st.maxShieldStun = in.shieldStun;
            }
            if ((enabledMeters & main::kMeterShieldstun) != 0)
            {
                out.shieldStunRatio = ratioClamp(in.shieldStun, st.maxShieldStun);
                out.activeMask |= main::kMeterShieldstun;
            }
        }
        else
        {
            st.maxShieldStun = 0;
        }

        // Time until IASA (actionable) peak
        if (in.framesUntilIasa > 0 && in.hitstun == 0 && in.shieldStun == 0)
        {
            if (in.framesUntilIasa > st.maxIasa)
            {
                st.maxIasa = in.framesUntilIasa;
            }
            if ((enabledMeters & main::kMeterIasa) != 0)
            {
                out.iasaRatio = ratioClamp(in.framesUntilIasa, st.maxIasa);
                out.activeMask |= main::kMeterIasa;
            }
        }
        else
        {
            st.maxIasa = 0;
        }

        // Ledge intangibility (xluFrames acquired while on ledge, continuing post-ledge)
        if (in.onLedge && in.xluFrames > 0)
        {
            st.hadLedgeIntang = true;
        }
        if (in.xluFrames <= 0)
        {
            st.hadLedgeIntang = false;
            st.maxLedgeIntang = 0;
        }
        int ledgeIntang = st.hadLedgeIntang ? in.xluFrames : 0;
        out.ledgeIntangFrames = static_cast<s16>(ledgeIntang > 0 ? ledgeIntang : 0);
        if (ledgeIntang > 0)
        {
            if (ledgeIntang > st.maxLedgeIntang)
            {
                st.maxLedgeIntang = ledgeIntang;
            }
            if ((enabledMeters & main::kMeterLedgeIntang) != 0)
            {
                out.ledgeIntangRatio = ratioClamp(ledgeIntang, st.maxLedgeIntang);
                out.activeMask |= main::kMeterLedgeIntang;
            }
        }

        // General intangibility / invincibility (invFrames, non-ledge xluFrames, or active bodyIntangible)
        int bodyIntang = in.invFrames > 0 ? in.invFrames : (!st.hadLedgeIntang ? in.xluFrames : 0);
        if (bodyIntang <= 0 && !st.hadLedgeIntang && in.bodyIntangible)
        {
            bodyIntang = 1;
        }
        out.intangFrames = static_cast<s16>(bodyIntang > 0 ? bodyIntang : 0);
        if (bodyIntang > 0)
        {
            if (bodyIntang > st.maxIntang)
            {
                st.maxIntang = bodyIntang;
            }
            if ((enabledMeters & main::kMeterIntang) != 0)
            {
                out.intangRatio = ratioClamp(bodyIntang, st.maxIntang);
                out.activeMask |= main::kMeterIntang;
            }
        }
        else
        {
            st.maxIntang = 0;
        }
    }

    // Steps one frame of Combo Trainer reset/noclip/recovery-trainer logic for
    // one CPU fighter.
    inline void stepComboTrainer(const main::ComboTrainerSettings& cfg, const ComboStepInput& in, ComboSlotState& st,
                                 ComboStepAction& act)
    {
        act.setCorrectNone = false;
        act.restoreCorrect = false;
        act.restoreCorrectKind = kCorrectAir;
        act.doWarp = false;
        act.warpFalling = false;
        act.resetP1 = false;
        act.pickRandomPos = false;
        act.pickOffstagePos = false;
        act.pickRandomDamage = false;
        act.applyFixedDamage = false;

        // Explicit 1-frame restoreCorrect check: restores Correct_Ground_Cliff_Stop
        // after a grounded reset or Correct_Air after an airborne reset on the
        // frame immediately following noclip.
        if (st.restoreCorrect)
        {
            act.restoreCorrect = true;
            act.restoreCorrectKind = st.restoreCorrectKind;
            st.restoreCorrect = false;
            st.lastCorrectWritten = st.restoreCorrectKind;
        }

        if (!cfg.enabled || in.outOfPlay)
        {
            if (!cfg.enabled)
            {
                st.wasInHitstun = false;
                st.postResetArmed = false;
                st.comboTimer = 0;
                st.postResetTimer = 0;
                st.recoveryAirFrames = 0;
                st.recoveryGraceFrames = 0;
            }
            else if (cfg.recoveryTrainer && in.status == 0xBE)
            {
                // CPU was KO'd and entered Rebirth: brief grace pause before next offstage rep.
                if (st.recoveryGraceFrames == 0)
                {
                    st.recoveryGraceFrames = kRecoveryGraceFrames;
                }
                if (st.recoveryGraceFrames > 1)
                {
                    st.recoveryGraceFrames--;
                    return;
                }
                st.recoveryGraceFrames = 0;
                st.recoveryResets++;
                st.recoveryAirFrames = 0;
                st.wasInHitstun = false;
                st.comboTimer = 0;
                act.doWarp = true;
                act.warpFalling = true;
                act.pickOffstagePos = true;
                if (cfg.noclip)
                {
                    act.setCorrectNone = true;
                    st.restoreCorrect = true;
                    st.restoreCorrectKind = kCorrectAir;
                    st.lastCorrectWritten = kCorrectNone;
                }
                if (cfg.randomizeDamage)
                {
                    act.pickRandomDamage = true;
                }
                else
                {
                    act.applyFixedDamage = true;
                }
            }
            else if (cfg.fixPosition && !cfg.recoveryTrainer && (st.wasInHitstun || st.postResetArmed))
            {
                // Immediate reset path on KO once out of Dead/Standby (Rebirth 0xBE).
                if (in.status == 0xBE)
                {
                    st.wasInHitstun = false;
                    st.postResetArmed = false;
                    st.comboTimer = 0;
                    st.postResetTimer = 0;
                    st.resetCount++;
                    act.doWarp = true;
                    act.resetP1 = cfg.resetP1 != 0;
                    act.warpFalling = cfg.randomizePosition ? (cfg.randOnGround == 0) : false;
                    act.pickRandomPos = cfg.randomizePosition != 0;
                    if (cfg.noclip)
                    {
                        act.setCorrectNone = true;
                        st.restoreCorrect = true;
                        st.restoreCorrectKind = act.warpFalling ? kCorrectAir : kCorrectGroundCliffStop;
                        st.lastCorrectWritten = kCorrectNone;
                    }
                    if (cfg.randomizeDamage)
                    {
                        act.pickRandomDamage = true;
                    }
                    else
                    {
                        act.applyFixedDamage = true;
                    }
                }
            }
            return;
        }

        // Capture initial fixPosition anchor when first enabled on a safe frame.
        if (!st.anchorValid && !in.outOfPlay)
        {
            st.anchorX = in.x;
            st.anchorY = in.y;
            st.anchorValid = true;
        }

        // 1. Recovery Trainer loop: repeatedly places CPU offstage for edgeguard
        // practice once it falls too low, or after a clean grace window once it
        // grabs ledge or becomes truly actionable back on stage.
        if (cfg.recoveryTrainer)
        {
            if (in.air)
            {
                st.recoveryAirFrames++;
            }
            bool initialPlace = st.recoveryResets == 0 && (in.landedSafeOnStage || in.onLedge);
            bool graceTrigger = st.recoveryResets > 0 && st.recoveryAirFrames >= 12
                                && (in.landedSafeOnStage || in.onLedge) && in.hitstun == 0 && in.hitlag == 0;
            bool needRespawn = in.belowRecoveryReset || initialPlace;
            if (!needRespawn)
            {
                if (graceTrigger)
                {
                    if (st.recoveryGraceFrames == 0)
                    {
                        st.recoveryGraceFrames = kRecoveryGraceFrames;
                    }
                    else if (st.recoveryGraceFrames > 1)
                    {
                        st.recoveryGraceFrames--;
                    }
                    else
                    {
                        st.recoveryGraceFrames = 0;
                        needRespawn = true;
                    }
                }
                else
                {
                    st.recoveryGraceFrames = 0;
                }
            }
            if (needRespawn)
            {
                st.recoveryResets++;
                st.recoveryAirFrames = 0;
                st.recoveryGraceFrames = 0;
                st.wasInHitstun = false;
                st.comboTimer = 0;
                act.doWarp = true;
                act.warpFalling = true;
                act.pickOffstagePos = true;
                if (cfg.noclip)
                {
                    act.setCorrectNone = true;
                    st.restoreCorrect = true;
                    st.restoreCorrectKind = kCorrectAir;
                    st.lastCorrectWritten = kCorrectNone;
                }
                if (cfg.randomizeDamage)
                {
                    act.pickRandomDamage = true;
                }
                else
                {
                    act.applyFixedDamage = true;
                }
                return;
            }
            return;
        }

        // 2. Hitstun-drop auto-reset (fixPosition) with configurable Reset Delay
        //    and frame-trap / dodge-read hit continuation.
        if (cfg.fixPosition)
        {
            if (in.status == 0xBE && (st.wasInHitstun || st.postResetArmed))
            {
                st.wasInHitstun = false;
                st.postResetArmed = false;
                st.comboTimer = 0;
                st.postResetTimer = 0;
                st.resetCount++;
                act.doWarp = true;
                act.resetP1 = cfg.resetP1 != 0;
                act.warpFalling = cfg.randomizePosition ? (cfg.randOnGround == 0) : false;
                act.pickRandomPos = cfg.randomizePosition != 0;
                if (cfg.noclip)
                {
                    act.setCorrectNone = true;
                    st.restoreCorrect = true;
                    st.restoreCorrectKind = act.warpFalling ? kCorrectAir : kCorrectGroundCliffStop;
                    st.lastCorrectWritten = kCorrectNone;
                }
                if (cfg.randomizeDamage)
                {
                    act.pickRandomDamage = true;
                }
                else
                {
                    act.applyFixedDamage = true;
                }
                return;
            }
            int delayBase = comboResetDelayFrames(cfg.resetDelay);
            int totalDelay = delayBase + static_cast<int>(cfg.comboTimerAdjustment);
            if (in.hitstun > 0)
            {
                int eff = in.hitstun + totalDelay;
                if (eff > 0)
                {
                    st.wasInHitstun = true;
                    st.postResetArmed = false;
                    st.postResetTimer = 0;
                    st.comboTimer = eff;
                    return;
                }
            }
            if (st.wasInHitstun)
            {
                if (isDownOrGrabStatus(in.status))
                {
                    // Stay in combo string through knockdown/tech/grab; cancel any reset timer.
                    st.postResetArmed = false;
                    st.postResetTimer = 0;
                    return;
                }
                if (in.hitstun == 0 && totalDelay > 0)
                {
                    if (!st.postResetArmed)
                    {
                        st.postResetArmed = true;
                        st.postResetTimer = totalDelay;
                    }
                    if (st.postResetTimer > 0)
                    {
                        st.postResetTimer--;
                        return;
                    }
                }

                st.wasInHitstun = false;
                st.postResetArmed = false;
                st.comboTimer = 0;
                st.postResetTimer = 0;
                st.resetCount++;
                act.doWarp = true;
                act.resetP1 = cfg.resetP1 != 0;
                act.warpFalling = cfg.randomizePosition ? (cfg.randOnGround == 0) : false;
                act.pickRandomPos = cfg.randomizePosition != 0;
                if (cfg.noclip)
                {
                    act.setCorrectNone = true;
                    st.restoreCorrect = true;
                    st.restoreCorrectKind = act.warpFalling ? kCorrectAir : kCorrectGroundCliffStop;
                    st.lastCorrectWritten = kCorrectNone;
                }
                if (cfg.randomizeDamage)
                {
                    act.pickRandomDamage = true;
                }
                else
                {
                    act.applyFixedDamage = true;
                }
            }
        }
    }
}
}
