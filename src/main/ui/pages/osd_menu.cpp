#include "main/extra.h"
#include "main/lab.h"
#include "main/ui/osd.h"
#include "main/ui/osd_place.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    // In osd::Position order (main/ui/osd_place.h).
    TM_LABELS_BEGIN(kPosition) TM_L("HUD") TM_L("Sides") TM_L("Top Left") TM_L("Top Right") TM_LABELS_END
    TM_LABELS_ASSERT(kPosition, osd::kPositionCount, tm_osd_positions_match);

    // In main::TimingMeterMode order (main/settings.h).
    TM_LABELS_BEGIN(kTimingMeter) TM_L("Off") TM_L("All") TM_L("L-Cancel") TM_L("Wavedash") TM_L("Act-Out") TM_LABELS_END
    TM_LABELS_ASSERT(kTimingMeter, main::kTimingMeterCount, tm_timing_meter_labels_match);

    // In main::OsdPreset order (main/settings.h).
    TM_LABELS_BEGIN(kOsdPresets) TM_L("Custom") TM_L("Essentials") TM_L("Movement") TM_L("Defense") TM_L("All Off") TM_LABELS_END
    TM_LABELS_ASSERT(kOsdPresets, main::kOsdPresetCount, tm_osd_presets_match);

    // In main::TechSparkleMode order (main/settings.h).
    TM_LABELS_BEGIN(kTechSparkle) TM_L("Off") TM_L("Perfect Wavedash") TM_L("Shorthop") TM_L("Both") TM_LABELS_END
    TM_LABELS_ASSERT(kTechSparkle, main::kTechSparkleCount, tm_tech_sparkle_labels_match);

    static void osdRowEdited()
    {
        main::settings().osd.preset = main::kOsdPresetCustom;
    }

    static void osdPresetChanged()
    {
        u8 preset = main::settings().osd.preset;
        if (preset == main::kOsdPresetCustom || preset >= main::kOsdPresetCount)
        {
            return;
        }
        static const u16 kToggleOffsets[20] = {
            TM_FIELD(osd.wavedash),
            TM_FIELD(osd.lCancel),
            TM_FIELD(osd.ledgedashInfo),
            TM_FIELD(osd.actOos),
            TM_FIELD(osd.frameAdvantage),
            TM_FIELD(osd.frameAdvantageOnHit),
            TM_FIELD(osd.dashback),
            TM_FIELD(osd.powershield),
            TM_FIELD(osd.actOoLag),
            TM_FIELD(osd.actOoAirborne),
            TM_FIELD(osd.actOoHitstun),
            TM_FIELD(osd.jumpCancel),
            TM_FIELD(osd.fastfall),
            TM_FIELD(osd.boostGrab),
            TM_FIELD(osd.sdi),
            TM_FIELD(osd.lockoutTimers),
            TM_FIELD(osd.comboCounter),
            TM_FIELD(osd.grabBreakout),
            TM_FIELD(osd.itemThrow),
            TM_FIELD(cpu.options.techTrackerOsd)
        };
        // Bitmasks matching kToggleOffsets order for Essentials / Movement / Defense / All Off.
        static const u32 kPresetMasks[main::kOsdPresetCount - 1] = {
            0x0003Fu, // Essentials: Wavedash, L-Cancel, Ledgedash, Act OoS, Shield Adv, Hit Adv
            0x83B47u, // Movement: Wavedash, L-Cancel, Ledgedash, Dashback, OoLag, OoAir, JC, FF, DACUS, P+ Tech
            0x2C498u, // Defense: Act OoS, Shield Adv, Powershield, OoHitstun, SDI, Tech Lockout, Grab Escape
            0x00000u  // All Off
        };
        u32 mask = kPresetMasks[preset - 1];
        u8* raw = reinterpret_cast<u8*>(&main::settings());
        for (int i = 0; i < 20; i++)
        {
            raw[kToggleOffsets[i]] = static_cast<u8>((mask >> i) & 1u);
        }
    }

    static bool advToleranceEnabled()
    {
        const main::OsdSettings& o = main::settings().osd;
        return o.frameAdvantage != 0 || o.frameAdvantageOnHit != 0;
    }

    static const Option kRows[] = {
        // Layout & meta controls.
        TM_CHOICE_P("OSD Preset", osd.preset, kOsdPresets, main::kOsdPresetCount, main::kOsdPresetEssentials, osdPresetChanged),
        TM_CHOICE_P("OSD Position", osd.position, kPosition, osd::kPositionCount, osd::kPositionSides, lab::osdSettingsChanged),
        TM_INT("Max OSDs", osd.maxMessages, 1, osd::kPerPlayer, 4, "%d", NULL),
        TM_ROW_CHOICE_P(0, TM_FIELD(osd.timingMeter), main::kTimingMeterCount, main::kTimingMeterOff,
                        "Overhead Timing Bar", kTimingMeter, NULL, main::extraLoaded),
        TM_ROW_CHOICE_P(0, TM_FIELD(osd.techSparkle), main::kTechSparkleCount, main::kTechSparkleWavedash,
                        "Tech Sparkle", kTechSparkle, NULL, main::extraLoaded),
        TM_ROW(kKindToggle, 0, TM_FIELD(cpu.options.techTrackerOsd), 0, 1, 0, "P+ Tech OSD", NULL, osdRowEdited,
               main::extraLoaded),
        // Highest-use P+ competitive OSDs (Essentials defaults On).
        TM_TOGGLE("Wavedash", osd.wavedash, 1, osdRowEdited),
        TM_TOGGLE("L-Cancel", osd.lCancel, 1, osdRowEdited),
        TM_TOGGLE("Ledgedash Info", osd.ledgedashInfo, 1, osdRowEdited),
        TM_TOGGLE("Act OoS Frame", osd.actOos, 1, osdRowEdited),
        TM_TOGGLE("Shield Advantage", osd.frameAdvantage, 1, osdRowEdited),
        TM_TOGGLE("Hit Advantage", osd.frameAdvantageOnHit, 1, osdRowEdited),
        TM_ROW(kKindInt, kFlagHideWhenDisabled, TM_FIELD(osd.advantageTolerance), 0, 5, 1, "Advantage Tolerance",
               "%df", lab::osdSettingsChanged, advToleranceEnabled),
        // Remaining technique & defensive OSDs.
        TM_TOGGLE("Dashback", osd.dashback, 0, osdRowEdited),
        TM_TOGGLE("Powershield", osd.powershield, 0, osdRowEdited),
        TM_TOGGLE("Act OoLag", osd.actOoLag, 0, osdRowEdited),
        TM_TOGGLE("Act OoAirborne", osd.actOoAirborne, 0, osdRowEdited),
        TM_TOGGLE("Act OoHitstun", osd.actOoHitstun, 0, osdRowEdited),
        TM_TOGGLE("Jump Cancel", osd.jumpCancel, 0, osdRowEdited),
        TM_TOGGLE("Fastfall Timing", osd.fastfall, 0, osdRowEdited),
        TM_TOGGLE("Boost Grab / DACUS", osd.boostGrab, 0, osdRowEdited),
        TM_TOGGLE("SDI Inputs", osd.sdi, 0, osdRowEdited),
        TM_TOGGLE("Tech Lockout", osd.lockoutTimers, 0, osdRowEdited),
        TM_TOGGLE("Combo Counter", osd.comboCounter, 0, osdRowEdited),
        TM_TOGGLE("Grab Escape", osd.grabBreakout, 0, osdRowEdited),
        TM_TOGGLE("Item Throw", osd.itemThrow, 0, osdRowEdited)
    };

    extern const Page kOsdMenu = TM_PAGE("OSDs", kRows);
}
}
}
