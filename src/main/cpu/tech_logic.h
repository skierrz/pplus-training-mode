#pragma once

#include "main/cpu/cpu_settings.h"
#include "main/game/status_ids.h"

// Pure tech and slip timing decisions shared by options.cpp and host tests.
namespace tm {
namespace cpu {
    enum {
        // Hold tech-roll stick only on the landing frame and 1 frame prior
        // (for ECB/slope prediction margin), avoiding pre-tech air drift.
        // P+ Fighter.pac Term_Passive_Fb (0x80FAB024) and Passive_Fb entry
        // (0x80FB563C) sample stickX on the landing frame itself.
        kTechRollLead = 1,
        kStatusSlip = 0x89,
        kStatusSlipDown = 0x8A,
        kStatusSlipTurn = 0x8B,
        kStatusSlipDash = 0x8C,
        kStatusSlipWait = 0x8D,
        kStatusSlipStand = 0x8E,
        kStatusSlipStandAttack = 0x8F,
        kStatusSlipStandF = 0x90,
        kStatusSlipStandB = 0x91
    };

    // P+ Naner Tripping is now Techable v4 (Source/Injects/MoveDef/Items.asm:10-39):
    // Slip (0x89) and Slip_Down (0x8A) enable Passive / Passive_Fb once
    // IC_Basic[0] (Var_Float_Motion_Frame) >= 20.0.
    static const float kSlipTechFrame = 20.0f;

    inline bool isSlipTechStatus(int s)
    {
        return s == kStatusSlip || s == kStatusSlipDown;
    }

    inline bool isDownStatus(int s)
    {
        return (s >= 0x4A && s <= 0x53) || (s >= kStatusSlipTurn && s <= kStatusSlipStandB);
    }

    inline bool isGetupWaitStatus(int s)
    {
        return s == 0x4D || s == 0x4E || s == 0x50 || s == kStatusSlipWait;
    }

    // Frames until Slip / Slip_Down reaches kSlipTechFrame (20.0), where 0
    // means the coming frame reaches or exceeds 20.0; -1 if motion is stopped.
    inline int framesToSlipTech(float motionFrame, float motionRate)
    {
        if (motionRate <= 0.0f)
        {
            return -1;
        }
        float nextFrame = motionFrame + motionRate;
        if (nextFrame >= kSlipTechFrame - 0.01f)
        {
            return 0;
        }
        float rem = (kSlipTechFrame - nextFrame) / motionRate;
        int n = static_cast<int>(rem);
        if (static_cast<float>(n) < rem - 0.01f)
        {
            n++;
        }
        return n;
    }

    // Direction vector for a straight-down ground ray of length `depth`
    // (soGroundModuleImpl::rayCheck / stRayCheck take (start, dir), not end).
    inline void groundRayDir(float depth, float& dx, float& dy)
    {
        dx = 0.0f;
        dy = -depth;
    }

    // True when `s` is a jump status (ground jump, midair jump, or multi-jumper
    // Fly 0x0D from Fighter::Status::Kind).
    inline bool isJumpStatus(int s)
    {
        return s == game::kStatusJump || s == game::kStatusJumpAerial || s == game::kStatusFly;
    }

    // True when a counter action has visibly started (left waiting, landing,
    // guard, or hit statuses). Multi-jumper Fly (0x0D) counts alongside Jump
    // and JumpAerial; LandingFallSpecial counts (only airdodge or special lands
    // in it).
    inline bool counterActionStarted(int s, bool shieldKind)
    {
        bool waiting = (s >= game::kStatusWait && s <= game::kStatusFallAerial && s != game::kStatusJumpSquat
                        && !isJumpStatus(s) && s != game::kStatusDash)
                       || (s >= game::kStatusSquat && s < game::kStatusLandingFallSpecial)
                       || s == game::kStatusGuardOff || (s >= game::kStatusDamage && s <= 0x49)
                       || (shieldKind && (s == game::kStatusGuardOn || s == game::kStatusGuard));
        return !waiting;
    }

    // Roll stick X to apply for a ground or slip tech, or 0 if stick should
    // stay untouched. Applied only once a tech guard window is open, outside
    // hitlag, within kTechRollLead frames of the tech frame, and either when
    // allowStick is true or on the tech frame itself (nLand == 0).
    // Magnitude stays midway between roll (3198, 0.2) and wiggle (3299, 0.75).
    inline float techRollStickX(int tech, float toward, int nLand, bool techPressed, int hitlag, bool allowStick,
                                float rollParam, float wiggleParam)
    {
        if (!techPressed || hitlag > 0 || (tech != kTechAway && tech != kTechTowards))
        {
            return 0.0f;
        }
        if (nLand < 0 || nLand > kTechRollLead || (!allowStick && nLand > 0))
        {
            return 0.0f;
        }
        float side = tech == kTechAway ? -toward : toward;
        return side * 0.5f * (rollParam + wiggleParam);
    }

    // Matches main::PreTechDrift in main/settings.h.
    enum PreTechDriftMode {
        kDriftOff = 0,
        kDriftRandom = 1,
        kDriftIn = 2,
        kDriftOut = 3,
        kDriftTowardTech = 4,
        kDriftAwayTech = 5
    };

    // Pre-tech tumble aerial drift stick X, or 0 if stick stays neutral.
    // Defers to techRollStickX during the final landing window (0..kTechRollLead)
    // and keeps magnitude strictly below wiggleParam so tumble is never broken.
    inline float preTechDriftStickX(int driftMode, int tech, float toward, u32 roll, int nLand, int hitlag,
                                    bool allowStick, float wiggleParam)
    {
        if (driftMode == kDriftOff || hitlag > 0 || !allowStick)
        {
            return 0.0f;
        }
        if (nLand >= 0 && nLand <= kTechRollLead)
        {
            return 0.0f;
        }
        float side = 0.0f;
        if (driftMode == kDriftIn)
        {
            side = toward;
        }
        else if (driftMode == kDriftOut)
        {
            side = -toward;
        }
        else if (driftMode == kDriftRandom)
        {
            u32 pick = roll % 3u;
            side = pick == 0u ? -toward : (pick == 2u ? toward : 0.0f);
        }
        else if ((driftMode == kDriftTowardTech || driftMode == kDriftAwayTech)
                 && (tech == kTechTowards || tech == kTechAway))
        {
            float tSide = tech == kTechTowards ? toward : -toward;
            side = driftMode == kDriftTowardTech ? tSide : -tSide;
        }
        if (side == 0.0f)
        {
            return 0.0f;
        }
        float mag = wiggleParam > 0.15f ? wiggleParam - 0.15f : 0.0f;
        if (mag > 0.60f)
        {
            mag = 0.60f;
        }
        return side * mag;
    }
}
}
