#include "main/cpu/actions.h"
#include "main/cpu/options.h"
#include "main/cpu/view.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/lab.h"
#include "main/savestate/settings.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"

#include <string.h>

#define NA TM_UNAVAILABLE_LABEL
#define DROPPED TM_DROPPED_LABEL

namespace tm {
namespace ui {
namespace pages {
    static void cpuPercent()
    {
        lab::applyPercent(main::kSideCpu);
    }

    bool cpuLabControl()
    {
        return main::settings().cpu.options.control == cpu::kControlLab;
    }

    static const char* const kControl[] = {"Training", "Lab"};
    static const char* const kTdi[] = {"Random", "In", "Out", "Survival", "None"};
    static const char* const kSdiDirection[] = {"Auto", "Random", "Away", "Toward", "Left", "Right", "Up", "Down"};
    static const char* const kAsdi[] = {"Auto", "Away", "Toward", "Left", "Right", "Up", "Down"};
    static const char* const kBehavior[] = {"Stand", "Shield", "Crouch", "Jump", "Powershield"};

#define TM_PLAY_SLOT_LABELS \
    "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Random Slot"
#define TM_DJC_LABELS NA "DJC Nair", NA "DJC Fair", NA "DJC Bair", NA "DJC Uair", NA "DJC Dair"
#define TM_FLOAT_LABELS NA "Float Nair", NA "Float Fair", NA "Float Bair", NA "Float Uair", NA "Float Dair"
#define TM_SHFFL_LABELS "SHFFL Nair", "SHFFL Fair", "SHFFL Bair", "SHFFL Uair", "SHFFL Dair"
#define TM_FFL_LABELS "FFL Nair", "FFL Fair", "FFL Bair", "FFL Uair", "FFL Dair"

    // Indexed to match cpu/actions.cpp action tables.
    extern const char* kCounterGround[kCounterGroundSize] = {
        "None", "Spotdodge", "Shield", "Grab", "Up B", "Side B Toward", "Down B", "Neutral B", "Up Smash",
        "Down Smash", "Forward Smash", "Roll Away", "Roll Toward", "Jab", "Forward Tilt", "Up Tilt", "Down Tilt",
        "Dash Attack", "Short Hop", "Full Hop", "Wavedash Away", "Wavedash Toward", "Wavedash Down", "Dash Back",
        "Dash Through", TM_PLAY_SLOT_LABELS, "Side B Away", "Roll Random", "SH Nair", "SH Fair",
        "SH Bair", "SH Uair", "SH Dair", "Random Custom", "Random", TM_DJC_LABELS, TM_FLOAT_LABELS, TM_SHFFL_LABELS};
    extern const char* const kCounterAir[kCounterAirSize] = {
        "None", "Airdodge", "Jump Away", "Jump Toward", "Jump Neutral", "Up B Away", "Up B Toward",
        "Neutral B", "Side B Away", "Side B Toward", "Down B", "Nair", "Fair", "Bair",
        "Uair", "Dair", "Tumble Fastfall", "Wiggle Fastfall", DROPPED "Ledge", DROPPED "Wait",
        TM_PLAY_SLOT_LABELS, "Random Custom", "Random", TM_FFL_LABELS};
    extern const char* kCounterShield[kCounterShieldSize] = {
        "None", "Grab", "Short Hop", "Full Hop", "Spotdodge", "Roll Away", "Roll Toward", "Up B",
        DROPPED "Side B Away", DROPPED "Side B Toward", "Down B", DROPPED "Neutral B", "Up Smash",
        DROPPED "Down Smash", DROPPED "Forward Smash", "SH Nair", "SH Fair", "SH Bair", "SH Uair",
        "SH Dair", "Wavedash Away", DROPPED "Shield Drop",
        TM_PLAY_SLOT_LABELS, "Wavedash Toward", "Wavedash Down", "Roll Random", "Random Custom", "Random",
        TM_DJC_LABELS, TM_FLOAT_LABELS, TM_SHFFL_LABELS};
#undef TM_PLAY_SLOT_LABELS
#undef TM_DJC_LABELS
#undef TM_FLOAT_LABELS
#undef TM_SHFFL_LABELS
#undef TM_FFL_LABELS

#pragma dont_inline on
    bool counterLabels()
    {
        static const char** const kLists[2] = {kCounterGround, kCounterShield};
        game::FighterRef ref;
        int kind = game::findFighter(true, ref) ? cpu::fighterKind(ref.entryId) : -1;
        for (int k = 0; k < 2; k++)
        {
            int list = k == 0 ? cpu::kListGround : cpu::kListShield;
            for (int i = 0; i < cpu::actionCount(list); i++)
            {
                const cpu::Action* a = cpu::action(list, i);
                if (a == NULL || (a->flags & (cpu::kForDjc | cpu::kForFloat)) == 0)
                {
                    continue;
                }
                const char*& label = kLists[k][i];
                bool grey = label[0] == TM_UNAVAILABLE_LABEL[0];
                if (grey == cpu::fitsKind(a, kind))
                {
                    label += grey ? 1 : -1;
                }
            }
        }
        return cpuLabControl();
    }
#pragma dont_inline reset

    static const char* const kInfiniteShields[] = {"Off", "Until Hit", "On"};
    static const char* const kShieldAngle[] = {"Neutral", "Up", "Toward", "Down", "Away"};
    static const char* const kGrabEscape[] = {"None", "Medium", "High", "Max"};
    static const char* const kGrabRelease[] = {"Grounded", "Airborne"};
    static const char* const kControlledBy[] = {"None", "Port 1", "Port 2", "Port 3", "Port 4"};
    static const char* const kRecovery[] = {"Game AI", "PPlusAI", "Off"};
    static const char* const kLedgeOption[] = {"Default", "Neutral", "Roll", "Attack", "Jump", "Drop", "Random"};
    static const char* const kAdaptiveMode[] = {"Off", "Track", "Adaptive"};
    static const char* const kShfflTiming[] = {"Frame-Perfect", "Early", "Normal", "Late", "Random"};
    static const char* const kShieldTiming[] = {"First Gap", "Full Move"};

#pragma dont_inline on
    static bool cpuExtraAvailable()
    {
        return main::extraLoaded() && counterLabels();
    }
#pragma dont_inline reset

    static bool cpuSdiActive()
    {
        return cpuLabControl() && main::settings().cpu.options.sdiAmount > 0;
    }

    static bool cpuShieldHealthActive()
    {
        return cpuLabControl() && main::settings().cpu.options.infiniteShields != cpu::kShieldsOff;
    }

    static bool isShfflAction(int list, int idx)
    {
        const cpu::Action* a = cpu::action(list, idx);
        return a != NULL && (a->flags & (cpu::kForShffl | cpu::kForFfl)) != 0;
    }

    static bool cpuShfflActive()
    {
        if (!cpuLabControl())
        {
            return false;
        }
        const cpu::CpuOptions& c = main::settings().cpu.options;
        if (isShfflAction(cpu::kListGround, c.counterGround)
            || isShfflAction(cpu::kListAir, c.counterAir)
            || isShfflAction(cpu::kListShield, c.counterShield))
        {
            return true;
        }
        if (c.multiReversal != 0
            && (isShfflAction(cpu::kListGround, c.revSlot2Action[cpu::kRevOnHit])
                || isShfflAction(cpu::kListShield, c.revSlot2Action[cpu::kRevOnShield])))
        {
            return true;
        }
        const cpu::CpuTables& t = main::settings().cpuTables;
        for (int h = 0; h < cpu::kAdvancedHits; h++)
        {
            if (t.hits[h].logic == cpu::kLogicCustom
                && (isShfflAction(cpu::kListGround, t.hits[h].action[cpu::kListGround])
                    || isShfflAction(cpu::kListAir, t.hits[h].action[cpu::kListAir])
                    || isShfflAction(cpu::kListShield, t.hits[h].action[cpu::kListShield])))
            {
                return true;
            }
        }
        return false;
    }

    static void controlledByChanged()
    {
        cpu::controlledByChanged();
    }

    static void moveCpu()
    {
        cpu::moveCpuToggle();
    }

    static void freezeCpu()
    {
        cpu::freezeCpuToggle();
    }

#define TM_COUNTER_CHOICE(name, f, labels, def)                                                             \
    TM_ROW(::tm::ui::kKindChoice, TM_HALF(f), TM_FIELD(f), 0, TM_COUNT(labels) - 1, def, name, labels, NULL, \
           counterLabels)
#define TM_CPU_FUNC(name, onSelect, label) \
    TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, name, label, onSelect, cpuLabControl)

    // Top-level CPU page: 14 core controls + 4 focused subpages.
    static const Option kRows[] = {
        TM_INT("CPU Damage", cpu.percent, 0, 999, 0, "%d%%", cpuPercent),
        TM_TOGGLE("Lock CPU Damage", cpu.lockPercent, 0, cpuPercent),
        TM_INT("CPU Load Random %", recording.randomPercent[1], 0, savestate::kMaxRandomPercent, 0, "%d%%", NULL),
        TM_CHOICE("CPU Control", cpu.options.control, kControl, cpu::kControlLab, NULL),
        TM_CPU_CHOICE("Idle Behavior", cpu.options.behavior, kBehavior, 0),
        TM_MENU_IF("DI & SDI", P_CPU_DI, cpuLabControl),
        TM_MENU_IF("Counters & Reversals", P_CPU_COUNTERS, cpuLabControl),
        TM_MENU_IF("Tech & Getup", P_TECH, cpuLabControl),
        TM_MENU_IF("Shield & Grab", P_CPU_DEFENSE, cpuLabControl),
        TM_CPU_CHOICE("Recovery", cpu.options.recovery, kRecovery, cpu::kRecoveryPPlusAi),
        TM_CPU_CHOICE("Ledge Option", cpu.options.ledgeOption, kLedgeOption, cpu::kLedgeDefault),
        TM_CPU_FUNC("Move CPU", moveCpu, cpu::g_moveCpuLabel),
        TM_CPU_FUNC("Freeze CPU", freezeCpu, cpu::g_freezeCpuLabel),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(cpu.options.controlledBy), 0, TM_COUNT(kControlledBy) - 1, 0,
               "Port Control", kControlledBy, controlledByChanged, cpuLabControl)
    };

    // CPU > DI & SDI (P_CPU_DI)
    static const Option kDiRows[] = {
        TM_CPU_CHOICE("Trajectory DI", cpu.options.tdi, kTdi, 0),
        TM_CPU_INT("SDI Inputs", cpu.options.sdiAmount, 0, 7, 0, "%d"),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.sdiDir), 0,
               TM_COUNT(kSdiDirection) - 1, 0, "SDI Direction", kSdiDirection, NULL, cpuSdiActive),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.dualStickSdi), 0, 1, 0,
               "Dual-Stick SDI", NULL, NULL, cpuSdiActive),
        TM_CPU_CHOICE("ASDI Direction", cpu.options.asdi, kAsdi, 0)
    };

    // CPU > Counters & Reversals (P_CPU_COUNTERS)
    static const Option kCounterRows[] = {
        TM_COUNTER_CHOICE("Ground Counter", cpu.options.counterGround, kCounterGround, 13),
        TM_COUNTER_CHOICE("Air Counter", cpu.options.counterAir, kCounterAir, 1),
        TM_COUNTER_CHOICE("Shield Counter", cpu.options.counterShield, kCounterShield, 1),
        TM_CPU_CHOICE("Shield Timing", cpu.options.shieldTiming, kShieldTiming, cpu::kShieldTimingFullMove),
        TM_CPU_INT("Counter Delay", cpu.options.counterDelay, 0, 99, 0, "%df"),
        TM_MENU_IF("Per-Hit Counters", P_ADVANCED_COUNTER, cpuLabControl),
        TM_MENU_IF("Reversal Slots", P_REVERSALS, cpuExtraAvailable),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(cpu.options.adaptiveMode), 0, TM_COUNT(kAdaptiveMode) - 1,
               cpu::kAdaptiveOff, "Adaptive CPU", kAdaptiveMode, NULL, cpuExtraAvailable),
        TM_CPU_TOGGLE("Auto L-Cancel", cpu.options.autoLCancel, 1),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.shfflTiming), 0,
               TM_COUNT(kShfflTiming) - 1, cpu::kShfflFramePerfect, "SHFFL Timing", kShfflTiming, NULL, cpuShfflActive)
    };

    // CPU > Shield & Grab (P_CPU_DEFENSE)
    static const Option kDefenseRows[] = {
        TM_CPU_CHOICE("Infinite Shield", cpu.options.infiniteShields, kInfiniteShields, 1),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.shieldHealth), 0, 60, 60,
               "Shield Health", "%d", NULL, cpuShieldHealthActive),
        TM_CPU_CHOICE("Shield Tilt", cpu.options.shieldAngle, kShieldAngle, 0),
        TM_CPU_TOGGLE("No Knockback", cpu.options.intangible, 0),
        TM_CPU_CHOICE("Grab Mash", cpu.options.grabEscape, kGrabEscape, 0),
        TM_CPU_CHOICE("Grab Release", cpu.options.grabRelease, kGrabRelease, 0),
        TM_CPU_CHOICE("Status Mash", cpu.options.clatterEscape, kGrabEscape, cpu::kMashNone)
    };

#undef TM_CPU_FUNC
#undef TM_COUNTER_CHOICE

    extern const Page kCpuOptions = TM_PAGE("CPU", kRows);
    extern const Page kCpuDi = TM_PAGE("DI & SDI", kDiRows);
    extern const Page kCpuCounters = TM_PAGE("Counters & Reversals", kCounterRows);
    extern const Page kCpuDefense = TM_PAGE("Shield & Grab", kDefenseRows);

    // CPU > Counters & Reversals > Reversal Slots (P_REVERSALS)
    enum {
        kRevTriggerScratchOffset = 33
    };
    TM_STATIC_ASSERT(kRevTriggerScratchOffset > kShowInactiveScratchOffset
                         && kRevTriggerScratchOffset < kScratchBytes,
                     tm_reversal_trigger_scratch_fits);

    static const char* const kRevTriggers[] = {"On-Hit", "On-Shield", "On-Getup", "On-Ledge"};
    static const char* const kRevGetup[] = {"None", "Stand", "Away", "Toward", "Attack"};
    static const char* const kRevSlot1Getup[] = {"Random", "Stand", "Away", "Toward", "Attack"};
    static const char* const kRevLedge[] = {"None", "Neutral", "Roll", "Attack", "Jump", "Drop"};
    static char s_revSlot1Label[28] = "Jab";

    static const char* stripMarker(const char* s)
    {
        if (s != NULL && (s[0] == TM_UNAVAILABLE_LABEL[0] || s[0] == TM_DROPPED_LABEL[0]))
        {
            return s + 1;
        }
        return s != NULL ? s : "None";
    }

    static bool revEnabled()
    {
        if (!cpuExtraAvailable() || main::settings().cpu.options.multiReversal == 0)
        {
            return false;
        }
        const cpu::CpuOptions& c = main::settings().cpu.options;
        u8 trig = menuScratch()[kRevTriggerScratchOffset];
        const char* src = "None";
        if (trig == cpu::kRevOnHit && c.counterGround < kCounterGroundSize)
        {
            src = stripMarker(kCounterGround[c.counterGround]);
        }
        else if (trig == cpu::kRevOnShield && c.counterShield < kCounterShieldSize)
        {
            src = stripMarker(kCounterShield[c.counterShield]);
        }
        else if (trig == cpu::kRevOnGetup && c.getup < TM_COUNT(kRevSlot1Getup))
        {
            src = kRevSlot1Getup[c.getup];
        }
        else if (trig == cpu::kRevOnLedge && c.ledgeOption < TM_COUNT(kLedgeOption))
        {
            src = kLedgeOption[c.ledgeOption];
        }
        strncpy(s_revSlot1Label, src, sizeof(s_revSlot1Label) - 1);
        s_revSlot1Label[sizeof(s_revSlot1Label) - 1] = '\0';
        return true;
    }

#pragma dont_inline on
    static bool revTrig(int t)
    {
        return revEnabled() && menuScratch()[kRevTriggerScratchOffset] == t;
    }
#pragma dont_inline reset

    static bool revTrig0() { return revTrig(cpu::kRevOnHit); }
    static bool revTrig1() { return revTrig(cpu::kRevOnShield); }
    static bool revTrig2() { return revTrig(cpu::kRevOnGetup); }
    static bool revTrig3() { return revTrig(cpu::kRevOnLedge); }

#define TM_REV_ACT(idx, labels, count, gate)                                                                     \
    TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.revSlot2Action[idx]), 0, \
           (count) - 1, 0, "Slot 2 Action", labels, NULL, gate)
#define TM_REV_DELAY(idx, gate)                                                                              \
    TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.revSlot2Delay[idx]), 0, \
           30, 0, "Slot 2 Delay", "%df", NULL, gate)
#define TM_REV_WEIGHT(idx, gate)                                                                              \
    TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.revSlot2Weight[idx]), 0, \
           100, 50, "Slot 2 Chance", "%d%%", NULL, gate)

    static const Option kRevRows[] = {
        TM_ROW(::tm::ui::kKindToggle, 0, TM_FIELD(cpu.options.multiReversal), 0, 1, 0, "Enabled", NULL, NULL,
               cpuExtraAvailable),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               kRevTriggerScratchOffset, 0, TM_COUNT(kRevTriggers) - 1, 0, "Trigger", kRevTriggers, NULL, revEnabled),
        TM_ROW(::tm::ui::kKindInfo, ::tm::ui::kFlagHideWhenDisabled, 0, 0, 0, 0, "Slot 1 (Primary)", s_revSlot1Label,
               NULL, revEnabled),
        TM_REV_ACT(cpu::kRevOnHit, kCounterGround, kCounterGroundSize, revTrig0),
        TM_REV_DELAY(cpu::kRevOnHit, revTrig0),
        TM_REV_WEIGHT(cpu::kRevOnHit, revTrig0),
        TM_REV_ACT(cpu::kRevOnShield, kCounterShield, kCounterShieldSize, revTrig1),
        TM_REV_DELAY(cpu::kRevOnShield, revTrig1),
        TM_REV_WEIGHT(cpu::kRevOnShield, revTrig1),
        TM_REV_ACT(cpu::kRevOnGetup, kRevGetup, TM_COUNT(kRevGetup), revTrig2),
        TM_REV_DELAY(cpu::kRevOnGetup, revTrig2),
        TM_REV_WEIGHT(cpu::kRevOnGetup, revTrig2),
        TM_REV_ACT(cpu::kRevOnLedge, kRevLedge, TM_COUNT(kRevLedge), revTrig3),
        TM_REV_DELAY(cpu::kRevOnLedge, revTrig3),
        TM_REV_WEIGHT(cpu::kRevOnLedge, revTrig3)
    };

#undef TM_REV_ACT
#undef TM_REV_DELAY
#undef TM_REV_WEIGHT

    extern const Page kReversalSlots = TM_PAGE("Reversal Slots", kRevRows);
}
}
}

#undef NA
#undef DROPPED
