#pragma once

#include <types.h>

#include "main/cpu/cpu_settings.h"
#include "main/cpu/fighter_input.h"

// Pure host-testable on-ledge reversal / getup logic for Project+ 3.2.1.
// Trigger design inspired by lil klon's PunchTraining On-Ledge reversal
// (triggers on Cliff_Wait 0x75, never during Cliff_Catch 0x74 startup).
// Status IDs verified against lib/BrawlHeaders/Brawl/Include/ft/fighter.h:135-143.
namespace tm {
namespace cpu {
    enum {
        kLedgeStatusCatchMove = 0x73, // Fighter::Status::Cliff_Catch_Move
        kLedgeStatusCatch = 0x74,     // Fighter::Status::Cliff_Catch
        kLedgeStatusWait = 0x75,      // Fighter::Status::Cliff_Wait
        kLedgeStatusAttack = 0x76,    // Fighter::Status::Cliff_Attack
        kLedgeStatusClimb = 0x77,     // Fighter::Status::Cliff_Climb
        kLedgeStatusEscape = 0x78,    // Fighter::Status::Cliff_Escape
        kLedgeStatusJump1 = 0x79,     // Fighter::Status::Cliff_Jump1
        kLedgeStatusJump2 = 0x7A,     // Fighter::Status::Cliff_Jump2
        kLedgeStatusJump3 = 0x7B      // Fighter::Status::Cliff_Jump3
    };

    // True while hanging on the ledge before any ledge option executes.
    inline bool isCliffHoldStatus(int status)
    {
        return status >= kLedgeStatusCatchMove && status <= kLedgeStatusWait;
    }

    // True only on actionable Cliff_Wait frames (excludes Cliff_Catch startup).
    inline bool isCliffWaitStatus(int status)
    {
        return status == kLedgeStatusWait;
    }

    // True while executing an on-stage ledge getup animation (Attack/Climb/Escape).
    inline bool isCliffGetupStatus(int status)
    {
        return status >= kLedgeStatusAttack && status <= kLedgeStatusEscape;
    }

    struct LedgeState {
        bool active;
        u8 chosen;      // resolved concrete LedgeOption (kLedgeNeutral..kLedgeDrop)
        u16 waitFrames; // actionable Cliff_Wait frames elapsed since entering Cliff_Wait
        u16 holdFrames; // frames elapsed since firing the chosen ledge option
    };

    struct LedgeInput {
        bool active; // true when ledge logic owns this frame's inputs
        u32 buttons;
        float x;
        float y;
    };

    inline void ledgeStateReset(LedgeState& st)
    {
        st.active = false;
        st.chosen = kLedgeDefault;
        st.waitFrames = 0;
        st.holdFrames = 0;
    }

    // Resolves kLedgeRandom deterministically across Neutral/Roll/Attack/Jump/Drop.
    inline u8 resolveLedgeOption(u8 option, u32 rng)
    {
        if (option == kLedgeRandom)
        {
            return static_cast<u8>(kLedgeNeutral + (rng % 5u));
        }
        if (option >= kLedgeNeutral && option <= kLedgeDrop)
        {
            return option;
        }
        return kLedgeDefault;
    }

    // Steps the on-ledge state machine for one frame.
    // - Returns false (out.active == false) when option == kLedgeDefault or the
    //   fighter is not in Cliff_Catch_Move / Cliff_Catch / Cliff_Wait.
    // - During Cliff_Catch_Move (0x73) and Cliff_Catch (0x74), outputs neutral
    //   so Game AI / PPlusAI cannot buffer an unwanted action during startup,
    //   while keeping waitFrames at 0 until Cliff_Wait (0x75) begins.
    // - During Cliff_Wait (0x75), waits `counterDelay` actionable frames (0 =
    //   fires on frame 1 of Cliff_Wait), then emits the verified P+ 3.2.1 input:
    //     Neutral -> stick toward stage (+lr, 0)    -> Cliff_Climb (0x77)
    //     Roll    -> guard button (kGuard)          -> Cliff_Escape (0x78)
    //     Attack  -> attack button (kAttack)        -> Cliff_Attack (0x76)
    //     Jump    -> jump button (kJump)            -> Cliff_Jump1..3 (0x79..0x7B)
    //     Drop    -> stick away from stage (-lr, 0) -> Fall (0x0E) without fastfall
    inline bool stepLedgeOption(u8 option, u8 counterDelay, int status, float lr, u32 rng, LedgeState& st,
                                LedgeInput& out)
    {
        out.active = false;
        out.buttons = 0;
        out.x = 0.0f;
        out.y = 0.0f;

        if (option == kLedgeDefault || option >= kLedgeOptionCount)
        {
            ledgeStateReset(st);
            return false;
        }

        if (!isCliffHoldStatus(status))
        {
            ledgeStateReset(st);
            return false;
        }

        // Preempt recovery during Cliff_Catch_Move / Cliff_Catch with neutral
        // input so delay starts cleanly on frame 1 of Cliff_Wait (0x75).
        if (!isCliffWaitStatus(status))
        {
            ledgeStateReset(st);
            out.active = true;
            return true;
        }

        out.active = true;
        if (!st.active)
        {
            st.active = true;
            st.chosen = resolveLedgeOption(option, rng);
            st.waitFrames = 0;
            st.holdFrames = 0;
        }

        if (st.waitFrames < counterDelay)
        {
            st.waitFrames++;
            return true;
        }

        // Emit on even holdFrames (frame 0 immediately, then retry every 2
        // frames if still in Cliff_Wait) so fresh edge triggers always fire.
        bool pulse = (st.holdFrames & 1u) == 0u;
        st.holdFrames++;
        if (!pulse)
        {
            return true;
        }

        float facingStage = lr < 0.0f ? -1.0f : 1.0f;
        switch (st.chosen)
        {
        case kLedgeNeutral:
            out.x = facingStage;
            break;
        case kLedgeRoll:
            out.buttons = button::kGuard;
            break;
        case kLedgeAttack:
            out.buttons = button::kAttack;
            break;
        case kLedgeJump:
            out.buttons = button::kJump;
            break;
        case kLedgeDrop:
            out.x = -facingStage;
            break;
        default:
            break;
        }
        return true;
    }
}
}
