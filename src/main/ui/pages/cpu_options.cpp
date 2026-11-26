#include "main/cpu/actions.h"
#include "main/cpu/options.h"
#include "main/cpu/view.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/lab.h"
#include "main/savestate/settings.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

#include <cstdio>
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

#ifdef __MWERKS__
    extern const char* kTdiLabels = NULL;
    extern const char* kCounterGround = NULL;
    extern const char* kCounterAir = NULL;
    extern const char* kCounterShield = NULL;
#else
    TM_LABELS_BEGIN(kControl) TM_L("Training") TM_L("Lab") TM_LABELS_END
    TM_LABELS_EXTERN_BEGIN(kTdiLabels, cpu::kTdiCount)
        TM_L("Random") TM_L("In") TM_L("Out") TM_L("Survival") TM_L("None") TM_L("Combo")
        TM_L("Slight Random") TM_L("Slight Away") TM_L("Down & Away") TM_L("Custom")
    TM_LABELS_END
    TM_LABELS_ASSERT(kTdiLabels, cpu::kTdiCount, tm_tdi_labels_match);
    TM_LABELS_BEGIN(kSdiDirection)
        TM_L("Auto") TM_L("Random") TM_L("Away") TM_L("Toward") TM_L("Left") TM_L("Right") TM_L("Up") TM_L("Down")
    TM_LABELS_END
    TM_LABELS_BEGIN(kAsdi)
        TM_L("Auto") TM_L("Away") TM_L("Toward") TM_L("Left") TM_L("Right") TM_L("Up") TM_L("Down")
    TM_LABELS_END
    TM_LABELS_BEGIN(kBehavior)
        TM_L("Stand") TM_L("Shield") TM_L("Crouch") TM_L("Jump") TM_L("Powershield")
    TM_LABELS_END
    TM_LABELS_ASSERT(kControl, 2, tm_control_match);
    TM_LABELS_ASSERT(kSdiDirection, 8, tm_sdi_dir_match);
    TM_LABELS_ASSERT(kAsdi, 7, tm_asdi_match);
    TM_LABELS_ASSERT(kBehavior, 5, tm_behavior_match);

#define TM_PLAY_SLOT_LABELS_P \
    TM_L("Slot 1") TM_L("Slot 2") TM_L("Slot 3") TM_L("Slot 4") TM_L("Slot 5") TM_L("Slot 6") TM_L("Random Slot")
#define DJC_M NA
#define FLT_M NA
#define TM_DJC_LABELS_P \
    TM_L(DJC_M "DJC Nair") TM_L(DJC_M "DJC Fair") TM_L(DJC_M "DJC Bair") TM_L(DJC_M "DJC Uair") TM_L(DJC_M "DJC Dair")
#define TM_FLOAT_LABELS_P \
    TM_L(FLT_M "Float Nair") TM_L(FLT_M "Float Fair") TM_L(FLT_M "Float Bair") TM_L(FLT_M "Float Uair") TM_L(FLT_M "Float Dair")
#define TM_SHFFL_LABELS_P TM_L("SHFFL Nair") TM_L("SHFFL Fair") TM_L("SHFFL Bair") TM_L("SHFFL Uair") TM_L("SHFFL Dair")
#define TM_FFL_LABELS_P TM_L("FFL Nair") TM_L("FFL Fair") TM_L("FFL Bair") TM_L("FFL Uair") TM_L("FFL Dair")

    // Indexed to match cpu/actions.cpp action tables.
    extern const char* kCounterGround[kCounterGroundSize] = {
        TM_L("None") TM_L("Spotdodge") TM_L("Shield") TM_L("Grab") TM_L("Up B") TM_L("Side B Toward")
        TM_L("Down B") TM_L("Neutral B") TM_L("Up Smash") TM_L("Down Smash") TM_L("Forward Smash")
        TM_L("Roll Away") TM_L("Roll Toward") TM_L("Jab") TM_L("Forward Tilt") TM_L("Up Tilt") TM_L("Down Tilt")
        TM_L("Dash Attack") TM_L("Short Hop") TM_L("Full Hop") TM_L("Wavedash Away") TM_L("Wavedash Toward")
        TM_L("Wavedash Down") TM_L("Dash Back") TM_L("Dash Through") TM_PLAY_SLOT_LABELS_P TM_L("Side B Away")
        TM_L("Roll Random") TM_L("SH Nair") TM_L("SH Fair") TM_L("SH Bair") TM_L("SH Uair") TM_L("SH Dair")
        TM_L("Random Custom") TM_L("Random") TM_DJC_LABELS_P TM_FLOAT_LABELS_P TM_SHFFL_LABELS_P
        TM_L("Custom Pool")
    };
    TM_LABELS_EXTERN_BEGIN(kCounterAir, kCounterAirSize)
        TM_L("None") TM_L("Airdodge") TM_L("Jump Away") TM_L("Jump Toward") TM_L("Jump Neutral")
        TM_L("Up B Away") TM_L("Up B Toward") TM_L("Neutral B") TM_L("Side B Away") TM_L("Side B Toward")
        TM_L("Down B") TM_L("Nair") TM_L("Fair") TM_L("Bair") TM_L("Uair") TM_L("Dair")
        TM_L("Tumble Fastfall") TM_L("Wiggle Fastfall") TM_L(DROPPED "Ledge") TM_L(DROPPED "Wait")
        TM_PLAY_SLOT_LABELS_P TM_L("Random Custom") TM_L("Random") TM_FFL_LABELS_P TM_L("Custom Pool")
    TM_LABELS_END
    TM_LABELS_ASSERT(kCounterAir, kCounterAirSize, tm_counter_air_match);
    extern const char* kCounterShield[kCounterShieldSize] = {
        TM_L("None") TM_L("Grab") TM_L("Short Hop") TM_L("Full Hop") TM_L("Spotdodge") TM_L("Roll Away")
        TM_L("Roll Toward") TM_L("Up B") TM_L(DROPPED "Side B Away") TM_L(DROPPED "Side B Toward")
        TM_L("Down B") TM_L(DROPPED "Neutral B") TM_L("Up Smash") TM_L(DROPPED "Down Smash")
        TM_L(DROPPED "Forward Smash") TM_L("SH Nair") TM_L("SH Fair") TM_L("SH Bair") TM_L("SH Uair")
        TM_L("SH Dair") TM_L("Wavedash Away") TM_L(DROPPED "Shield Drop")
        TM_PLAY_SLOT_LABELS_P TM_L("Wavedash Toward") TM_L("Wavedash Down") TM_L("Roll Random")
        TM_L("Random Custom") TM_L("Random") TM_DJC_LABELS_P TM_FLOAT_LABELS_P TM_SHFFL_LABELS_P
        TM_L("Custom Pool")
    };
#undef DJC_M
#undef FLT_M
#undef TM_PLAY_SLOT_LABELS_P
#undef TM_DJC_LABELS_P
#undef TM_FLOAT_LABELS_P
#undef TM_SHFFL_LABELS_P
#undef TM_FFL_LABELS_P
#endif

#pragma dont_inline on
    bool counterLabels()
    {
        game::FighterRef ref;
        int kind = game::findFighter(true, ref) ? cpu::fighterKind(ref.entryId) : -1;
#ifdef __MWERKS__
        static const cpu::Action kDjcProbe = {NULL, cpu::kForDjc, 0};
        static const cpu::Action kFloatProbe = {NULL, cpu::kForFloat, 0};
        g_counterAvailMask = static_cast<u8>((cpu::fitsKind(&kDjcProbe, kind) ? 1 : 0)
                                             | (cpu::fitsKind(&kFloatProbe, kind) ? 2 : 0));
#else
        static const char** const kLists[2] = {kCounterGround, kCounterShield};
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
#endif
        return cpuLabControl();
    }
#pragma dont_inline reset

#ifndef __MWERKS__
    TM_LABELS_BEGIN(kInfiniteShields) TM_L("Off") TM_L("Until Hit") TM_L("On") TM_LABELS_END
    TM_LABELS_BEGIN(kShieldAngle) TM_L("Neutral") TM_L("Up") TM_L("Toward") TM_L("Down") TM_L("Away") TM_LABELS_END
    TM_LABELS_BEGIN(kGrabEscape) TM_L("None") TM_L("Medium") TM_L("High") TM_L("Max") TM_LABELS_END
    TM_LABELS_BEGIN(kGrabRelease) TM_L("Grounded") TM_L("Airborne") TM_LABELS_END
    TM_LABELS_BEGIN(kControlledBy) TM_L("None") TM_L("Port 1") TM_L("Port 2") TM_L("Port 3") TM_L("Port 4") TM_LABELS_END
    TM_LABELS_BEGIN(kRecovery) TM_L("Game AI") TM_L("PPlusAI") TM_L("Off") TM_LABELS_END
    TM_LABELS_BEGIN(kLedgeOption) TM_L("Default") TM_L("Neutral") TM_L("Roll") TM_L("Attack") TM_L("Jump") TM_L("Drop") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kAdaptiveMode) TM_L("Off") TM_L("Track") TM_L("Adaptive") TM_LABELS_END
    TM_LABELS_BEGIN(kShfflTiming) TM_L("Frame-Perfect") TM_L("Early") TM_L("Normal") TM_L("Late") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kShieldTiming) TM_L("First Gap") TM_L("Full Move") TM_LABELS_END
    TM_LABELS_BEGIN(kCounterModes)
        TM_L("Off") TM_L("Single Action") TM_L("Custom Pool") TM_L("Reversal Slots") TM_L("Per-Hit") TM_L("Adaptive")
    TM_LABELS_END
    TM_LABELS_ASSERT(kInfiniteShields, 3, tm_inf_sh_match);
    TM_LABELS_ASSERT(kShieldAngle, 5, tm_sh_ang_match);
    TM_LABELS_ASSERT(kGrabEscape, 4, tm_grab_esc_match);
    TM_LABELS_ASSERT(kGrabRelease, 2, tm_grab_rel_match);
    TM_LABELS_ASSERT(kControlledBy, 5, tm_ctrl_by_match);
    TM_LABELS_ASSERT(kRecovery, 3, tm_rec_match);
    TM_LABELS_ASSERT(kLedgeOption, 7, tm_ledge_opt_match);
    TM_LABELS_ASSERT(kAdaptiveMode, 3, tm_adapt_match);
    TM_LABELS_ASSERT(kShfflTiming, 5, tm_shffl_match);
    TM_LABELS_ASSERT(kShieldTiming, 2, tm_sh_time_match);
    TM_LABELS_ASSERT(kCounterModes, 6, tm_ctr_mode_match);
#endif

    enum CounterMode {
        kCtrModeOff = 0,
        kCtrModeSingle = 1,
        kCtrModePool = 2,
        kCtrModeReversals = 3,
        kCtrModePerHit = 4,
        kCtrModeAdaptive = 5,
        kCtrModeCount = 6
    };
    TM_STATIC_ASSERT(kCounterModeScratchOffset < kScratchBytes, tm_counter_mode_scratch_fits);
    static u8 s_lastDerivedCtrMode = 0xFFu;

    static u8 deriveCounterMode()
    {
        const cpu::CpuOptions& c = main::settings().cpu.options;
        const cpu::CpuTables& t = main::settings().cpuTables;
        if (c.adaptiveMode != cpu::kAdaptiveOff)
        {
            return kCtrModeAdaptive;
        }
        if (c.multiReversal != 0)
        {
            return kCtrModeReversals;
        }
        for (int h = 0; h < cpu::kAdvancedHits; h++)
        {
            if (t.hits[h].logic == cpu::kLogicCustom)
            {
                return kCtrModePerHit;
            }
        }
        if (cpu::actionKind(cpu::kListGround, c.counterGround) == cpu::kActionCustomPool
            || cpu::actionKind(cpu::kListAir, c.counterAir) == cpu::kActionCustomPool
            || cpu::actionKind(cpu::kListShield, c.counterShield) == cpu::kActionCustomPool)
        {
            return kCtrModePool;
        }
        if (c.counterGround == 0 && c.counterAir == 0 && c.counterShield == 0)
        {
            return kCtrModeOff;
        }
        return kCtrModeSingle;
    }

    static u8 activeCounterMode()
    {
        u8 derived = deriveCounterMode();
        u8& slot = menuScratch()[kCounterModeScratchOffset];
        if (s_lastDerivedCtrMode != derived || slot >= kCtrModeCount)
        {
            slot = derived;
            s_lastDerivedCtrMode = derived;
        }
        return slot;
    }

    static void restoreDefaultSingleCounters(cpu::CpuOptions& c)
    {
        if (c.counterGround == 0 || cpu::actionKind(cpu::kListGround, c.counterGround) == cpu::kActionCustomPool)
        {
            c.counterGround = 13;
        }
        if (c.counterAir == 0 || cpu::actionKind(cpu::kListAir, c.counterAir) == cpu::kActionCustomPool)
        {
            c.counterAir = 1;
        }
        if (c.counterShield == 0 || cpu::actionKind(cpu::kListShield, c.counterShield) == cpu::kActionCustomPool)
        {
            c.counterShield = 1;
        }
    }

    static void counterModeChanged()
    {
        u8 mode = menuScratch()[kCounterModeScratchOffset];
        cpu::CpuOptions& c = main::settings().cpu.options;
        cpu::CpuTables& t = main::settings().cpuTables;
        if (mode == kCtrModeOff)
        {
            c.counterGround = 0;
            c.counterAir = 0;
            c.counterShield = 0;
            c.multiReversal = 0;
            c.adaptiveMode = cpu::kAdaptiveOff;
            for (int h = 0; h < cpu::kAdvancedHits; h++)
            {
                if (t.hits[h].logic == cpu::kLogicCustom)
                {
                    t.hits[h].logic = cpu::kLogicDefault;
                }
            }
        }
        else if (mode == kCtrModeSingle)
        {
            c.multiReversal = 0;
            c.adaptiveMode = cpu::kAdaptiveOff;
            restoreDefaultSingleCounters(c);
        }
        else if (mode == kCtrModePool)
        {
            c.multiReversal = 0;
            c.adaptiveMode = cpu::kAdaptiveOff;
            c.counterGround = kCounterGroundSize - 1;
            c.counterAir = kCounterAirSize - 1;
            c.counterShield = kCounterShieldSize - 1;
        }
        else if (mode == kCtrModeReversals)
        {
            c.adaptiveMode = cpu::kAdaptiveOff;
            c.multiReversal = 1;
            restoreDefaultSingleCounters(c);
        }
        else if (mode == kCtrModePerHit)
        {
            c.multiReversal = 0;
            c.adaptiveMode = cpu::kAdaptiveOff;
        }
        else if (mode == kCtrModeAdaptive)
        {
            c.multiReversal = 0;
            if (c.adaptiveMode == cpu::kAdaptiveOff)
            {
                c.adaptiveMode = cpu::kAdaptiveActive;
            }
            restoreDefaultSingleCounters(c);
        }
        s_lastDerivedCtrMode = deriveCounterMode();
    }

#pragma dont_inline on
    static bool cpuExtraAvailable()
    {
        return main::extraLoaded() && counterLabels();
    }
#pragma dont_inline reset

    static bool counterModeGate()
    {
        if (!cpuLabControl())
        {
            return false;
        }
        activeCounterMode();
        return true;
    }

    static bool counterSingleRows()
    {
        if (!counterLabels())
        {
            return false;
        }
        u8 m = activeCounterMode();
        return m == kCtrModeSingle || m == kCtrModeReversals || m == kCtrModeAdaptive;
    }

    static bool counterNonOff()
    {
        return cpuLabControl() && activeCounterMode() != kCtrModeOff;
    }

    static bool counterPoolRow()
    {
        return cpuLabControl() && activeCounterMode() == kCtrModePool;
    }

    static bool counterPerHitRow()
    {
        return cpuLabControl() && activeCounterMode() == kCtrModePerHit;
    }

    static bool counterReversalRow()
    {
        return cpuExtraAvailable() && activeCounterMode() == kCtrModeReversals;
    }

    static bool counterAdaptiveRow()
    {
        return cpuExtraAvailable() && activeCounterMode() == kCtrModeAdaptive;
    }

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

    static bool poolHasShffl(int list, const u8 bits[8])
    {
        int n = cpu::actionCount(list);
        for (int i = 0; i < n && i < 64; i++)
        {
            if ((bits[i >> 3] & (1u << (i & 7))) != 0 && isShfflAction(list, i))
            {
                return true;
            }
        }
        return false;
    }

    static bool cpuShfflActive()
    {
        if (!cpuLabControl())
        {
            return false;
        }
        const cpu::CpuOptions& c = main::settings().cpu.options;
        const cpu::CpuTables& t = main::settings().cpuTables;
        const u8 ctr[3] = {c.counterGround, c.counterAir, c.counterShield};
        for (int list = 0; list < 3; list++)
        {
            if (isShfflAction(list, ctr[list])
                || (cpu::actionKind(list, ctr[list]) == cpu::kActionCustomPool && poolHasShffl(list, t.poolBits[list])))
            {
                return true;
            }
        }
        if (c.multiReversal != 0
            && (isShfflAction(cpu::kListGround, c.revSlot2Action[cpu::kRevOnHit])
                || isShfflAction(cpu::kListShield, c.revSlot2Action[cpu::kRevOnShield])))
        {
            return true;
        }
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

    static bool cpuCustomDiActive()
    {
        return cpuLabControl() && main::settings().cpu.options.tdi == cpu::kTdiCustom;
    }

#ifdef __MWERKS__
    extern Page kCpuOptions = {"CPU", NULL, 0, {0, 0, 0}};
    extern Page kCpuDi = {"DI & SDI", NULL, 0, {0, 0, 0}};
    extern Page kCpuCounters = {"Counters & Reversals", NULL, 0, {0, 0, 0}};
    extern Page kCpuDefense = {"Shield & Grab", NULL, 0, {0, 0, 0}};
#else
#define TM_COUNTER_CHOICE(name, f, labels, count, def)                                              \
    TM_ROW(::tm::ui::kKindChoice, TM_HALF(f), TM_FIELD(f), TM_CHOICE_LO, (count) - 1, def, name, labels, NULL, \
           counterLabels)
#define TM_CPU_FUNC(name, onSelect, label) \
    TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, name, label, onSelect, cpuLabControl)

    // Top-level CPU page: 14 core controls + 4 focused subpages.
    static const Option kRows[] = {
        TM_INT("CPU Damage", cpu.percent, 0, 999, 0, "%d%%", cpuPercent),
        TM_TOGGLE("Lock CPU Damage", cpu.lockPercent, 0, cpuPercent),
        TM_INT("CPU Load Random %", recording.randomPercent[1], 0, savestate::kMaxRandomPercent, 0, "%d%%", NULL),
        TM_CHOICE_P("CPU Control", cpu.options.control, kControl, 2, cpu::kControlLab, NULL),
        TM_CPU_CHOICE_P("Idle Behavior", cpu.options.behavior, kBehavior, 5, 0),
        TM_MENU_IF("DI & SDI", P_CPU_DI, cpuLabControl),
        TM_MENU_IF("Counters & Reversals", P_CPU_COUNTERS, cpuLabControl),
        TM_MENU_IF("Tech & Getup", P_TECH, cpuLabControl),
        TM_MENU_IF("Shield & Grab", P_CPU_DEFENSE, cpuLabControl),
        TM_CPU_CHOICE_P("Recovery", cpu.options.recovery, kRecovery, 3, cpu::kRecoveryPPlusAi),
        TM_CPU_CHOICE_P("Ledge Option", cpu.options.ledgeOption, kLedgeOption, 7, cpu::kLedgeDefault),
        TM_CPU_FUNC("Move CPU", moveCpu, cpu::g_moveCpuLabel),
        TM_CPU_FUNC("Freeze CPU", freezeCpu, cpu::g_freezeCpuLabel),
        TM_ROW_CHOICE_P(0, TM_FIELD(cpu.options.controlledBy), 5, 0, "Port Control", kControlledBy,
                        controlledByChanged, cpuLabControl)
    };

    // CPU > DI & SDI (P_CPU_DI)
    static const Option kDiRows[] = {
        TM_CPU_CHOICE_P("Trajectory DI", cpu.options.tdi, kTdiLabels, cpu::kTdiCount, 0),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagHideWhenDisabled, 0, 0, 0, 0, "Pick DI Angle",
               lab::g_diPickLabel, lab::toggleDiStickPicker, cpuCustomDiActive),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagSigned | ::tm::ui::kFlagHideWhenDisabled,
               TM_FIELD(cpuTables.customDiX), -100, 100, 100, "Custom DI X", "%+d", NULL, cpuCustomDiActive),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagSigned | ::tm::ui::kFlagHideWhenDisabled,
               TM_FIELD(cpuTables.customDiY), -100, 100, 0, "Custom DI Y", "%+d", NULL, cpuCustomDiActive),
        TM_CPU_INT("SDI Inputs", cpu.options.sdiAmount, 0, 7, 0, "%d"),
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.sdiDir), 8, 0, "SDI Direction",
                        kSdiDirection, NULL, cpuSdiActive),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.dualStickSdi), 0, 1, 0,
               "Dual-Stick SDI", NULL, NULL, cpuSdiActive),
        TM_CPU_CHOICE_P("ASDI Direction", cpu.options.asdi, kAsdi, 7, 0)
    };

    // CPU > Counters & Reversals (P_CPU_COUNTERS)
    static const Option kCounterRows[] = {
        TM_ROW_CHOICE_P(::tm::ui::kFlagScratch, kCounterModeScratchOffset, kCtrModeCount, kCtrModeSingle,
                        "Counter Mode", kCounterModes, counterModeChanged, counterModeGate),
        TM_ROW(::tm::ui::kKindChoice, TM_HALF(cpu.options.counterGround) | ::tm::ui::kFlagHideWhenDisabled,
               TM_FIELD(cpu.options.counterGround), TM_CHOICE_LO, kCounterGroundSize - 1, 13,
               "Ground Counter", kCounterGround, NULL, counterSingleRows),
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.counterAir), kCounterAirSize, 1,
                        "Air Counter", kCounterAir, NULL, counterSingleRows),
        TM_ROW(::tm::ui::kKindChoice, TM_HALF(cpu.options.counterShield) | ::tm::ui::kFlagHideWhenDisabled,
               TM_FIELD(cpu.options.counterShield), TM_CHOICE_LO, kCounterShieldSize - 1, 1,
               "Shield Counter", kCounterShield, NULL, counterSingleRows),
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.shieldTiming), 2,
                        cpu::kShieldTimingFullMove, "Shield Timing", kShieldTiming, NULL, counterNonOff),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.counterDelay), 0, 99, 0,
               "Counter Delay", "%df", NULL, counterNonOff),
        TM_ROW(::tm::ui::kKindMenu, ::tm::ui::kFlagHideWhenDisabled, 0, 0, P_COUNTER_POOL, 0, "Custom Pools",
               NULL, NULL, counterPoolRow),
        TM_ROW(::tm::ui::kKindMenu, ::tm::ui::kFlagHideWhenDisabled, 0, 0, P_ADVANCED_COUNTER, 0, "Per-Hit Counters",
               NULL, NULL, counterPerHitRow),
        TM_ROW(::tm::ui::kKindMenu, ::tm::ui::kFlagHideWhenDisabled, 0, 0, P_REVERSALS, 0, "Reversal Slots",
               NULL, NULL, counterReversalRow),
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.adaptiveMode), 3, cpu::kAdaptiveOff,
                        "Adaptive CPU", kAdaptiveMode, NULL, counterAdaptiveRow),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.autoLCancel), 0, 1, 1,
               "Auto L-Cancel", NULL, NULL, counterNonOff),
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.shfflTiming), 5, cpu::kShfflFramePerfect,
                        "SHFFL Timing", kShfflTiming, NULL, cpuShfflActive)
    };

    // CPU > Shield & Grab (P_CPU_DEFENSE)
    static const Option kDefenseRows[] = {
        TM_CPU_CHOICE_P("Infinite Shield", cpu.options.infiniteShields, kInfiniteShields, 3, 1),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.shieldHealth), 0, 60, 60,
               "Shield Health", "%d", NULL, cpuShieldHealthActive),
        TM_CPU_CHOICE_P("Shield Tilt", cpu.options.shieldAngle, kShieldAngle, 5, 0),
        TM_CPU_TOGGLE("No Knockback", cpu.options.intangible, 0),
        TM_CPU_CHOICE_P("Grab Mash", cpu.options.grabEscape, kGrabEscape, 4, 0),
        TM_CPU_CHOICE_P("Grab Release", cpu.options.grabRelease, kGrabRelease, 2, 0),
        TM_CPU_CHOICE_P("Status Mash", cpu.options.clatterEscape, kGrabEscape, 4, cpu::kMashNone)
    };

#undef TM_CPU_FUNC
#undef TM_COUNTER_CHOICE

    extern const Page kCpuOptions = TM_PAGE("CPU", kRows);
    extern const Page kCpuDi = TM_PAGE("DI & SDI", kDiRows);
    extern const Page kCpuCounters = TM_PAGE("Counters & Reversals", kCounterRows);
    extern const Page kCpuDefense = TM_PAGE("Shield & Grab", kDefenseRows);
#endif

    // CPU > Counters & Reversals > Reversal Slots (P_REVERSALS)
    enum {
        kRevTriggerScratchOffset = 33
    };
    TM_STATIC_ASSERT(kRevTriggerScratchOffset > kShowInactiveScratchOffset
                         && kRevTriggerScratchOffset < kScratchBytes,
                     tm_reversal_trigger_scratch_fits);

#ifndef __MWERKS__
    TM_LABELS_BEGIN(kRevTriggers) TM_L("On-Hit") TM_L("On-Shield") TM_L("On-Getup") TM_L("On-Ledge") TM_LABELS_END
    TM_LABELS_BEGIN(kRevGetup) TM_L("None") TM_L("Stand") TM_L("Away") TM_L("Toward") TM_L("Attack") TM_LABELS_END
    TM_LABELS_BEGIN(kRevSlot1Getup) TM_L("Random") TM_L("Stand") TM_L("Away") TM_L("Toward") TM_L("Attack") TM_LABELS_END
    TM_LABELS_BEGIN(kRevLedge) TM_L("None") TM_L("Neutral") TM_L("Roll") TM_L("Attack") TM_L("Jump") TM_L("Drop") TM_LABELS_END
    TM_LABELS_ASSERT(kRevTriggers, 4, tm_rev_trig_match);
    TM_LABELS_ASSERT(kRevGetup, 5, tm_rev_getup_match);
    TM_LABELS_ASSERT(kRevSlot1Getup, 5, tm_rev_s1_getup_match);
    TM_LABELS_ASSERT(kRevLedge, 6, tm_rev_ledge_match);
#endif
    static char s_revSlot1Label[28] = "Jab";

    static const char* stripMarker(const char* s)
    {
        if (s != NULL && (s[0] == TM_UNAVAILABLE_LABEL[0] || s[0] == TM_DROPPED_LABEL[0]
#ifdef __MWERKS__
                          || s[0] == TM_DJC_LABEL[0] || s[0] == TM_FLOAT_LABEL[0]
#endif
                          ))
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
            src = stripMarker(packedLabelAt(kCounterGround, c.counterGround));
        }
        else if (trig == cpu::kRevOnShield && c.counterShield < kCounterShieldSize)
        {
            src = stripMarker(packedLabelAt(kCounterShield, c.counterShield));
        }
        else if (trig == cpu::kRevOnGetup && c.getup < 5)
        {
#ifdef __MWERKS__
            src = pageStringsReady() ? packedLabelAt(pageStr(kPs_cpu_kRevSlot1Getup), c.getup) : "None";
#else
            src = packedLabelAt(kRevSlot1Getup, c.getup);
#endif
        }
        else if (trig == cpu::kRevOnLedge && c.ledgeOption < 7)
        {
#ifdef __MWERKS__
            src = pageStringsReady() ? packedLabelAt(pageStr(kPs_cpu_kLedgeOption), c.ledgeOption) : "None";
#else
            src = packedLabelAt(kLedgeOption, c.ledgeOption);
#endif
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

#ifdef __MWERKS__
    extern Page kReversalSlots = {"Reversal Slots", NULL, 0, {0, 0, 0}};

    static void initReversalPage(Option* buf)
    {
        static const Option kRevHead[3] = {
            TM_ROW(::tm::ui::kKindToggle, 0, TM_FIELD(cpu.options.multiReversal), 0, 1, 0, "Enabled", NULL, NULL,
                   cpuExtraAvailable),
            TM_ROW_CHOICE_P(::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
                            kRevTriggerScratchOffset, 4, 0, "Trigger", NULL, NULL, revEnabled),
            TM_ROW(::tm::ui::kKindInfo, ::tm::ui::kFlagHideWhenDisabled, 0, 0, 0, 0, "Slot 1 (Primary)", s_revSlot1Label,
                   NULL, revEnabled)
        };
        const char* kActLbls[4] = {
            kCounterGround, kCounterShield, pageStr(kPs_cpu_kRevGetup), pageStr(kPs_cpu_kRevLedge)
        };
        static const u8 kActMax[4] = {kCounterGroundSize - 1, kCounterShieldSize - 1, 4, 5};
        static bool (*const kGates[4])() = {revTrig0, revTrig1, revTrig2, revTrig3};
        memcpy(buf, kRevHead, sizeof(kRevHead));
        buf[1].data = pageStr(kPs_cpu_kRevTriggers);
        for (int i = 0; i < 4; i++)
        {
            Option* g = buf + 3 + i * 3;
            g[0].kind = kKindChoice;
            g[0].flags = kFlagHideWhenDisabled;
            g[0].offset = static_cast<u16>(TM_FIELD(cpu.options.revSlot2Action) + i);
            g[0].min = TM_CHOICE_LO;
            g[0].max = kActMax[i];
            g[0].name = pageStr(kPs_rev_act);
            g[0].data = kActLbls[i];
            g[0].enabled = kGates[i];

            g[1].kind = kKindInt;
            g[1].flags = kFlagHideWhenDisabled;
            g[1].offset = static_cast<u16>(TM_FIELD(cpu.options.revSlot2Delay) + i);
            g[1].max = 30;
            g[1].name = pageStr(kPs_rev_dly);
            g[1].data = pageStr(kPs_fmt_df);
            g[1].enabled = kGates[i];

            g[2].kind = kKindInt;
            g[2].flags = kFlagHideWhenDisabled;
            g[2].offset = static_cast<u16>(TM_FIELD(cpu.options.revSlot2Weight) + i);
            g[2].def = 50;
            g[2].max = 100;
            g[2].name = pageStr(kPs_rev_wgt);
            g[2].data = pageStr(kPs_fmt_pct);
            g[2].enabled = kGates[i];
        }
        kReversalSlots.options = buf;
        kReversalSlots.count = 15;
    }
#else
#define TM_REV_ACT_P(idx, labels, count, gate)                                                                   \
    TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.revSlot2Action[idx]), count, 0,        \
                    "Slot 2 Action", labels, NULL, gate)
#define TM_REV_DELAY(idx, gate)                                                                              \
    TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.revSlot2Delay[idx]), 0, \
           30, 0, "Slot 2 Delay", "%df", NULL, gate)
#define TM_REV_WEIGHT(idx, gate)                                                                              \
    TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.revSlot2Weight[idx]), 0, \
           100, 50, "Slot 2 Chance", "%d%%", NULL, gate)

    static const Option kRevRows[] = {
        TM_ROW(::tm::ui::kKindToggle, 0, TM_FIELD(cpu.options.multiReversal), 0, 1, 0, "Enabled", NULL, NULL,
               cpuExtraAvailable),
        TM_ROW_CHOICE_P(::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
                        kRevTriggerScratchOffset, 4, 0, "Trigger", kRevTriggers, NULL, revEnabled),
        TM_ROW(::tm::ui::kKindInfo, ::tm::ui::kFlagHideWhenDisabled, 0, 0, 0, 0, "Slot 1 (Primary)", s_revSlot1Label,
               NULL, revEnabled),
        TM_REV_ACT_P(cpu::kRevOnHit, kCounterGround, kCounterGroundSize, revTrig0),
        TM_REV_DELAY(cpu::kRevOnHit, revTrig0),
        TM_REV_WEIGHT(cpu::kRevOnHit, revTrig0),
        TM_REV_ACT_P(cpu::kRevOnShield, kCounterShield, kCounterShieldSize, revTrig1),
        TM_REV_DELAY(cpu::kRevOnShield, revTrig1),
        TM_REV_WEIGHT(cpu::kRevOnShield, revTrig1),
        TM_REV_ACT_P(cpu::kRevOnGetup, kRevGetup, 5, revTrig2),
        TM_REV_DELAY(cpu::kRevOnGetup, revTrig2),
        TM_REV_WEIGHT(cpu::kRevOnGetup, revTrig2),
        TM_REV_ACT_P(cpu::kRevOnLedge, kRevLedge, 6, revTrig3),
        TM_REV_DELAY(cpu::kRevOnLedge, revTrig3),
        TM_REV_WEIGHT(cpu::kRevOnLedge, revTrig3)
    };

#undef TM_REV_ACT_P
#undef TM_REV_DELAY
#undef TM_REV_WEIGHT

    extern const Page kReversalSlots = TM_PAGE("Reversal Slots", kRevRows);
#endif

    // CPU > Counters & Reversals > Custom Pools (P_COUNTER_POOL)
    enum {
        kPoolCatScratchOffset = 34
    };
    TM_STATIC_ASSERT(kPoolCatScratchOffset > kRevTriggerScratchOffset && kPoolCatScratchOffset < kScratchBytes,
                     tm_pool_cat_scratch_fits);

    static char s_poolCatBuf[3][20] = {"Ground (12 On)", "Air (8 On)", "Shield (10 On)"};
    static const char* const s_poolCatPtrs[3] = {s_poolCatBuf[0], s_poolCatBuf[1], s_poolCatBuf[2]};
    static char s_poolStatusLabel[20] = "Set Custom Pool";

    static int activePoolCat()
    {
        u8 c = menuScratch()[kPoolCatScratchOffset];
        return c < cpu::kListCount ? static_cast<int>(c) : 0;
    }

    static int countPoolBits(int cat)
    {
        const u8* bits = main::settings().cpuTables.poolBits[cat];
        int total = 0;
        for (int b = 0; b < 8; b++)
        {
            u8 v = bits[b];
            while (v != 0)
            {
                total += (v & 1u) != 0 ? 1 : 0;
                v >>= 1;
            }
        }
        return total;
    }

    static bool poolPageActive()
    {
        if (!cpuLabControl())
        {
            return false;
        }
        static const char* const kPoolNames[3] = {"Ground", "Air", "Shield"};
        for (int c = 0; c < 3; c++)
        {
            snprintf(s_poolCatBuf[c], sizeof(s_poolCatBuf[c]), "%s (%d On)", kPoolNames[c], countPoolBits(c));
        }
        int cat = activePoolCat();
        const cpu::CpuOptions& co = main::settings().cpu.options;
        u8 cur = cat == cpu::kListGround ? co.counterGround : (cat == cpu::kListAir ? co.counterAir : co.counterShield);
        bool live = cpu::actionKind(cat, cur) == cpu::kActionCustomPool;
        snprintf(s_poolStatusLabel, sizeof(s_poolStatusLabel), "%s", live ? "Active" : "Set Custom Pool");
        return true;
    }

    static void resetActivePool()
    {
        int cat = activePoolCat();
        cpu::poolDefaults(cat, main::settings().cpuTables.poolBits[cat]);
        main::settings().cpuTables.poolReady = 1;
    }

    static void clearActivePool()
    {
        int cat = activePoolCat();
        memset(main::settings().cpuTables.poolBits[cat], 0, 8);
        main::settings().cpuTables.poolReady = 1;
    }

#pragma dont_inline on
    static bool poolCatIs(int cat)
    {
        return cpuLabControl() && activePoolCat() == cat;
    }
#pragma dont_inline reset

    static bool poolCat0() { return poolCatIs(cpu::kListGround); }
    static bool poolCat1() { return poolCatIs(cpu::kListAir); }
    static bool poolCat2() { return poolCatIs(cpu::kListShield); }

#ifdef __MWERKS__
    static const Option kPoolHead[4] = {
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch, kPoolCatScratchOffset, 0, 2, 0,
               "Category", s_poolCatPtrs, NULL, poolPageActive),
        TM_ROW(::tm::ui::kKindInfo, 0, 0, 0, 0, 0, "Pool Status", s_poolStatusLabel, NULL, poolPageActive),
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Reset Pool", "Reset", resetActivePool, poolPageActive),
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Clear Pool", "Clear", clearActivePool, poolPageActive)
    };

    static void initPoolPage(Option* buf)
    {
#define PBIT(idx, def) static_cast<u8>((idx) | ((def) << 7))
        static const u8 kSpec[62] = {
            // Ground (28)
            PBIT(1, 1), PBIT(2, 1), PBIT(3, 1), PBIT(4, 0), PBIT(5, 0), PBIT(32, 0), PBIT(6, 0),
            PBIT(7, 0), PBIT(8, 0), PBIT(9, 0), PBIT(10, 0), PBIT(11, 1), PBIT(12, 1), PBIT(13, 1),
            PBIT(14, 1), PBIT(15, 1), PBIT(16, 1), PBIT(17, 0), PBIT(18, 1), PBIT(19, 0), PBIT(20, 1),
            PBIT(21, 0), PBIT(23, 1), PBIT(34, 0), PBIT(35, 0), PBIT(36, 0), PBIT(37, 0), PBIT(38, 0),
            // Air (17)
            PBIT(1, 1), PBIT(2, 1), PBIT(3, 1), PBIT(4, 0), PBIT(5, 0), PBIT(6, 0), PBIT(7, 0),
            PBIT(8, 0), PBIT(9, 0), PBIT(10, 0), PBIT(11, 1), PBIT(12, 1), PBIT(13, 1), PBIT(14, 1),
            PBIT(15, 1), PBIT(16, 0), PBIT(17, 0),
            // Shield (17)
            PBIT(1, 1), PBIT(2, 1), PBIT(3, 0), PBIT(4, 1), PBIT(5, 1), PBIT(6, 1), PBIT(7, 1),
            PBIT(10, 0), PBIT(12, 1), PBIT(15, 1), PBIT(16, 1), PBIT(17, 0), PBIT(18, 0), PBIT(19, 0),
            PBIT(20, 1), PBIT(29, 0), PBIT(30, 0)
        };
#undef PBIT
        static const u8 kEnds[3] = {28, 45, 62};
        const char* kLists[3] = {kCounterGround, kCounterAir, kCounterShield};
        typedef bool (*GateFn)();
        static const GateFn kGates[3] = {poolCat0, poolCat1, poolCat2};

        memcpy(buf, kPoolHead, sizeof(kPoolHead));
        int cat = 0;
        for (int i = 0; i < 62; i++)
        {
            while (i >= kEnds[cat])
            {
                cat++;
            }
            u8 idx = static_cast<u8>(kSpec[i] & 0x7Fu);
            s8 def = static_cast<s8>(kSpec[i] >> 7);
            Option& o = buf[4 + i];
            o.kind = ::tm::ui::kKindToggle;
            o.flags = ::tm::ui::kFlagBit | ::tm::ui::kFlagHideWhenDisabled;
            o.offset = static_cast<u16>(TM_FIELD(cpuTables.poolBits) + cat * 8 + (idx >> 3));
            o.min = static_cast<u8>(1u << (idx & 7));
            o.def = def;
            o.max = 1;
            o.name = packedLabelAt(kLists[cat], idx);
            o.data = NULL;
            o.onChange = NULL;
            o.enabled = kGates[cat];
        }
        kCounterPool.options = buf;
        kCounterPool.count = 66;
    }

    extern Page kCounterPool = {"Custom Pools", NULL, 0, {0, 0, 0}};

    void initCpuPages(Option* buf)
    {
        kTdiLabels = pageStr(kPs_cpu_kTdiLabels);
        kCounterGround = pageStr(kPs_cpu_kCounterGround);
        kCounterAir = pageStr(kPs_cpu_kCounterAir);
        kCounterShield = pageStr(kPs_cpu_kCounterShield);

        static const u16 kStrs[23] = {
            kPs_fmt_pct, kPs_cpu_kControl, kPs_cpu_kBehavior, kPs_cpu_kRecovery,
            kPs_cpu_kLedgeOption, kPs_cpu_kControlledBy, kPs_cpu_kTdiLabels,
            kPs_fmt_pd, kPs_fmt_d, kPs_cpu_kSdiDirection, kPs_cpu_kAsdi,
            kPs_cpu_kCounterGround, kPs_cpu_kCounterAir, kPs_cpu_kCounterShield,
            kPs_cpu_kShieldTiming, kPs_fmt_df, kPs_cpu_kAdaptiveMode,
            kPs_cpu_kShfflTiming, kPs_cpu_kInfiniteShields, kPs_cpu_kShieldAngle,
            kPs_cpu_kGrabEscape, kPs_cpu_kGrabRelease, kPs_cpu_kCounterModes
        };
        static const void* const kExtra[3] = {cpu::g_moveCpuLabel, cpu::g_freezeCpuLabel, lab::g_diPickLabel};
        static void (*const kCbs[7])() = {
            NULL, cpuPercent, moveCpu, freezeCpu, controlledByChanged, lab::toggleDiStickPicker, counterModeChanged
        };
        static bool (*const kGates[15])() = {
            NULL, cpuLabControl, cpuCustomDiActive, cpuSdiActive, counterLabels, cpuExtraAvailable,
            cpuShfflActive, cpuShieldHealthActive, counterModeGate, counterSingleRows, counterNonOff,
            counterPoolRow, counterPerHitRow, counterReversalRow, counterAdaptiveRow
        };
        static const RowSpec kMainS[14] = {
            {kKindInt,    kFlagHalf, TM_FIELD(cpu.percent), 0, 0, 999, 0, 1, 0, 0},
            {kKindToggle, 0, TM_FIELD(cpu.lockPercent), 0, 0, 1, 0xFF, 1, 0, 0},
            {kKindInt,    0, TM_FIELD(recording.randomPercent[1]), 0, 0, savestate::kMaxRandomPercent, 0, 0, 0, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.control), TM_CHOICE_LO, cpu::kControlLab, 1, 1, 0, 0, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.behavior), TM_CHOICE_LO, 0, 4, 2, 0, 1, 0},
            {kKindMenu,   0, 0, 0, 0, P_CPU_DI, 0xFF, 0, 1, 0},
            {kKindMenu,   0, 0, 0, 0, P_CPU_COUNTERS, 0xFF, 0, 1, 0},
            {kKindMenu,   0, 0, 0, 0, P_TECH, 0xFF, 0, 1, 0},
            {kKindMenu,   0, 0, 0, 0, P_CPU_DEFENSE, 0xFF, 0, 1, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.recovery), TM_CHOICE_LO, cpu::kRecoveryPPlusAi, 2, 3, 0, 1, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.ledgeOption), TM_CHOICE_LO, cpu::kLedgeDefault, 6, 4, 0, 1, 0},
            {kKindFunc,   0, 0, 0, 0, 0, 0xF0, 2, 1, 0},
            {kKindFunc,   0, 0, 0, 0, 0, 0xF1, 3, 1, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.controlledBy), TM_CHOICE_LO, 0, 4, 5, 4, 1, 0}
        };
        static const RowSpec kDiS[8] = {
            {kKindChoice, 0, TM_FIELD(cpu.options.tdi), TM_CHOICE_LO, 0, cpu::kTdiCount - 1, 6, 0, 1, 0},
            {kKindFunc,   kFlagHideWhenDisabled, 0, 0, 0, 0, 0xF2, 5, 2, 0},
            {kKindInt,    kFlagSigned | kFlagHideWhenDisabled, TM_FIELD(cpuTables.customDiX), -100, 100, 100, 7, 0, 2, 0},
            {kKindInt,    kFlagSigned | kFlagHideWhenDisabled, TM_FIELD(cpuTables.customDiY), -100, 0, 100, 7, 0, 2, 0},
            {kKindInt,    0, TM_FIELD(cpu.options.sdiAmount), 0, 0, 7, 8, 0, 1, 0},
            {kKindChoice, kFlagHideWhenDisabled, TM_FIELD(cpu.options.sdiDir), TM_CHOICE_LO, 0, 7, 9, 0, 3, 0},
            {kKindToggle, kFlagHideWhenDisabled, TM_FIELD(cpu.options.dualStickSdi), 0, 0, 1, 0xFF, 0, 3, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.asdi), TM_CHOICE_LO, 0, 6, 10, 0, 1, 0}
        };
        static const RowSpec kCtrS[12] = {
            {kKindChoice, kFlagScratch, kCounterModeScratchOffset, TM_CHOICE_LO, kCtrModeSingle, kCtrModeCount - 1, 22, 6, 8, 0},
            {kKindChoice, kFlagHideWhenDisabled, TM_FIELD(cpu.options.counterGround), TM_CHOICE_LO, 13, kCounterGroundSize - 1, 11, 0, 9, 0},
            {kKindChoice, kFlagHideWhenDisabled, TM_FIELD(cpu.options.counterAir), TM_CHOICE_LO, 1, kCounterAirSize - 1, 12, 0, 9, 0},
            {kKindChoice, kFlagHideWhenDisabled, TM_FIELD(cpu.options.counterShield), TM_CHOICE_LO, 1, kCounterShieldSize - 1, 13, 0, 9, 0},
            {kKindChoice, kFlagHideWhenDisabled, TM_FIELD(cpu.options.shieldTiming), TM_CHOICE_LO, cpu::kShieldTimingFullMove, 1, 14, 0, 10, 0},
            {kKindInt,    kFlagHideWhenDisabled, TM_FIELD(cpu.options.counterDelay), 0, 0, 99, 15, 0, 10, 0},
            {kKindMenu,   kFlagHideWhenDisabled, 0, 0, 0, P_COUNTER_POOL, 0xFF, 0, 11, 0},
            {kKindMenu,   kFlagHideWhenDisabled, 0, 0, 0, P_ADVANCED_COUNTER, 0xFF, 0, 12, 0},
            {kKindMenu,   kFlagHideWhenDisabled, 0, 0, 0, P_REVERSALS, 0xFF, 0, 13, 0},
            {kKindChoice, kFlagHideWhenDisabled, TM_FIELD(cpu.options.adaptiveMode), TM_CHOICE_LO, cpu::kAdaptiveOff, 2, 16, 0, 14, 0},
            {kKindToggle, kFlagHideWhenDisabled, TM_FIELD(cpu.options.autoLCancel), 0, 1, 1, 0xFF, 0, 10, 0},
            {kKindChoice, kFlagHideWhenDisabled, TM_FIELD(cpu.options.shfflTiming), TM_CHOICE_LO, cpu::kShfflFramePerfect, 4, 17, 0, 6, 0}
        };
        static const RowSpec kDefS[7] = {
            {kKindChoice, 0, TM_FIELD(cpu.options.infiniteShields), TM_CHOICE_LO, 1, 2, 18, 0, 1, 0},
            {kKindInt,    kFlagHideWhenDisabled, TM_FIELD(cpu.options.shieldHealth), 0, 60, 60, 8, 0, 7, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.shieldAngle), TM_CHOICE_LO, 0, 4, 19, 0, 1, 0},
            {kKindToggle, 0, TM_FIELD(cpu.options.intangible), 0, 0, 1, 0xFF, 0, 1, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.grabEscape), TM_CHOICE_LO, 0, 3, 20, 0, 1, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.grabRelease), TM_CHOICE_LO, 0, 1, 21, 0, 1, 0},
            {kKindChoice, 0, TM_FIELD(cpu.options.clatterEscape), TM_CHOICE_LO, cpu::kMashNone, 3, 20, 0, 1, 0}
        };
        fillPageRows(kCpuOptions, buf, 14, kMainS, kPs_cpu_main_names, kStrs, kExtra, kCbs, kGates);
        buf += 14;
        fillPageRows(kCpuDi, buf, 8, kDiS, kPs_cpu_di_names, kStrs, kExtra, kCbs, kGates);
        buf += 8;
        fillPageRows(kCpuCounters, buf, 12, kCtrS, kPs_cpu_ctr_names, kStrs, kExtra, kCbs, kGates);
        buf += 12;
        fillPageRows(kCpuDefense, buf, 7, kDefS, kPs_cpu_def_names, kStrs, kExtra, kCbs, kGates);
        buf += 7;
        initReversalPage(buf);
        buf += 15;
        initPoolPage(buf);
    }

    void detachCpuPages()
    {
        kTdiLabels = NULL;
        kCounterGround = NULL;
        kCounterAir = NULL;
        kCounterShield = NULL;
    }
#else
#define TM_POOL_BIT(cat, idx, def, name, gate)                                                                    \
    TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagBit | ::tm::ui::kFlagHideWhenDisabled,                           \
           TM_FIELD(cpuTables.poolBits[cat][(idx) >> 3]), (1 << ((idx) & 7)), 1, def, name, NULL, NULL, gate)

    static const Option kPoolRows[] = {
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch, kPoolCatScratchOffset, 0, 2, 0,
               "Category", s_poolCatPtrs, NULL, poolPageActive),
        TM_ROW(::tm::ui::kKindInfo, 0, 0, 0, 0, 0, "Pool Status", s_poolStatusLabel, NULL, poolPageActive),
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Reset Pool", "Reset", resetActivePool, poolPageActive),
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Clear Pool", "Clear", clearActivePool, poolPageActive),
        // Ground Pool (indices match kGround in actions.cpp)
        TM_POOL_BIT(0, 1, 1, "Spotdodge", poolCat0),
        TM_POOL_BIT(0, 2, 1, "Shield", poolCat0),
        TM_POOL_BIT(0, 3, 1, "Grab", poolCat0),
        TM_POOL_BIT(0, 4, 0, "Up B", poolCat0),
        TM_POOL_BIT(0, 5, 0, "Side B Toward", poolCat0),
        TM_POOL_BIT(0, 32, 0, "Side B Away", poolCat0),
        TM_POOL_BIT(0, 6, 0, "Down B", poolCat0),
        TM_POOL_BIT(0, 7, 0, "Neutral B", poolCat0),
        TM_POOL_BIT(0, 8, 0, "Up Smash", poolCat0),
        TM_POOL_BIT(0, 9, 0, "Down Smash", poolCat0),
        TM_POOL_BIT(0, 10, 0, "Forward Smash", poolCat0),
        TM_POOL_BIT(0, 11, 1, "Roll Away", poolCat0),
        TM_POOL_BIT(0, 12, 1, "Roll Toward", poolCat0),
        TM_POOL_BIT(0, 13, 1, "Jab", poolCat0),
        TM_POOL_BIT(0, 14, 1, "Forward Tilt", poolCat0),
        TM_POOL_BIT(0, 15, 1, "Up Tilt", poolCat0),
        TM_POOL_BIT(0, 16, 1, "Down Tilt", poolCat0),
        TM_POOL_BIT(0, 17, 0, "Dash Attack", poolCat0),
        TM_POOL_BIT(0, 18, 1, "Short Hop", poolCat0),
        TM_POOL_BIT(0, 19, 0, "Full Hop", poolCat0),
        TM_POOL_BIT(0, 20, 1, "Wavedash Away", poolCat0),
        TM_POOL_BIT(0, 21, 0, "Wavedash Toward", poolCat0),
        TM_POOL_BIT(0, 23, 1, "Dash Back", poolCat0),
        TM_POOL_BIT(0, 34, 0, "SH Nair", poolCat0),
        TM_POOL_BIT(0, 35, 0, "SH Fair", poolCat0),
        TM_POOL_BIT(0, 36, 0, "SH Bair", poolCat0),
        TM_POOL_BIT(0, 37, 0, "SH Uair", poolCat0),
        TM_POOL_BIT(0, 38, 0, "SH Dair", poolCat0),
        // Air Pool (indices match kAir in actions.cpp)
        TM_POOL_BIT(1, 1, 1, "Airdodge", poolCat1),
        TM_POOL_BIT(1, 2, 1, "Jump Away", poolCat1),
        TM_POOL_BIT(1, 3, 1, "Jump Toward", poolCat1),
        TM_POOL_BIT(1, 4, 0, "Jump Neutral", poolCat1),
        TM_POOL_BIT(1, 5, 0, "Up B Away", poolCat1),
        TM_POOL_BIT(1, 6, 0, "Up B Toward", poolCat1),
        TM_POOL_BIT(1, 7, 0, "Neutral B", poolCat1),
        TM_POOL_BIT(1, 8, 0, "Side B Away", poolCat1),
        TM_POOL_BIT(1, 9, 0, "Side B Toward", poolCat1),
        TM_POOL_BIT(1, 10, 0, "Down B", poolCat1),
        TM_POOL_BIT(1, 11, 1, "Nair", poolCat1),
        TM_POOL_BIT(1, 12, 1, "Fair", poolCat1),
        TM_POOL_BIT(1, 13, 1, "Bair", poolCat1),
        TM_POOL_BIT(1, 14, 1, "Uair", poolCat1),
        TM_POOL_BIT(1, 15, 1, "Dair", poolCat1),
        TM_POOL_BIT(1, 16, 0, "Tumble Fastfall", poolCat1),
        TM_POOL_BIT(1, 17, 0, "Wiggle Fastfall", poolCat1),
        // Shield Pool (indices match kShieldList in actions.cpp)
        TM_POOL_BIT(2, 1, 1, "Grab", poolCat2),
        TM_POOL_BIT(2, 2, 1, "Short Hop", poolCat2),
        TM_POOL_BIT(2, 3, 0, "Full Hop", poolCat2),
        TM_POOL_BIT(2, 4, 1, "Spotdodge", poolCat2),
        TM_POOL_BIT(2, 5, 1, "Roll Away", poolCat2),
        TM_POOL_BIT(2, 6, 1, "Roll Toward", poolCat2),
        TM_POOL_BIT(2, 7, 1, "Up B", poolCat2),
        TM_POOL_BIT(2, 10, 0, "Down B", poolCat2),
        TM_POOL_BIT(2, 12, 1, "Up Smash", poolCat2),
        TM_POOL_BIT(2, 15, 1, "SH Nair", poolCat2),
        TM_POOL_BIT(2, 16, 1, "SH Fair", poolCat2),
        TM_POOL_BIT(2, 17, 0, "SH Bair", poolCat2),
        TM_POOL_BIT(2, 18, 0, "SH Uair", poolCat2),
        TM_POOL_BIT(2, 19, 0, "SH Dair", poolCat2),
        TM_POOL_BIT(2, 20, 1, "Wavedash Away", poolCat2),
        TM_POOL_BIT(2, 29, 0, "Wavedash Toward", poolCat2),
        TM_POOL_BIT(2, 30, 0, "Wavedash Down", poolCat2)
    };
#undef TM_POOL_BIT

    extern const Page kCounterPool = TM_PAGE("Custom Pools", kPoolRows);
#endif
}
}
}

#undef NA
#undef DROPPED
