#include "main/ui/pages/scenarios.h"

#include <stddef.h>

#include "main/extra.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"

// Unified P+ Lab Scenario Presets and Combo Trainer entry.
// Preset values live in ui::menuScratch() at kScenarioScratchOffset (session-only).
namespace tm {
namespace ui {
namespace pages {
    extern const char* const kTechInvisLabels[4];
    extern const char* const kPreTechDriftLabels[6];

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

    static bool scenarioActive()
    {
        return activePreset() != kPresetOff;
    }

    static bool scenarioSideActive()
    {
        u8 p = activePreset();
        return p != kPresetOff && p != kPresetShieldDrop;
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

    static const char* const kPresets[] = {
        "Off", "Combo Lab", "Techchase", "Edgeguard", "Shield Pressure / OoS",
        "Shield Drop", "Ledgetech", "Slide-Off", "Grab Defense / Throw DI", "P+ Multi-Hit SDI"
    };
    static const char* const kSides[] = {"Right", "Left", "Random"};
    static const char* const kTechSetups[] = {"Tumble Drop", "Free Hit"};
    static const char* const kTechPositions[] = {"Ground", "Platform 1", "Platform 2", "Platform 3", "Platform 4"};
    static const char* const kTechEscapes[] = {"Intang Flash", "Spotdodge", "Jab", "Jump", "Shield", "Roll", "None"};
    static const char* const kTechTdis[] = {"Random", "None", "Away", "Toward", "Survival"};
    static const char* const kEdgeSetups[] = {"Knockback Launch", "Offstage Spawn"};
    static const char* const kEdgeStrengths[] = {"Weak", "Normal", "Strong", "Random"};
    static const char* const kEdgeDists[] = {"Mid", "Deep", "High", "Random"};
    static const char* const kEdgeDjs[] = {"Available", "Spent", "Random"};
    static const char* const kEdgeMixups[] = {"Mixed", "Low (Sweetspot)", "High", "Direct"};
    static const char* const kShieldRoles[] = {"Offense", "Defense"};
    static const char* const kOosActions[] = {
        "Grab", "Up B", "Spotdodge", "Roll Away", "SH Nair", "Wavedash", "Random", "Custom Pool"
    };
    static const char* const kShieldDefAttacks[] = {
        "Aerial", "Tilt", "Smash", "Dash Attack", "Getup Attack", "Random"
    };
    static const char* const kLtStarts[] = {"Revival Platform", "Instant Drop"};
    static const char* const kLtTimings[] = {"Hold Active", "Timed Swing"};
    static const char* const kLedgetechSpawns[] = {
        "Close Low", "Deep Low", "Far Out", "High", "Random"
    };
    static const char* const kLedgetechAttacks[] = {
        "Down Tilt", "Fwd Tilt", "F-Smash", "Down Smash", "Down Air", "Neutral B", "Side B", "Random"
    };
    static const char* const kShieldDropPlats[] = {"Left", "Right", "Top", "Random"};
    static const char* const kShieldDropAttacks[] = {"SH Aerial", "Tilt", "Down Smash", "Off"};
    static const char* const kShieldDropSpacing[] = {"Random", "Close", "Under", "Far"};
    static const char* const kShieldDropFacing[] = {"Toward", "Away", "Random"};
    static const char* const kSlideOffAttacks[] = {"Under Up Air", "Under Fair", "Under Nair", "On-Plat Ftilt"};
    static const char* const kGrabActions[] = {
        "Hold (Mash Out)", "Random Throw", "F-Throw", "B-Throw", "U-Throw", "D-Throw", "Stand (P1 Grabs)"
    };
    static const char* const kGrabPctModes[] = {"Random 0-79%", "Current P1 %"};
    static const char* const kMultiHitMoves[] = {
        "Auto (Best)", "Nair", "Fair", "Up Air", "Down Air", "Rapid Jab", "Up B", "Side B", "Down B"
    };
    static const char* const kPracticeModes[] = {"Free Practice", "10-Rep Set", "20-Rep Set"};
    static const char* const kPsAtkModes[] = {"Projectile", "Physical"};
    static const char* const kPsHeights[] = {"Low", "Mid", "High", "Ground", "Random"};
    static const char* const kPsCadences[] = {"Slow", "Medium", "Fast", "Random"};
    static const char* const kLedgeSides[] = {"Same", "Opposite", "Random"};
    static const char* const kLedgeInvulns[] = {"P+ 5-Grab", "Unlimited"};
    static const char* const kPracticeTargets[] = {"Off", "Stationary CPU", "Shielding CPU", "Moving CPU"};

#define TM_SCEN_OFF(f) static_cast<u16>(kScenarioScratchOffset + offsetof(ScenarioMenuState, f))
#define TM_SCEN_CHOICE(name, field, labels, def, en)                                                      \
    TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,               \
           TM_SCEN_OFF(field), 0, TM_COUNT(labels) - 1, def, name, labels, main::extraScenarioChanged, en)
#define TM_PRAC_OFF(f) static_cast<u16>(kPracticeScratchOffset + offsetof(PracticeMenuState, f))
#define TM_PRAC_CHOICE(name, field, labels, def, en)                                        \
    TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, \
           TM_PRAC_OFF(field), 0, TM_COUNT(labels) - 1, def, name, labels, practiceChanged, en)
#define TM_PRAC_TOGGLE(name, field, def, en)                                                \
    TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, \
           TM_PRAC_OFF(field), 0, 1, def, name, NULL, practiceChanged, en)

    static const Option kRows[] = {
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch, TM_SCEN_OFF(preset), 0, TM_COUNT(kPresets) - 1, 0,
               "Preset", kPresets, main::extraScenarioChanged, main::extraLoaded),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled, TM_SCEN_OFF(autoReset),
               0, 1, 1, "Auto-Reset", NULL, main::extraScenarioChanged, scenarioActive),
        TM_SCEN_CHOICE("Side", side, kSides, 0, scenarioSideActive),
        // Techchase inline rows
        TM_SCEN_CHOICE("Setup", techSetup, kTechSetups, 0, scenarioTechchaseActive),
        TM_SCEN_CHOICE("Position", techPosition, kTechPositions, 0, scenarioTechchaseActive),
        TM_SCEN_CHOICE("Escape", techEscape, kTechEscapes, 0, scenarioTechchaseActive),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(techShowReaction), 0, 1, 0, "Reaction Frame", NULL, main::extraScenarioChanged,
               scenarioTechchaseActive),
        TM_SCEN_CHOICE("Tech Invisibility", techInvisMode, kTechInvisLabels, 0, scenarioTechchaseActive),
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(techInvisDelay), 0, 15, 0, "Invis Delay", "%df", main::extraScenarioChanged,
               scenarioTechInvisDelayActive),
        TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(techSound), 0, 1, 0, "Tech Sound Cue", NULL, main::extraScenarioChanged,
               scenarioTechchaseActive),
        TM_SCEN_CHOICE("Pre-Tech Drift", techPreDrift, kPreTechDriftLabels, 0, scenarioTechchaseActive),
        TM_SCEN_CHOICE("CPU Launch DI", techTdi, kTechTdis, 0, scenarioTechchaseActive),
        // Edgeguard inline rows
        TM_SCEN_CHOICE("Setup", edgeguardSetup, kEdgeSetups, 0, scenarioEdgeguardActive),
        TM_SCEN_CHOICE("Launch Strength", edgeguardStrength, kEdgeStrengths, 1, scenarioEdgeguardLaunchActive),
        TM_SCEN_CHOICE("Distance", edgeguardDist, kEdgeDists, 0, scenarioEdgeguardSpawnActive),
        TM_SCEN_CHOICE("CPU Double Jump", edgeguardDj, kEdgeDjs, 0, scenarioEdgeguardSpawnActive),
        TM_SCEN_CHOICE("Recovery Mixup", edgeguardMixup, kEdgeMixups, 0, scenarioEdgeguardActive),
        // Shield Pressure / OoS rows
        TM_SCEN_CHOICE("Role", shieldRole, kShieldRoles, 0, scenarioShieldPressureActive),
        TM_SCEN_CHOICE("CPU OoS", oosAction, kOosActions, 0, scenarioShieldOffenseActive),
        TM_SCEN_CHOICE("CPU Attack", shieldDefAttack, kShieldDefAttacks, 0, scenarioShieldDefenseActive),
        // Ledgetech rows
        TM_SCEN_CHOICE("Start", ledgetechStart, kLtStarts, 0, scenarioLedgetechActive),
        TM_SCEN_CHOICE("Timing", ledgetechTiming, kLtTimings, 0, scenarioLedgetechActive),
        TM_SCEN_CHOICE("Spawn Position", ledgetechSpawn, kLedgetechSpawns, 0, scenarioLedgetechActive),
        TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHideWhenDisabled,
               TM_SCEN_OFF(ledgetechDj), 0, 1, 0, "P1 Double Jump", kEdgeDjs, main::extraScenarioChanged,
               scenarioLedgetechActive),
        TM_SCEN_CHOICE("CPU Edgeguard", ledgetechAttack, kLedgetechAttacks, 3, scenarioLedgetechActive),
        // Shield Drop rows
        TM_SCEN_CHOICE("Platform", shieldDropPlat, kShieldDropPlats, 0, scenarioShieldDropActive),
        TM_SCEN_CHOICE("CPU Attack", shieldDropAttack, kShieldDropAttacks, 0, scenarioShieldDropActive),
        TM_SCEN_CHOICE("CPU Spacing", shieldDropSpacing, kShieldDropSpacing, 0, scenarioShieldDropActive),
        TM_SCEN_CHOICE("CPU Facing", shieldDropFacing, kShieldDropFacing, 0, scenarioShieldDropActive),
        // Slide-Off rows
        TM_SCEN_CHOICE("CPU Attack", slideOffAttack, kSlideOffAttacks, 0, scenarioSlideOffActive),
        // Grab Defense rows
        TM_SCEN_CHOICE("CPU Action", grabAction, kGrabActions, 0, scenarioGrabActive),
        TM_SCEN_CHOICE("P1 Damage", grabPctMode, kGrabPctModes, 0, scenarioGrabActive),
        // Multi-Hit SDI rows
        TM_SCEN_CHOICE("Multi-Hit Move", multiHitMove, kMultiHitMoves, 0, scenarioMultiHitActive),
        TM_ROW(::tm::ui::kKindFunc, ::tm::ui::kFlagHideWhenDisabled, 0, 0, 0, 0, "Reset Now", NULL,
               main::extraScenarioResetNow, scenarioActive),
        // Active Tech Practice drill options (hidden unless a Tech Practice drill is running)
        TM_PRAC_CHOICE("Practice Mode", mode, kPracticeModes, 0, practiceActive),
        TM_PRAC_CHOICE("Attack Mode", psAttackMode, kPsAtkModes, 0, practicePsActive),
        TM_PRAC_CHOICE("Shot Height", psHeight, kPsHeights, 0, practicePsProjActive),
        TM_PRAC_CHOICE("Cadence", psCadence, kPsCadences, 1, practicePsActive),
        TM_PRAC_CHOICE("Reset Ledge", ledgeResetSide, kLedgeSides, 0, practiceLedgeActive),
        TM_PRAC_TOGGLE("Ledge Auto-Reset", ledgeAutoReset, 1, practiceLedgeActive),
        TM_PRAC_CHOICE("Ledge Invuln", ledgeInvulnLimit, kLedgeInvulns, 0, practiceLedgeActive),
        TM_PRAC_CHOICE("Target", lcancelTarget, kPracticeTargets, 1, practiceLcActive),
        TM_PRAC_CHOICE("Target", floatDjcTarget, kPracticeTargets, 1, practiceFloatActive),
        TM_PRAC_TOGGLE("Target Zone", wdTargetZone, 0, practiceWdActive),
        TM_MENU_IF("Combo Trainer", P_COMBO_TRAINER, comboTrainerAvailable)
    };
#undef TM_PRAC_TOGGLE
#undef TM_PRAC_CHOICE
#undef TM_PRAC_OFF
#undef TM_SCEN_CHOICE
#undef TM_SCEN_OFF

    extern const Page kScenarios = TM_PAGE("Scenarios", kRows);
}
}
}
