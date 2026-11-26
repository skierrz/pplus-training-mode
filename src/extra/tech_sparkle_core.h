#pragma once

#include <types.h>

#include "main/game/status_ids.h"
#include "main/settings.h"

namespace tm {
namespace extra {
    enum {
        kTsNoFrame = -1,
        kTsMaxEvents = 8,
        kTsWavedashMaxAir = 30,
        kTsShorthopDeferFrames = 3,
        kTsShortHopFlag = 1 << 6
    };

    enum TechSparkleCue {
        kSparkleNone = 0,
        kSparklePerfectWavedash = 1,
        kSparkleShorthop = 2
    };

    struct TechSparkleEvent {
        s16 status;
        s16 prev;
        u32 flags;
    };

    struct TechSparkleInput {
        s32 frame;
        u8 mode; // main::TechSparkleMode
        u8 eventCount;
        TechSparkleEvent events[kTsMaxEvents];
    };

    struct TechSparkleSlot {
        s32 jumpSquat;
        s32 air;
        s32 airdodge;
        s32 pendingShorthop;
    };

    inline void techSparkleResetSlot(TechSparkleSlot& slot)
    {
        slot.jumpSquat = kTsNoFrame;
        slot.air = kTsNoFrame;
        slot.airdodge = kTsNoFrame;
        slot.pendingShorthop = kTsNoFrame;
    }

    inline bool techSparkleIsImmediateShAction(int s)
    {
        return s == game::kStatusAttackAirFirst || (s >= game::kStatusSpecialN && s < game::kStatusFinal);
    }

    // Steps one tracked human fighter for the finished frame and returns which
    // native ef_common cue (if any) should fire on this frame.
    //  - Perfect Wavedash: Frame-1 wavedash landing (JumpSquat -> Jump ->
    //    EscapeAir on same frame -> LandingFallSpecial). Never emits Shorthop.
    //  - Shorthop: JumpSquat -> Jump with kShortHopFlag set when no EscapeAir
    //    occurs on the takeoff frame.
    //  - Both: defers the Shorthop cue across kTsShorthopDeferFrames (3f) unless
    //    an aerial/special starts immediately, so 1f/2f/3f shorthop-button
    //    wavedashes never double-fire or falsely trigger the Shorthop sparkle.
    inline u8 stepTechSparkle(TechSparkleSlot& slot, const TechSparkleInput& in)
    {
        if (in.mode == main::kTechSparkleOff)
        {
            techSparkleResetSlot(slot);
            return kSparkleNone;
        }
        if ((slot.jumpSquat != kTsNoFrame && in.frame < slot.jumpSquat)
            || (slot.air != kTsNoFrame && in.frame < slot.air)
            || (slot.pendingShorthop != kTsNoFrame && in.frame < slot.pendingShorthop))
        {
            techSparkleResetSlot(slot);
        }

        bool wantWd = in.mode == main::kTechSparkleWavedash || in.mode == main::kTechSparkleBoth;
        bool wantSh = in.mode == main::kTechSparkleShorthop || in.mode == main::kTechSparkleBoth;
        bool deferSh = wantWd && wantSh;
        if (!wantSh)
        {
            slot.pendingShorthop = kTsNoFrame;
        }

        bool sawEscapeAirThisFrame = false;
        int n = in.eventCount < kTsMaxEvents ? static_cast<int>(in.eventCount) : kTsMaxEvents;
        for (int k = 0; k < n; k++)
        {
            if (in.events[k].status == game::kStatusEscapeAir)
            {
                sawEscapeAirThisFrame = true;
                break;
            }
        }

        u8 cue = kSparkleNone;
        for (int k = 0; k < n; k++)
        {
            const TechSparkleEvent& e = in.events[k];
            int s = e.status;
            if (s == game::kStatusJumpSquat)
            {
                slot.jumpSquat = in.frame;
                slot.air = kTsNoFrame;
                slot.airdodge = kTsNoFrame;
                slot.pendingShorthop = kTsNoFrame;
            }
            else if (s == game::kStatusJump && e.prev == game::kStatusJumpSquat && slot.jumpSquat != kTsNoFrame)
            {
                slot.air = in.frame;
                slot.pendingShorthop = kTsNoFrame;
                if (wantSh && (e.flags & kTsShortHopFlag) != 0 && !sawEscapeAirThisFrame)
                {
                    if (deferSh)
                    {
                        slot.pendingShorthop = in.frame;
                    }
                    else
                    {
                        cue = kSparkleShorthop;
                    }
                }
            }
            else if (s == game::kStatusEscapeAir)
            {
                slot.pendingShorthop = kTsNoFrame;
                if (slot.air != kTsNoFrame && in.frame - slot.air < kTsWavedashMaxAir)
                {
                    slot.airdodge = in.frame;
                }
                else
                {
                    slot.airdodge = kTsNoFrame;
                }
            }
            else if (s == game::kStatusLandingFallSpecial && e.prev == game::kStatusEscapeAir
                     && slot.airdodge != kTsNoFrame && slot.air != kTsNoFrame)
            {
                int wdFrame = slot.airdodge - slot.air + 1;
                if (wantWd && wdFrame == 1)
                {
                    cue = kSparklePerfectWavedash;
                }
                techSparkleResetSlot(slot);
            }
            else if (s != game::kStatusJump && s != game::kStatusEscapeAir)
            {
                if (deferSh && slot.pendingShorthop != kTsNoFrame && techSparkleIsImmediateShAction(s)
                    && cue == kSparkleNone)
                {
                    cue = kSparkleShorthop;
                }
                techSparkleResetSlot(slot);
            }
        }

        if (deferSh && slot.pendingShorthop != kTsNoFrame && !sawEscapeAirThisFrame)
        {
            if (in.frame - slot.pendingShorthop >= kTsShorthopDeferFrames)
            {
                if (cue == kSparkleNone)
                {
                    cue = kSparkleShorthop;
                }
                slot.pendingShorthop = kTsNoFrame;
            }
        }
        return cue;
    }
}
}
