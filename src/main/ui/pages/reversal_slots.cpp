#include "main/extra.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"

// CPU > Multi-Slot Reversals (P_REVERSALS): configures weighted multi-slot
// reversal options and per-slot delays across On-Hit, On-Shield, On-Getup,
// and On-Ledge triggers. The active trigger selector lives in ui::menuScratch()
// so all 13 persisted bytes fit inside CpuOptions while leaving reserved[1].
namespace tm {
namespace ui {
namespace pages {
    enum {
        kRevTriggerScratchOffset = 33
    };
    TM_STATIC_ASSERT(kRevTriggerScratchOffset > kShowInactiveScratchOffset
                         && kRevTriggerScratchOffset < kScratchBytes,
                     tm_reversal_trigger_scratch_fits);

    static const char* const kRevTriggers[] = {"On-Hit", "On-Shield", "On-Getup", "On-Ledge"};
    static const char* const kRevGetup[] = {"None", "Stand", "Away", "Toward", "Attack"};

#pragma dont_inline on
    static bool revTrig(u8 t)
    {
        if (!cpuExtraAvailable() || menuScratch()[kRevTriggerScratchOffset] != t)
        {
            return false;
        }
        return t > cpu::kRevOnShield || counterLabels();
    }
#pragma dont_inline reset

    static bool revTrig0() { return revTrig(cpu::kRevOnHit); }
    static bool revTrig1() { return revTrig(cpu::kRevOnShield); }
    static bool revTrig2() { return revTrig(cpu::kRevOnGetup); }
    static bool revTrig3() { return revTrig(cpu::kRevOnLedge); }

#define TM_REV_ACT(idx, labels, count, gate)                                                                     \
    TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.revSlot2Action[idx]), 0, \
           (count) - 1, 0, "2nd Action", labels, NULL, gate)
#define TM_REV_DELAY(idx, gate)                                                                               \
    TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.revSlot2Delay[idx]), 0,  \
           30, 0, "2nd Delay", "%df", NULL, gate)
#define TM_REV_WEIGHT(idx, gate)                                                                               \
    TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.revSlot2Weight[idx]), 0,  \
           100, 50, "2nd Weight", "%d%%", NULL, gate)

    static const Option kRows[] = {
        TM_ROW(::tm::ui::kKindToggle, 0, TM_FIELD(cpu.options.multiReversal), 0, 1, 0, "Enabled", NULL, NULL,
               cpuExtraAvailable),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch, kRevTriggerScratchOffset, 0, TM_COUNT(kRevTriggers) - 1,
               0, "Trigger", kRevTriggers, NULL, cpuExtraAvailable),
        TM_REV_ACT(cpu::kRevOnHit, kCounterGround, kCounterGroundSize, revTrig0),
        TM_REV_DELAY(cpu::kRevOnHit, revTrig0),
        TM_REV_WEIGHT(cpu::kRevOnHit, revTrig0),
        TM_REV_ACT(cpu::kRevOnShield, kCounterShield, kCounterShieldSize, revTrig1),
        TM_REV_DELAY(cpu::kRevOnShield, revTrig1),
        TM_REV_WEIGHT(cpu::kRevOnShield, revTrig1),
        TM_REV_ACT(cpu::kRevOnGetup, kRevGetup, TM_COUNT(kRevGetup), revTrig2),
        TM_REV_DELAY(cpu::kRevOnGetup, revTrig2),
        TM_REV_WEIGHT(cpu::kRevOnGetup, revTrig2),
        TM_REV_ACT(cpu::kRevOnLedge, kLedgeOption, cpu::kLedgeDrop + 1, revTrig3),
        TM_REV_DELAY(cpu::kRevOnLedge, revTrig3),
        TM_REV_WEIGHT(cpu::kRevOnLedge, revTrig3)
    };

#undef TM_REV_ACT
#undef TM_REV_DELAY
#undef TM_REV_WEIGHT

    extern const Page kReversalSlots = TM_PAGE("Reversal Slots", kRows);
}
}
}
