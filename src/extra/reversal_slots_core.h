#pragma once

#include <string.h>
#include <types.h>

#include "main/cpu/cpu_settings.h"

// Pure host-testable weighted multi-slot reversal picker with per-slot delays
// across On-Hit, On-Shield, On-Getup, and On-Ledge triggers.
// Zero Brawl headers; compiles identically on PowerPC (MWCC 4.2) and PC host tests.
// Design adapted from lil klon's dummy reversal slot expansion.
namespace tm {
namespace extra {
    enum ReversalConst {
        kRevSlotsPerTrigger = 4,
        kRevMaxDelay = 30,
        kRevMaxPrimaryDelay = 99,
        kRevMaxWeight = 100,
        // Sentinel Slot-0 action for On-Ledge when primary Ledge Option is
        // Default (0): participates in weighted selection against Slot 2..4
        // and yields back to Default ledge behavior when picked.
        kRevActionDefault = 0xFE
    };

    enum ReversalFlag {
        kRevFlagEnabled = 1 << 0
    };

    struct ReversalSlot {
        u8 action;
        u8 delay;
        u8 weight;
        u8 flags;
    };

    struct ReversalTable {
        ReversalSlot slots[cpu::kRevTriggerCount][kRevSlotsPerTrigger];
    };

    inline void reversalTableClear(ReversalTable& table)
    {
        memset(&table, 0, sizeof(table));
    }

    inline u8 clampRevWeight(u8 w)
    {
        return w > kRevMaxWeight ? static_cast<u8>(kRevMaxWeight) : w;
    }

    inline u8 clampRevDelay(u8 d, u8 hi = kRevMaxDelay)
    {
        return d > hi ? hi : d;
    }

    // Deterministic weighted-random picker over `slots[0..count-1]`.
    // Skips disabled, zero-weight, or empty (action == 0) slots.
    // Returns true and writes winning `(outAction, outDelay)` when at least one
    // eligible slot exists, else returns false.
    inline bool pickReversalSlot(const ReversalSlot* slots, int count, u32 roll, int& outAction, int& outDelay)
    {
        if (slots == NULL || count <= 0)
        {
            return false;
        }
        int n = count > kRevSlotsPerTrigger ? kRevSlotsPerTrigger : count;
        u32 total = 0;
        for (int i = 0; i < n; i++)
        {
            if ((slots[i].flags & kRevFlagEnabled) != 0 && slots[i].weight > 0 && slots[i].action != 0)
            {
                total += slots[i].weight;
            }
        }
        if (total == 0)
        {
            return false;
        }
        u32 r = roll % total;
        for (int i = 0; i < n; i++)
        {
            if ((slots[i].flags & kRevFlagEnabled) == 0 || slots[i].weight == 0 || slots[i].action == 0)
            {
                continue;
            }
            if (r < slots[i].weight)
            {
                outAction = slots[i].action;
                outDelay = slots[i].delay;
                return true;
            }
            r -= slots[i].weight;
        }
        return false;
    }

    // Resolves primary Slot 0 action from existing CpuOptions for `trigger`.
    // Random Getup and Random Ledge resolve deterministically from `roll` so
    // Slot 0 always carries a concrete nonzero action when active.
    inline u8 primaryReversalAction(int trigger, const cpu::CpuOptions& opts, u32 roll)
    {
        switch (trigger)
        {
        case cpu::kRevOnHit:
            return opts.counterGround;
        case cpu::kRevOnShield:
            return opts.counterShield;
        case cpu::kRevOnGetup:
            if (opts.getup == cpu::kGetupRandom)
            {
                u32 total = static_cast<u32>(opts.getupChance[0]) + opts.getupChance[1] + opts.getupChance[2]
                            + opts.getupChance[3];
                if (total == 0)
                {
                    return static_cast<u8>(cpu::kGetupStand + ((roll >> 16) % 4u));
                }
                u32 r = (roll >> 16) % total;
                for (int i = 0; i < 4; i++)
                {
                    if (r < opts.getupChance[i])
                    {
                        return static_cast<u8>(cpu::kGetupStand + i);
                    }
                    r -= opts.getupChance[i];
                }
                return cpu::kGetupAttack;
            }
            return opts.getup;
        case cpu::kRevOnLedge:
            if (opts.ledgeOption == cpu::kLedgeRandom)
            {
                return static_cast<u8>(cpu::kLedgeNeutral + ((roll >> 16) % 5u));
            }
            return opts.ledgeOption;
        default:
            return 0;
        }
    }

    // True when at least one secondary slot (Slot 2 persisted or session extra
    // Slots 3..4) has a non-empty enabled action for `trigger`. When false,
    // multi-slot reversal picking returns false immediately so Per-Hit Counters
    // and On-Getup zero-delay defaults are never clobbered.
    inline bool hasSecondaryReversal(int trigger, const cpu::CpuOptions& opts, const ReversalTable* extraSlots)
    {
        if (trigger < 0 || trigger >= cpu::kRevTriggerCount)
        {
            return false;
        }
        if (opts.revSlot2Action[trigger] != 0 && clampRevWeight(opts.revSlot2Weight[trigger]) > 0)
        {
            return true;
        }
        if (extraSlots != NULL)
        {
            for (int i = 2; i < kRevSlotsPerTrigger; i++)
            {
                const ReversalSlot& s = extraSlots->slots[trigger][i];
                if ((s.flags & kRevFlagEnabled) != 0 && s.action != 0 && clampRevWeight(s.weight) > 0)
                {
                    return true;
                }
            }
        }
        return false;
    }

    // Builds the 4-slot candidate array for `trigger`:
    //   Slot 0: Primary setting (`counterGround`/`counterShield`/`getup`/`ledgeOption` + `counterDelay`),
    //           weight = 100 - revSlot2Weight[trigger]
    //   Slot 1: Persisted Slot 2 (`revSlot2Action`/`revSlot2Delay`/`revSlot2Weight`)
    //   Slot 2..3: Optional session-configured extra slots (default disabled)
    inline void buildReversalSlots(int trigger, const cpu::CpuOptions& opts, const ReversalTable* extraSlots, u32 roll,
                                   ReversalSlot out[kRevSlotsPerTrigger])
    {
        memset(out, 0, sizeof(ReversalSlot) * kRevSlotsPerTrigger);
        if (trigger < 0 || trigger >= cpu::kRevTriggerCount)
        {
            return;
        }
        const u8 w2 = clampRevWeight(opts.revSlot2Weight[trigger]);
        const u8 w1 = static_cast<u8>(kRevMaxWeight - w2);

        u8 prim = primaryReversalAction(trigger, opts, roll);
        if (prim == 0 && trigger == cpu::kRevOnLedge && opts.ledgeOption == cpu::kLedgeDefault)
        {
            prim = static_cast<u8>(kRevActionDefault);
        }
        out[0].action = prim;
        out[0].delay = clampRevDelay(opts.counterDelay, kRevMaxPrimaryDelay);
        out[0].weight = w1;
        out[0].flags = out[0].action != 0 ? static_cast<u8>(kRevFlagEnabled) : 0;

        out[1].action = opts.revSlot2Action[trigger];
        out[1].delay = clampRevDelay(opts.revSlot2Delay[trigger], kRevMaxDelay);
        out[1].weight = w2;
        out[1].flags = out[1].action != 0 ? static_cast<u8>(kRevFlagEnabled) : 0;

        if (extraSlots != NULL)
        {
            for (int i = 2; i < kRevSlotsPerTrigger; i++)
            {
                out[i] = extraSlots->slots[trigger][i];
                out[i].delay = clampRevDelay(out[i].delay, kRevMaxDelay);
                out[i].weight = clampRevWeight(out[i].weight);
            }
        }
    }

    // Top-level pick helper: returns false immediately when multiReversal == 0
    // or no secondary slot is configured for `trigger`, so default single-slot
    // behavior, Per-Hit Counters, and On-Getup zero-delay stay 100% untouched.
    inline bool pickConfiguredReversal(int trigger, const cpu::CpuOptions& opts, const ReversalTable* extraSlots,
                                       u32 roll, int& outAction, int& outDelay)
    {
        if (opts.multiReversal == 0 || !hasSecondaryReversal(trigger, opts, extraSlots))
        {
            return false;
        }
        ReversalSlot slots[kRevSlotsPerTrigger];
        buildReversalSlots(trigger, opts, extraSlots, roll, slots);
        if (!pickReversalSlot(slots, kRevSlotsPerTrigger, roll, outAction, outDelay))
        {
            return false;
        }
        if (outAction == kRevActionDefault)
        {
            outAction = 0;
            outDelay = 0;
            return false;
        }
        return true;
    }
}
}
