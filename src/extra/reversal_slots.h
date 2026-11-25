#pragma once

#include <types.h>

#include "extra/reversal_slots_core.h"

// Live runtime service in the extra module for weighted multi-slot CPU reversals
// across On-Hit, On-Shield, On-Getup, and On-Ledge triggers.
namespace tm {
namespace extra {
    void reversalSlotsAttach();
    void reversalSlotsReset();
    void reversalSlotsSetExtra(int trigger, int slotIndex, const ReversalSlot& slot);
    bool reversalSlotsPick(int trigger, int fighterKind, u32 roll, int& outAction, int& outDelay);
}
}
