#include "main/cpu/actions.h"
#include "main/cpu/options.h"
#include "main/cpu/view.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/lab.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"

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

    // Indexed to match cpu/actions.cpp action tables.
    extern const char* kCounterGround[kCounterGroundSize] = {
        "None", "Spotdodge", "Shield", "Grab", "Up B", "Side B Toward", "Down B", "Neutral B", "Up Smash",
        "Down Smash", "Forward Smash", "Roll Away", "Roll Toward", "Jab", "Forward Tilt", "Up Tilt", "Down Tilt",
        "Dash Attack", "Short Hop", "Full Hop", "Wavedash Away", "Wavedash Toward", "Wavedash Down", "Dash Back",
        "Dash Through", TM_PLAY_SLOT_LABELS, "Side B Away", "Roll Random", "SH Nair", "SH Fair",
        "SH Bair", "SH Uair", "SH Dair", "Random Custom", "Random", TM_DJC_LABELS, TM_FLOAT_LABELS};
    extern const char* const kCounterAir[kCounterAirSize] = {
        "None", "Airdodge", "Jump Away", "Jump Toward", "Jump Neutral", "Up B Away", "Up B Toward",
        "Neutral B", "Side B Away", "Side B Toward", "Down B", "Nair", "Fair", "Bair",
        "Uair", "Dair", "Tumble Fastfall", "Wiggle Fastfall", DROPPED "Ledge", DROPPED "Wait",
        TM_PLAY_SLOT_LABELS, "Random Custom", "Random"};
    extern const char* kCounterShield[kCounterShieldSize] = {
        "None", "Grab", "Short Hop", "Full Hop", "Spotdodge", "Roll Away", "Roll Toward", "Up B",
        DROPPED "Side B Away", DROPPED "Side B Toward", "Down B", DROPPED "Neutral B", "Up Smash",
        DROPPED "Down Smash", DROPPED "Forward Smash", "SH Nair", "SH Fair", "SH Bair", "SH Uair",
        "SH Dair", "Wavedash Away", DROPPED "Shield Drop",
        TM_PLAY_SLOT_LABELS, "Wavedash Toward", "Wavedash Down", "Roll Random", "Random Custom", "Random",
        TM_DJC_LABELS, TM_FLOAT_LABELS};
#undef TM_PLAY_SLOT_LABELS
#undef TM_DJC_LABELS
#undef TM_FLOAT_LABELS

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

    static const char* const kInfiniteShields[] = {"Off", "Until Hit", "On"};
    static const char* const kShieldAngle[] = {"Neutral", "Up", "Toward", "Down", "Away"};
    static const char* const kGrabEscape[] = {"None", "Medium", "High", "Max"};
    static const char* const kGrabRelease[] = {"Grounded", "Airborne"};
    static const char* const kControlledBy[] = {"None", "Port 1", "Port 2", "Port 3", "Port 4"};
    static const char* const kRecovery[] = {"Game AI", "PPlusAI", "Off"};
    static const char* const kLedgeOption[] = {"Default", "Neutral", "Roll", "Attack", "Jump", "Drop", "Random"};
    static const char* const kAdaptiveMode[] = {"Off", "Track", "Adaptive"};

    static bool comboTrainerAvailable()
    {
        return cpuLabControl() && main::extraLoaded();
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

    static const Option kRows[] = {
        TM_INT("CPU Damage", cpu.percent, 0, 999, 0, "%d%%", cpuPercent),
        TM_TOGGLE("Lock CPU Damage", cpu.lockPercent, 0, cpuPercent),
        TM_CHOICE("CPU Control", cpu.options.control, kControl, cpu::kControlLab, NULL),
        TM_MENU("Tech & Getup", P_TECH),
        TM_CPU_CHOICE("Trajectory DI", cpu.options.tdi, kTdi, 0),
        TM_CPU_INT("SDI Inputs", cpu.options.sdiAmount, 0, 7, 0, "%df"),
        TM_CPU_CHOICE("SDI Direction", cpu.options.sdiDir, kSdiDirection, 0),
        TM_CPU_CHOICE("ASDI Direction", cpu.options.asdi, kAsdi, 0),
        TM_CPU_TOGGLE("Dual-Stick SDI", cpu.options.dualStickSdi, 0),
        TM_CPU_CHOICE("Idle Behavior", cpu.options.behavior, kBehavior, 0),
        TM_COUNTER_CHOICE("Ground Counter", cpu.options.counterGround, kCounterGround, 13),
        TM_COUNTER_CHOICE("Air Counter", cpu.options.counterAir, kCounterAir, 1),
        TM_COUNTER_CHOICE("Shield Counter", cpu.options.counterShield, kCounterShield, 1),
        TM_CPU_INT("Counter Delay", cpu.options.counterDelay, 0, 99, 0, "%df"),
        TM_MENU("Per-Hit Counters", P_ADVANCED_COUNTER),
        TM_CPU_CHOICE("Infinite Shield", cpu.options.infiniteShields, kInfiniteShields, 1),
        TM_CPU_INT("Shield Health", cpu.options.shieldHealth, 0, 60, 60, "%d"),
        TM_CPU_CHOICE("Shield Tilt", cpu.options.shieldAngle, kShieldAngle, 0),
        TM_CPU_TOGGLE("No Knockback", cpu.options.intangible, 0),
        TM_CPU_CHOICE("Grab Mash", cpu.options.grabEscape, kGrabEscape, 0),
        TM_CPU_CHOICE("Grab Release", cpu.options.grabRelease, kGrabRelease, 0),
        TM_CPU_FUNC("Move CPU", moveCpu, cpu::g_moveCpuLabel),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(cpu.options.controlledBy), 0, TM_COUNT(kControlledBy) - 1, 0,
               "Port Control", kControlledBy, controlledByChanged, cpuLabControl),
        TM_CPU_FUNC("Freeze CPU", freezeCpu, cpu::g_freezeCpuLabel),
        TM_CPU_CHOICE("Recovery", cpu.options.recovery, kRecovery, cpu::kRecoveryGame),
        TM_CPU_CHOICE("Ledge Option", cpu.options.ledgeOption, kLedgeOption, cpu::kLedgeDefault),
        TM_CPU_CHOICE("Clatter Escape", cpu.options.clatterEscape, kGrabEscape, cpu::kMashNone),
        TM_ROW(::tm::ui::kKindChoice, 0, TM_FIELD(cpu.options.adaptiveMode), 0, TM_COUNT(kAdaptiveMode) - 1,
               cpu::kAdaptiveOff, "Adaptive CPU", kAdaptiveMode, NULL, comboTrainerAvailable)
    };

#undef TM_CPU_FUNC
#undef TM_COUNTER_CHOICE

    extern const Page kCpuOptions = TM_PAGE("CPU", kRows);
}
}
}

#undef NA
#undef DROPPED
