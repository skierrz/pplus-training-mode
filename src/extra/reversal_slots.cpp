#include "extra/reversal_slots.h"

#include "main/cpu/actions.h"
#include "main/settings.h"

namespace tm {
namespace extra {
    static ReversalTable s_extraSlots;

    void reversalSlotsAttach()
    {
        reversalTableClear(s_extraSlots);
    }

    void reversalSlotsReset()
    {
        reversalTableClear(s_extraSlots);
    }

    void reversalSlotsSetExtra(int trigger, int slotIndex, const ReversalSlot& slot)
    {
        if (trigger < 0 || trigger >= cpu::kRevTriggerCount || slotIndex < 2 || slotIndex >= kRevSlotsPerTrigger)
        {
            return;
        }
        s_extraSlots.slots[trigger][slotIndex] = slot;
    }

    static bool validForFighter(int trigger, int actionIndex, int fighterKind)
    {
        if (trigger == cpu::kRevOnHit || trigger == cpu::kRevOnShield)
        {
            const int list = trigger == cpu::kRevOnShield ? cpu::kListShield : cpu::kListGround;
            const int kind = cpu::actionKind(list, actionIndex);
            if (kind == cpu::kActionNone)
            {
                return false;
            }
            if (kind == cpu::kActionRandom || kind == cpu::kActionRandomAdvanced || kind == cpu::kActionPlayRandomSlot)
            {
                return true;
            }
            return cpu::actionFor(list, actionIndex, fighterKind) != NULL;
        }
        if (trigger == cpu::kRevOnGetup)
        {
            return actionIndex >= cpu::kGetupStand && actionIndex <= cpu::kGetupAttack;
        }
        if (trigger == cpu::kRevOnLedge)
        {
            return actionIndex == kRevActionDefault
                   || (actionIndex >= cpu::kLedgeNeutral && actionIndex <= cpu::kLedgeDrop);
        }
        return false;
    }

    bool reversalSlotsPick(int trigger, int fighterKind, u32 roll, int& outAction, int& outDelay)
    {
        const cpu::CpuOptions& opts = main::settings().cpu.options;
        if (opts.multiReversal == 0 || !hasSecondaryReversal(trigger, opts, &s_extraSlots))
        {
            return false;
        }
        ReversalSlot slots[kRevSlotsPerTrigger];
        buildReversalSlots(trigger, opts, &s_extraSlots, roll, slots);
        for (int i = 0; i < kRevSlotsPerTrigger; i++)
        {
            if ((slots[i].flags & kRevFlagEnabled) != 0 && !validForFighter(trigger, slots[i].action, fighterKind))
            {
                slots[i].flags = 0;
            }
        }
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
