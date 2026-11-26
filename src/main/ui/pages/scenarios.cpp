#include "main/ui/pages/scenarios.h"

#include <stddef.h>

#include "main/cpu/actions.h"
#include "main/extra.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

// Unified P+ Lab Scenario Presets and Combo Trainer entry.
// Preset values live in ui::menuScratch() at kScenarioScratchOffset (session-only).
namespace tm {
namespace ui {
namespace pages {
#ifndef __MWERKS__
    TM_LABELS_EXTERN_DECL(kTechInvisLabels, 4);
    TM_LABELS_EXTERN_DECL(kPreTechDriftLabels, 6);
#endif

#pragma dont_inline on
    static u8 activePreset()
    {
        return main::extraLoaded() ? scenarioMenuState()->preset : kPresetOff;
    }

    static bool presetIs(u8 id)
    {
        return activePreset() == id;
    }

    static bool presetFieldEq(u8 id, u16 off, u8 val, bool eq)
    {
        if (activePreset() != id)
        {
            return false;
        }
        u8 cur = reinterpret_cast<const u8*>(scenarioMenuState())[off];
        return eq ? (cur == val) : (cur != val);
    }
#pragma dont_inline reset

    static bool comboTrainerAvailable()
    {
        return cpuLabControl() && main::extraLoaded();
    }

    static bool presetRowVisible()
    {
        return main::extraLoaded() && practiceMenuState()->activeDrill == 0;
    }

    static bool comboTrainerRowVisible()
    {
        return comboTrainerAvailable() && practiceMenuState()->activeDrill == 0;
    }

    static bool scenarioActive()
    {
        return activePreset() != kPresetOff;
    }

    static bool scenarioSideActive()
    {
        u8 p = activePreset();
        return p != kPresetOff && p != kPresetShieldDrop;
    }

    static bool scenarioComboLabActive() { return presetIs(kPresetComboLab); }
    static bool scenarioEditAirPoolActive()
    {
        bool on = scenarioComboLabActive()
                  && main::settings().comboTrainer.postHitstunEscape == main::kComboEscapeCustomPool;
        if (on)
        {
            primePoolCategory(cpu::kListAir);
        }
        else
        {
            clearPoolCategoryPrime(cpu::kListAir);
        }
        return on;
    }
    static bool scenarioTechchaseActive() { return presetIs(kPresetTechchase); }
    static bool scenarioTechInvisDelayActive()
    {
        return presetFieldEq(kPresetTechchase, offsetof(ScenarioMenuState, techInvisMode), 0, false);
    }
    static bool scenarioEdgeguardActive() { return presetIs(kPresetEdgeguard); }
    static bool scenarioEdgeguardLaunchActive()
    {
        return presetFieldEq(kPresetEdgeguard, offsetof(ScenarioMenuState, edgeguardSetup), 0, true);
    }
    static bool scenarioEdgeguardSpawnActive()
    {
        return presetFieldEq(kPresetEdgeguard, offsetof(ScenarioMenuState, edgeguardSetup), 1, true);
    }
    static bool scenarioShieldPressureActive() { return presetIs(kPresetShieldPressure); }
    static bool scenarioShieldOffenseActive()
    {
        return presetFieldEq(kPresetShieldPressure, offsetof(ScenarioMenuState, shieldRole), 0, true);
    }
    static bool scenarioEditShieldPoolActive()
    {
        bool on = scenarioShieldOffenseActive()
                  && presetFieldEq(kPresetShieldPressure, offsetof(ScenarioMenuState, oosAction), 7, true);
        if (on)
        {
            primePoolCategory(cpu::kListShield);
        }
        else
        {
            clearPoolCategoryPrime(cpu::kListShield);
        }
        return on;
    }
    static bool scenarioShieldDefenseActive()
    {
        return presetFieldEq(kPresetShieldPressure, offsetof(ScenarioMenuState, shieldRole), 1, true);
    }
    static bool scenarioShieldDropActive() { return presetIs(kPresetShieldDrop); }
    static bool scenarioLedgetechActive() { return presetIs(kPresetLedgetech); }
    static bool scenarioSlideOffActive() { return presetIs(kPresetSlideOff); }
    static bool scenarioGrabActive() { return presetIs(kPresetGrabEscape); }
    static bool scenarioMultiHitActive() { return presetIs(kPresetMultiHitSdi); }

    static void practiceChanged() { main::extraScenarioCall(main::kScenarioPracticeChanged); }

    static bool practiceActive() { return main::extraLoaded() && practiceMenuState()->activeDrill != 0; }

#pragma dont_inline on
    static bool practiceDrillIs(u8 id) { return main::extraLoaded() && practiceMenuState()->activeDrill == id; }
#pragma dont_inline reset

    static bool practiceLedgeActive() { return practiceDrillIs(1); }
    static bool practiceWdActive() { return practiceDrillIs(2); }
    static bool practiceLcActive() { return practiceDrillIs(3); }
    static bool practiceFloatActive() { return practiceDrillIs(4); }
    static bool practicePsActive() { return practiceDrillIs(5); }
    static bool practicePsProjActive() { return practiceDrillIs(5) && practiceMenuState()->psAttackMode == 0; }

#ifdef __MWERKS__
    static const u16 kScenLblOffs[42] = {
        kPs_scen_kPresets, kPs_scen_kSides, kPs_scen_kTechSetups, kPs_scen_kTechPositions, kPs_scen_kTechEscapes,
        kPs_tech_kTechInvisLabels, kPs_tech_kPreTechDriftLabels, kPs_scen_kTechTdis, kPs_scen_kEdgeSetups,
        kPs_scen_kEdgeStrengths, kPs_scen_kEdgeDists, kPs_scen_kEdgeDjs, kPs_scen_kEdgeMixups, kPs_scen_kShieldRoles,
        kPs_scen_kOosActions, kPs_scen_kShieldDefAttacks, kPs_scen_kLtStarts, kPs_scen_kLtTimings,
        kPs_scen_kLedgetechSpawns, kPs_scen_kLedgetechAttacks, kPs_scen_kShieldDropPlats, kPs_scen_kShieldDropAttacks,
        kPs_scen_kShieldDropSpacing, kPs_scen_kShieldDropFacing, kPs_scen_kSlideOffAttacks, kPs_scen_kGrabActions,
        kPs_scen_kGrabPctModes, kPs_scen_kMultiHitMoves, kPs_scen_kPracticeModes, kPs_scen_kPsAtkModes,
        kPs_scen_kPsHeights, kPs_scen_kPsCadences, kPs_scen_kLedgeSides, kPs_scen_kLedgeInvulns,
        kPs_scen_kPracticeTargets, kPs_fmt_df,
        kPs_ct_kPostEscapes, kPs_ct_kResetDelays, kPs_cpu_kTdiLabels, kPs_tech_kTech,
        kPs_scen_kEdgeResets, kPs_scen_kSlideOffStarts
    };
    static bool (*const kScenGates[])() = {
        NULL, presetRowVisible, scenarioActive, scenarioSideActive, scenarioTechchaseActive,
        scenarioTechInvisDelayActive, scenarioEdgeguardActive, scenarioEdgeguardLaunchActive,
        scenarioEdgeguardSpawnActive, scenarioShieldPressureActive, scenarioShieldOffenseActive,
        scenarioShieldDefenseActive, scenarioLedgetechActive, scenarioShieldDropActive, scenarioSlideOffActive,
        scenarioGrabActive, scenarioMultiHitActive, practiceActive, practicePsActive, practicePsProjActive,
        practiceLedgeActive, practiceLcActive, practiceFloatActive, practiceWdActive, comboTrainerRowVisible,
        scenarioComboLabActive, scenarioEditAirPoolActive, scenarioEditShieldPoolActive
    };
    static void (*const kScenCbs[])() = {
        NULL, main::extraScenarioChanged, main::extraScenarioResetNow, practiceChanged
    };
    enum {
        kScenRows = 55,
        kScenFlagHide = ::tm::ui::kFlagHideWhenDisabled,
        kScenFlagSH = ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled
    };
#define SC_OFF(f) static_cast<u16>(kScenarioScratchOffset + offsetof(ScenarioMenuState, f))
#define PR_OFF(f) static_cast<u16>(kPracticeScratchOffset + offsetof(PracticeMenuState, f))
    static const RowSpec kScenSpecs[kScenRows] = {
        {kKindChoice, kScenFlagSH, SC_OFF(preset), TM_CHOICE_LO, 0, 9, 0, 1, 1, 0},
        {kKindToggle, kScenFlagSH, SC_OFF(autoReset), 0, 1, 1, 0xFF, 1, 2, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(side), TM_CHOICE_LO, 0, 2, 1, 1, 3, 0},
        // Combo Lab inline quick-knobs
        {kKindChoice, kScenFlagHide, TM_FIELD(comboTrainer.postHitstunEscape), TM_CHOICE_LO, main::kComboEscapeDodge,
         main::kComboEscapeCount - 1, 36, 0, 25, 0},
        {kKindMenu,   kScenFlagHide, 0, 0, 0, P_COUNTER_POOL, 0xFF, 0, 26, 0},
        {kKindChoice, kScenFlagHide, TM_FIELD(comboTrainer.resetDelay), TM_CHOICE_LO, main::kComboDelay30,
         main::kComboDelayCount - 1, 37, 0, 25, 0},
        {kKindChoice, kScenFlagHide, TM_FIELD(cpu.options.tdi), TM_CHOICE_LO, 0, cpu::kTdiCount - 1, 38, 0, 25, 0},
        {kKindChoice, kScenFlagHide, TM_FIELD(cpu.options.tech), TM_CHOICE_LO, 0, 4, 39, 0, 25, 0},
        {kKindToggle, kScenFlagHide, TM_FIELD(comboTrainer.resetP1), 0, 1, 1, 0xFF, 0, 25, 0},
        {kKindToggle, kScenFlagHide, TM_FIELD(comboTrainer.noclip), 0, 1, 1, 0xFF, 0, 25, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(techSetup), TM_CHOICE_LO, 0, 1, 2, 1, 4, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(techPosition), TM_CHOICE_LO, 0, 4, 3, 1, 4, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(techEscape), TM_CHOICE_LO, 0, 6, 4, 1, 4, 0},
        {kKindToggle, kScenFlagSH, SC_OFF(techShowReaction), 0, 0, 1, 0xFF, 1, 4, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(techInvisMode), TM_CHOICE_LO, 0, 3, 5, 1, 4, 0},
        {kKindInt,    kScenFlagSH, SC_OFF(techInvisDelay), 0, 0, 15, 35, 1, 5, 0},
        {kKindToggle, kScenFlagSH, SC_OFF(techSound), 0, 0, 1, 0xFF, 1, 4, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(techPreDrift), TM_CHOICE_LO, 0, 5, 6, 1, 4, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(techTdi), TM_CHOICE_LO, 0, 4, 7, 1, 4, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(edgeguardSetup), TM_CHOICE_LO, 0, 1, 8, 1, 6, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(edgeguardStrength), TM_CHOICE_LO, 1, 3, 9, 1, 7, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(edgeguardDist), TM_CHOICE_LO, 0, 3, 10, 1, 8, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(edgeguardDj), TM_CHOICE_LO, 0, 2, 11, 1, 8, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(edgeguardMixup), TM_CHOICE_LO, 0, 3, 12, 1, 6, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(edgeguardReset), TM_CHOICE_LO, 0, 1, 40, 1, 6, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(shieldRole), TM_CHOICE_LO, 0, 1, 13, 1, 9, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(oosAction), TM_CHOICE_LO, 0, 7, 14, 1, 10, 0},
        {kKindMenu,   kScenFlagHide, 0, 0, 0, P_COUNTER_POOL, 0xFF, 0, 27, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(shieldDefAttack), TM_CHOICE_LO, 0, 5, 15, 1, 11, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(ledgetechStart), TM_CHOICE_LO, 0, 1, 16, 1, 12, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(ledgetechTiming), TM_CHOICE_LO, 0, 1, 17, 1, 12, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(ledgetechSpawn), TM_CHOICE_LO, 0, 4, 18, 1, 12, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(ledgetechDj), TM_CHOICE_LO, 0, 1, 11, 1, 12, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(ledgetechAttack), TM_CHOICE_LO, 3, 7, 19, 1, 12, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(shieldDropPlat), TM_CHOICE_LO, 0, 3, 20, 1, 13, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(shieldDropAttack), TM_CHOICE_LO, 0, 3, 21, 1, 13, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(shieldDropSpacing), TM_CHOICE_LO, 0, 3, 22, 1, 13, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(shieldDropFacing), TM_CHOICE_LO, 0, 2, 23, 1, 13, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(slideOffStart), TM_CHOICE_LO, 0, 2, 41, 1, 14, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(slideOffAttack), TM_CHOICE_LO, 0, 3, 24, 1, 14, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(grabAction), TM_CHOICE_LO, 0, 6, 25, 1, 15, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(grabPctMode), TM_CHOICE_LO, 0, 1, 26, 1, 15, 0},
        {kKindChoice, kScenFlagSH, SC_OFF(multiHitMove), TM_CHOICE_LO, 0, 8, 27, 1, 16, 0},
        {kKindFunc,   kScenFlagHide, 0, 0, 0, 0, 0xFF, 2, 2, 0},
        {kKindChoice, kScenFlagSH, PR_OFF(mode), TM_CHOICE_LO, 0, 2, 28, 3, 17, 0},
        {kKindChoice, kScenFlagSH, PR_OFF(psAttackMode), TM_CHOICE_LO, 0, 1, 29, 3, 18, 0},
        {kKindChoice, kScenFlagSH, PR_OFF(psHeight), TM_CHOICE_LO, 0, 4, 30, 3, 19, 0},
        {kKindChoice, kScenFlagSH, PR_OFF(psCadence), TM_CHOICE_LO, 1, 3, 31, 3, 18, 0},
        {kKindChoice, kScenFlagSH, PR_OFF(ledgeResetSide), TM_CHOICE_LO, 0, 2, 32, 3, 20, 0},
        {kKindToggle, kScenFlagSH, PR_OFF(ledgeAutoReset), 0, 1, 1, 0xFF, 3, 20, 0},
        {kKindChoice, kScenFlagSH, PR_OFF(ledgeInvulnLimit), TM_CHOICE_LO, 0, 1, 33, 3, 20, 0},
        {kKindChoice, kScenFlagSH, PR_OFF(lcancelTarget), TM_CHOICE_LO, 1, 3, 34, 3, 21, 0},
        {kKindChoice, kScenFlagSH, PR_OFF(floatDjcTarget), TM_CHOICE_LO, 1, 3, 34, 3, 22, 0},
        {kKindToggle, kScenFlagSH, PR_OFF(wdTargetZone), 0, 0, 1, 0xFF, 3, 23, 0},
        {kKindMenu,   kScenFlagHide, 0, 0, 0, P_COMBO_TRAINER, 0xFF, 0, 24, 0}
    };
#undef SC_OFF
#undef PR_OFF

    extern Page kScenarios = {"Scenarios", NULL, 0, {0, 0, 0}};

    void initScenariosPage(Option* buf)
    {
        fillPageRows(kScenarios, buf, kScenRows, kScenSpecs, kPs_scen_names, kScenLblOffs, NULL, kScenCbs, kScenGates);
    }
#else
    TM_LABELS_BEGIN(kPresets)
        TM_L("Off") TM_L("Combo Lab") TM_L("Techchase") TM_L("Edgeguard") TM_L("Shield Pressure / OoS")
        TM_L("Shield Drop") TM_L("Ledgetech") TM_L("Slide-Off") TM_L("Grab Defense / Throw DI") TM_L("P+ Multi-Hit SDI")
    TM_LABELS_END
    TM_LABELS_BEGIN(kSides) TM_L("Right") TM_L("Left") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kTechSetups) TM_L("Tumble Drop") TM_L("Free Hit") TM_LABELS_END
    TM_LABELS_BEGIN(kTechPositions) TM_L("Ground") TM_L("Platform 1") TM_L("Platform 2") TM_L("Platform 3") TM_L("Platform 4") TM_LABELS_END
    TM_LABELS_BEGIN(kTechEscapes) TM_L("Intang Flash") TM_L("Spotdodge") TM_L("Jab") TM_L("Jump") TM_L("Shield") TM_L("Roll") TM_L("None") TM_LABELS_END
    TM_LABELS_BEGIN(kTechTdis) TM_L("Random") TM_L("None") TM_L("Away") TM_L("Toward") TM_L("Survival") TM_LABELS_END
    TM_LABELS_BEGIN(kEdgeSetups) TM_L("Knockback Launch") TM_L("Offstage Spawn") TM_LABELS_END
    TM_LABELS_BEGIN(kEdgeStrengths) TM_L("Weak") TM_L("Normal") TM_L("Strong") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kEdgeDists) TM_L("Mid") TM_L("Deep") TM_L("High") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kEdgeDjs) TM_L("Available") TM_L("Spent") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kEdgeMixups) TM_L("Mixed") TM_L("Low (Sweetspot)") TM_L("High") TM_L("Direct") TM_LABELS_END
    TM_LABELS_BEGIN(kEdgeResets) TM_L("Stage Safe") TM_L("Ledge Grab") TM_LABELS_END
    TM_LABELS_BEGIN(kShieldRoles) TM_L("Offense") TM_L("Defense") TM_LABELS_END
    TM_LABELS_BEGIN(kOosActions)
        TM_L("Grab") TM_L("Up B") TM_L("Spotdodge") TM_L("Roll Away") TM_L("SH Nair") TM_L("Wavedash") TM_L("Random") TM_L("Custom Pool")
    TM_LABELS_END
    TM_LABELS_BEGIN(kShieldDefAttacks)
        TM_L("Aerial") TM_L("Tilt") TM_L("Smash") TM_L("Dash Attack") TM_L("Getup Attack") TM_L("Random")
    TM_LABELS_END
    TM_LABELS_BEGIN(kLtStarts) TM_L("Revival Platform") TM_L("Instant Drop") TM_LABELS_END
    TM_LABELS_BEGIN(kLtTimings) TM_L("Hold Active") TM_L("Timed Swing") TM_LABELS_END
    TM_LABELS_BEGIN(kLedgetechSpawns)
        TM_L("Close Low") TM_L("Deep Low") TM_L("Far Out") TM_L("High") TM_L("Random")
    TM_LABELS_END
    TM_LABELS_BEGIN(kLedgetechAttacks)
        TM_L("Down Tilt") TM_L("Fwd Tilt") TM_L("F-Smash") TM_L("Down Smash") TM_L("Down Air") TM_L("Neutral B") TM_L("Side B") TM_L("Random")
    TM_LABELS_END
    TM_LABELS_BEGIN(kShieldDropPlats) TM_L("Left") TM_L("Right") TM_L("Top") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kShieldDropAttacks) TM_L("SH Aerial") TM_L("Tilt") TM_L("Down Smash") TM_L("Off") TM_LABELS_END
    TM_LABELS_BEGIN(kShieldDropSpacing) TM_L("Random") TM_L("Close") TM_L("Under") TM_L("Far") TM_LABELS_END
    TM_LABELS_BEGIN(kShieldDropFacing) TM_L("Toward") TM_L("Away") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kSlideOffStarts) TM_L("Knockdown") TM_L("Tumble") TM_L("Standing") TM_LABELS_END
    TM_LABELS_BEGIN(kSlideOffAttacks) TM_L("Under Up Air") TM_L("Under Fair") TM_L("Under Nair") TM_L("On-Plat Ftilt") TM_LABELS_END
    TM_LABELS_BEGIN(kGrabActions)
        TM_L("Hold (Mash Out)") TM_L("Random Throw") TM_L("F-Throw") TM_L("B-Throw") TM_L("U-Throw") TM_L("D-Throw") TM_L("Stand (P1 Grabs)")
    TM_LABELS_END
    TM_LABELS_BEGIN(kGrabPctModes) TM_L("Random 0-79%") TM_L("Current P1 %") TM_LABELS_END
    TM_LABELS_BEGIN(kMultiHitMoves)
        TM_L("Auto (Best)") TM_L("Nair") TM_L("Fair") TM_L("Up Air") TM_L("Down Air") TM_L("Rapid Jab") TM_L("Up B") TM_L("Side B") TM_L("Down B")
    TM_LABELS_END
    TM_LABELS_BEGIN(kPracticeModes) TM_L("Free Practice") TM_L("10-Rep Set") TM_L("20-Rep Set") TM_LABELS_END
    TM_LABELS_BEGIN(kPsAtkModes) TM_L("Projectile") TM_L("Physical") TM_LABELS_END
    TM_LABELS_BEGIN(kPsHeights) TM_L("Low") TM_L("Mid") TM_L("High") TM_L("Ground") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kPsCadences) TM_L("Slow") TM_L("Medium") TM_L("Fast") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kLedgeSides) TM_L("Same Side") TM_L("Opposite") TM_L("Random") TM_LABELS_END
    TM_LABELS_BEGIN(kLedgeInvulns) TM_L("P+ 5-Grab Limit") TM_L("Unlimited") TM_LABELS_END
    TM_LABELS_BEGIN(kPracticeTargets) TM_L("Off") TM_L("Stationary CPU") TM_L("Shielding CPU") TM_L("Moving CPU") TM_LABELS_END
    TM_LABELS_BEGIN(kComboPostEscapes) TM_L("Dodge") TM_L("Jump-Invuln") TM_L("Counter") TM_L("Custom Pool") TM_L("None") TM_LABELS_END
    TM_LABELS_BEGIN(kComboResetDelays) TM_L("Instant") TM_L("15f") TM_L("30f") TM_L("45f") TM_L("60f") TM_LABELS_END
    TM_LABELS_BEGIN(kComboTechLabels) TM_L("Random") TM_L("In Place") TM_L("Away") TM_L("Toward") TM_L("Miss") TM_LABELS_END

#define TM_SCEN_OFF(f) static_cast<u16>(kScenarioScratchOffset + offsetof(ScenarioMenuState, f))
#define TM_SCEN_CHOICE(name, field, labels, count, def, en)                                               \
    TM_ROW_CHOICE_P(::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,                             \
                    TM_SCEN_OFF(field), count, def, name, labels, main::extraScenarioChanged, en)
#define TM_PRAC_OFF(f) static_cast<u16>(kPracticeScratchOffset + offsetof(PracticeMenuState, f))
#define TM_PRAC_CHOICE(name, field, labels, count, def, en)                                               \
    TM_ROW_CHOICE_P(::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,                             \
                    TM_PRAC_OFF(field), count, def, name, labels, practiceChanged, en)
#define TM_PRAC_TOGGLE(name, field, def, en)                                                \
    TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, \
           TM_PRAC_OFF(field), 0, 1, def, name, NULL, practiceChanged, en)

    static const Option kRows[] = {
        TM_ROW_CHOICE_P(::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, TM_SCEN_OFF(preset), 10, 0,
                        "Preset", kPresets, main::extraScenarioChanged, presetRowVisible),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, TM_SCEN_OFF(autoReset),
               0, 1, 1, "Auto-Reset", NULL, main::extraScenarioChanged, scenarioActive),
        TM_SCEN_CHOICE("Side", side, kSides, 3, 0, scenarioSideActive),
        // Combo Lab inline quick-knobs
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(comboTrainer.postHitstunEscape),
                        main::kComboEscapeCount, main::kComboEscapeDodge, "Post-Hitstun", kComboPostEscapes, NULL,
                        scenarioComboLabActive),
        TM_ROW(::tm::ui::kKindMenu, ::tm::ui::kFlagHideWhenDisabled, 0, 0, P_COUNTER_POOL, 0, "Edit Air Pool", NULL,
               NULL, scenarioEditAirPoolActive),
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(comboTrainer.resetDelay), main::kComboDelayCount,
                        main::kComboDelay30, "Reset Delay", kComboResetDelays, NULL, scenarioComboLabActive),
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.tdi), cpu::kTdiCount, 0,
                        "Trajectory DI", kTdiLabels, NULL, scenarioComboLabActive),
        TM_ROW_CHOICE_P(::tm::ui::kFlagHideWhenDisabled, TM_FIELD(cpu.options.tech), 5, 0,
                        "Tech Option", kComboTechLabels, NULL, scenarioComboLabActive),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(comboTrainer.resetP1), 0, 1, 1,
               "Reset P1", NULL, NULL, scenarioComboLabActive),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagHideWhenDisabled, TM_FIELD(comboTrainer.noclip), 0, 1, 1,
               "Noclip Reset", NULL, NULL, scenarioComboLabActive),
        // Techchase inline rows
        TM_SCEN_CHOICE("Setup", techSetup, kTechSetups, 2, 0, scenarioTechchaseActive),
        TM_SCEN_CHOICE("Position", techPosition, kTechPositions, 5, 0, scenarioTechchaseActive),
        TM_SCEN_CHOICE("Escape", techEscape, kTechEscapes, 7, 0, scenarioTechchaseActive),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(techShowReaction), 0, 1, 0, "Reaction Frame", NULL, main::extraScenarioChanged,
               scenarioTechchaseActive),
        TM_SCEN_CHOICE("Tech Invisibility", techInvisMode, kTechInvisLabels, 4, 0, scenarioTechchaseActive),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(techInvisDelay), 0, 15, 0, "Invis Delay", "%df", main::extraScenarioChanged,
               scenarioTechInvisDelayActive),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(techSound), 0, 1, 0, "Tech Sound Cue", NULL, main::extraScenarioChanged,
               scenarioTechchaseActive),
        TM_SCEN_CHOICE("Pre-Tech Drift", techPreDrift, kPreTechDriftLabels, 6, 0, scenarioTechchaseActive),
        TM_SCEN_CHOICE("CPU Launch DI", techTdi, kTechTdis, 5, 0, scenarioTechchaseActive),
        // Edgeguard inline rows
        TM_SCEN_CHOICE("Setup", edgeguardSetup, kEdgeSetups, 2, 0, scenarioEdgeguardActive),
        TM_SCEN_CHOICE("Launch Strength", edgeguardStrength, kEdgeStrengths, 4, 1, scenarioEdgeguardLaunchActive),
        TM_SCEN_CHOICE("Distance", edgeguardDist, kEdgeDists, 4, 0, scenarioEdgeguardSpawnActive),
        TM_SCEN_CHOICE("CPU Double Jump", edgeguardDj, kEdgeDjs, 3, 0, scenarioEdgeguardSpawnActive),
        TM_SCEN_CHOICE("Recovery Mixup", edgeguardMixup, kEdgeMixups, 4, 0, scenarioEdgeguardActive),
        TM_SCEN_CHOICE("Reset On", edgeguardReset, kEdgeResets, 2, 0, scenarioEdgeguardActive),
        // Shield Pressure / OoS rows
        TM_SCEN_CHOICE("Role", shieldRole, kShieldRoles, 2, 0, scenarioShieldPressureActive),
        TM_SCEN_CHOICE("CPU OoS", oosAction, kOosActions, 8, 0, scenarioShieldOffenseActive),
        TM_ROW(::tm::ui::kKindMenu, ::tm::ui::kFlagHideWhenDisabled, 0, 0, P_COUNTER_POOL, 0, "Edit Shield Pool", NULL,
               NULL, scenarioEditShieldPoolActive),
        TM_SCEN_CHOICE("CPU Attack", shieldDefAttack, kShieldDefAttacks, 6, 0, scenarioShieldDefenseActive),
        // Ledgetech rows
        TM_SCEN_CHOICE("Start", ledgetechStart, kLtStarts, 2, 0, scenarioLedgetechActive),
        TM_SCEN_CHOICE("Timing", ledgetechTiming, kLtTimings, 2, 0, scenarioLedgetechActive),
        TM_SCEN_CHOICE("Spawn Position", ledgetechSpawn, kLedgetechSpawns, 5, 0, scenarioLedgetechActive),
        TM_ROW_CHOICE_P(::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
                        TM_SCEN_OFF(ledgetechDj), 2, 0, "P1 Double Jump", kEdgeDjs, main::extraScenarioChanged,
                        scenarioLedgetechActive),
        TM_SCEN_CHOICE("CPU Edgeguard", ledgetechAttack, kLedgetechAttacks, 8, 3, scenarioLedgetechActive),
        // Shield Drop rows
        TM_SCEN_CHOICE("Platform", shieldDropPlat, kShieldDropPlats, 4, 0, scenarioShieldDropActive),
        TM_SCEN_CHOICE("CPU Attack", shieldDropAttack, kShieldDropAttacks, 4, 0, scenarioShieldDropActive),
        TM_SCEN_CHOICE("CPU Spacing", shieldDropSpacing, kShieldDropSpacing, 4, 0, scenarioShieldDropActive),
        TM_SCEN_CHOICE("CPU Facing", shieldDropFacing, kShieldDropFacing, 3, 0, scenarioShieldDropActive),
        // Slide-Off rows
        TM_SCEN_CHOICE("P1 Start", slideOffStart, kSlideOffStarts, 3, 0, scenarioSlideOffActive),
        TM_SCEN_CHOICE("CPU Attack", slideOffAttack, kSlideOffAttacks, 4, 0, scenarioSlideOffActive),
        // Grab Defense rows
        TM_SCEN_CHOICE("CPU Action", grabAction, kGrabActions, 7, 0, scenarioGrabActive),
        TM_SCEN_CHOICE("P1 Damage", grabPctMode, kGrabPctModes, 2, 0, scenarioGrabActive),
        // Multi-Hit SDI rows
        TM_SCEN_CHOICE("Multi-Hit Move", multiHitMove, kMultiHitMoves, 9, 0, scenarioMultiHitActive),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagHideWhenDisabled, 0, 0, 0, 0, "Reset Now", NULL,
               main::extraScenarioResetNow, scenarioActive),
        // Active Tech Practice drill options (hidden unless a Tech Practice drill is running)
        TM_PRAC_CHOICE("Practice Mode", mode, kPracticeModes, 3, 0, practiceActive),
        TM_PRAC_CHOICE("Attack Mode", psAttackMode, kPsAtkModes, 2, 0, practicePsActive),
        TM_PRAC_CHOICE("Shot Height", psHeight, kPsHeights, 5, 0, practicePsProjActive),
        TM_PRAC_CHOICE("Cadence", psCadence, kPsCadences, 4, 1, practicePsActive),
        TM_PRAC_CHOICE("Reset Ledge", ledgeResetSide, kLedgeSides, 3, 0, practiceLedgeActive),
        TM_PRAC_TOGGLE("Ledge Auto-Reset", ledgeAutoReset, 1, practiceLedgeActive),
        TM_PRAC_CHOICE("Ledge Invuln", ledgeInvulnLimit, kLedgeInvulns, 2, 0, practiceLedgeActive),
        TM_PRAC_CHOICE("Target", lcancelTarget, kPracticeTargets, 4, 1, practiceLcActive),
        TM_PRAC_CHOICE("Target", floatDjcTarget, kPracticeTargets, 4, 1, practiceFloatActive),
        TM_PRAC_TOGGLE("Target Zone", wdTargetZone, 0, practiceWdActive),
        TM_ROW(::tm::ui::kKindMenu, ::tm::ui::kFlagHideWhenDisabled, 0, 0, P_COMBO_TRAINER, 0, "More Combo Options",
               NULL, NULL, comboTrainerRowVisible)
    };
#undef TM_PRAC_TOGGLE
#undef TM_PRAC_CHOICE
#undef TM_PRAC_OFF
#undef TM_SCEN_CHOICE
#undef TM_SCEN_OFF

    extern const Page kScenarios = TM_PAGE("Scenarios", kRows);
#endif
}
}
}
