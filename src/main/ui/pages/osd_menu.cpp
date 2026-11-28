#include <cstdio>
#include <string.h>

#include "main/extra.h"
#include "main/lab.h"
#include "main/storage/notice.h"
#include "main/ui/osd.h"
#include "main/ui/osd_place.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
#ifndef __MWERKS__
    // In osd::Position order (main/ui/osd_place.h).
    TM_LABELS_BEGIN(kPosition) TM_L("HUD") TM_L("Sides") TM_L("Top Left") TM_L("Top Right") TM_LABELS_END
    TM_LABELS_ASSERT(kPosition, osd::kPositionCount, tm_osd_positions_match);

    // In main::TimingMeterMode order (main/settings.h).
    TM_LABELS_BEGIN(kTimingMeter) TM_L("Off") TM_L("All") TM_L("L-Cancel") TM_L("Wavedash") TM_L("Act-Out") TM_LABELS_END
    TM_LABELS_ASSERT(kTimingMeter, main::kTimingMeterCount, tm_timing_meter_labels_match);

    // In main::OsdPreset order (main/settings.h).
    TM_LABELS_BEGIN(kOsdPresets) TM_L("Custom") TM_L("Essentials") TM_L("Movement") TM_L("Defense") TM_L("All Off") TM_L("Custom 1") TM_L("Custom 2") TM_L("Custom 3") TM_LABELS_END
    TM_LABELS_ASSERT(kOsdPresets, main::kOsdPresetCount, tm_osd_presets_match);

    // In Custom 1..3 save target order.
    TM_LABELS_BEGIN(kOsdSaveTargets) TM_L("Custom 1") TM_L("Custom 2") TM_L("Custom 3") TM_LABELS_END
    TM_LABELS_ASSERT(kOsdSaveTargets, main::kOsdCustomSlotCount, tm_osd_save_targets_match);

    // In main::TechSparkleMode order (main/settings.h).
    TM_LABELS_BEGIN(kTechSparkle) TM_L("Off") TM_L("Perfect Wavedash") TM_L("Shorthop") TM_L("Both") TM_LABELS_END
    TM_LABELS_ASSERT(kTechSparkle, main::kTechSparkleCount, tm_tech_sparkle_labels_match);
#endif

    static char s_osdSaveLabel[12] = "Empty";

    static void osdRowEdited()
    {
        main::settings().osd.preset = main::kOsdPresetCustom;
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

    static u8& osdSaveSlot()
    {
        u8& slot = ui::menuScratch()[ui::kOsdSaveSlotScratchOffset];
        if (slot >= main::kOsdCustomSlotCount)
        {
            slot = 0;
        }
        return slot;
    }

    void osdSyncSaveLabel()
    {
        u32 saved = main::settings().osdPresets.customMask[osdSaveSlot()];
        if ((saved & main::kOsdCustomSavedBit) == 0u)
        {
            strcpy(s_osdSaveLabel, "Empty");
            return;
        }
        int n = 0;
        u32 m = saved & 0xFFFFFu;
        for (int i = 0; i < 20; i++)
        {
            n += static_cast<int>((m >> i) & 1u);
        }
        snprintf(s_osdSaveLabel, sizeof(s_osdSaveLabel), "%d OSDs", n);
    }

    static void osdSavePressed()
    {
        u8 slot = osdSaveSlot();
        u32 mask = 0u;
        int count = 0;
        const u8* raw = reinterpret_cast<const u8*>(&main::settings());
        for (int i = 0; i < 20; i++)
        {
            if (raw[kToggleOffsets[i]] != 0)
            {
                mask |= (1u << i);
                count++;
            }
        }
        main::settings().osdPresets.customMask[slot] = main::kOsdCustomSavedBit | mask;
        main::settings().osd.preset = static_cast<u8>(main::kOsdPresetCustom1 + slot);
        osdSyncSaveLabel();
        storage::notice(storage::kNoticeOk, "Saved %d OSDs to Custom %d", count, slot + 1);
    }

    static void osdPresetChanged()
    {
        u8 preset = main::settings().osd.preset;
        if (preset == main::kOsdPresetCustom || preset >= main::kOsdPresetCount)
        {
            return;
        }
        u32 mask = 0u;
        if (preset >= main::kOsdPresetCustom1)
        {
            u8 slot = static_cast<u8>(preset - main::kOsdPresetCustom1);
            ui::menuScratch()[ui::kOsdSaveSlotScratchOffset] = slot;
            osdSyncSaveLabel();
            u32 saved = main::settings().osdPresets.customMask[slot];
            if ((saved & main::kOsdCustomSavedBit) == 0u)
            {
                return;
            }
            mask = saved & 0xFFFFFu;
        }
        else
        {
            // Bitmasks matching kToggleOffsets order for Essentials / Movement / Defense / All Off.
            static const u32 kPresetMasks[4] = {
                0x0003Fu, // Essentials: Wavedash, L-Cancel, Ledgedash, Act OoS, Shield Adv, Hit Adv
                0x83B47u, // Movement: Wavedash, L-Cancel, Ledgedash, Dashback, OoLag, OoAir, JC, FF, DACUS, P+ Tech
                0x2C498u, // Defense: Act OoS, Shield Adv, Powershield, OoHitstun, SDI, Tech Lockout, Grab Escape
                0x00000u  // All Off
            };
            mask = kPresetMasks[preset - 1];
        }
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

#ifdef __MWERKS__
    extern Page kOsdMenu = {"OSDs", NULL, 0, {0, 0, 0}};
    extern Page kOsdIndividual = {"Individual OSDs", NULL, 0, {0, 0, 0}};

    void initOsdMenuPage(Option* buf)
    {
        static const u16 kStrs[6] = {
            kPs_osd_kOsdPresets, kPs_osd_kOsdSaveTargets, kPs_osd_kPosition,
            kPs_osd_kTimingMeter, kPs_osd_kTechSparkle, kPs_fmt_d
        };
        static const void* const kExtra[1] = {s_osdSaveLabel};
        static void (*const kCbs[5])() = {
            NULL, osdPresetChanged, osdSyncSaveLabel, osdSavePressed, lab::osdSettingsChanged
        };
        static bool (*const kGates[2])() = {NULL, main::extraLoaded};
        static const RowSpec kSpecs[8] = {
            {kKindChoice, 0, TM_FIELD(osd.preset), TM_CHOICE_LO, main::kOsdPresetEssentials, main::kOsdPresetCount - 1, 0, 1, 0, 0},
            {kKindChoice, kFlagScratch, ui::kOsdSaveSlotScratchOffset, TM_CHOICE_LO, 0, main::kOsdCustomSlotCount - 1, 1, 2, 0, 0},
            {kKindFunc,   0, 0, 0, 0, 0, 0xF0, 3, 0, 0},
            {kKindChoice, 0, TM_FIELD(osd.position), TM_CHOICE_LO, osd::kPositionSides, osd::kPositionCount - 1, 2, 4, 0, 0},
            {kKindInt,    0, TM_FIELD(osd.maxMessages), 1, 4, osd::kPerPlayer, 5, 0, 0, 0},
            {kKindChoice, 0, TM_FIELD(osd.timingMeter), TM_CHOICE_LO, main::kTimingMeterOff, main::kTimingMeterCount - 1, 3, 0, 1, 0},
            {kKindChoice, 0, TM_FIELD(osd.techSparkle), TM_CHOICE_LO, main::kTechSparkleWavedash, main::kTechSparkleCount - 1, 4, 0, 1, 0},
            {kKindMenu,   0, 0, 0, 0, P_OSD_INDIVIDUAL, 0xFF, 0, 0, 0}
        };
        fillPageRows(kOsdMenu, buf, 8, kSpecs, kPs_osd_names, kStrs, kExtra, kCbs, kGates);
        const char* nm = pageStr(kPs_osd_names);
        for (int s = 0; s < 8; s++)
        {
            while (*nm++ != '\0') {}
        }
        int dst = 8;
        for (int i = 0; i < 20; i++)
        {
            if (i == 6)
            {
                Option& tol = buf[dst++];
                tol.kind = kKindInt;
                tol.flags = kFlagHideWhenDisabled;
                tol.offset = TM_FIELD(osd.advantageTolerance);
                tol.def = 1;
                tol.max = 5;
                tol.name = nm;
                tol.data = pageStr(kPs_fmt_df);
                tol.onChange = lab::osdSettingsChanged;
                tol.enabled = advToleranceEnabled;
                while (*nm++ != '\0') {}
            }
            Option& o = buf[dst++];
            o.kind = kKindToggle;
            o.offset = kToggleOffsets[i];
            o.def = i < 6 ? 1 : 0;
            o.max = 1;
            o.name = nm;
            o.onChange = osdRowEdited;
            if (i == 19)
            {
                o.enabled = main::extraLoaded;
            }
            while (*nm++ != '\0') {}
        }
        kOsdIndividual.options = buf + 8;
        kOsdIndividual.count = 21;
        osdSyncSaveLabel();
    }
#else
    static const Option kRows[] = {
        TM_CHOICE_P("OSD Preset", osd.preset, kOsdPresets, main::kOsdPresetCount, main::kOsdPresetEssentials,
                    osdPresetChanged),
        TM_ROW_CHOICE_P(::tm::ui::kFlagScratch, ui::kOsdSaveSlotScratchOffset, main::kOsdCustomSlotCount, 0,
                        "Save Target", kOsdSaveTargets, osdSyncSaveLabel, NULL),
        TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, "Save Current OSDs", s_osdSaveLabel, osdSavePressed, NULL),
        TM_CHOICE_P("OSD Position", osd.position, kPosition, osd::kPositionCount, osd::kPositionSides,
                    lab::osdSettingsChanged),
        TM_INT("Max OSDs", osd.maxMessages, 1, osd::kPerPlayer, 4, "%d", NULL),
        TM_ROW_CHOICE_P(0, TM_FIELD(osd.timingMeter), main::kTimingMeterCount, main::kTimingMeterOff,
                        "Overhead Timing Bar", kTimingMeter, NULL, main::extraLoaded),
        TM_ROW_CHOICE_P(0, TM_FIELD(osd.techSparkle), main::kTechSparkleCount, main::kTechSparkleWavedash,
                        "Tech Sparkle", kTechSparkle, NULL, main::extraLoaded),
        TM_MENU("Individual OSDs", P_OSD_INDIVIDUAL)
    };

    static const Option kIndivRows[] = {
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
        TM_TOGGLE("Item Throw", osd.itemThrow, 0, osdRowEdited),
        TM_ROW(kKindToggle, 0, TM_FIELD(cpu.options.techTrackerOsd), 0, 1, 0, "P+ Tech OSD", NULL, osdRowEdited,
               main::extraLoaded)
    };

    extern const Page kOsdMenu = TM_PAGE("OSDs", kRows);
    extern const Page kOsdIndividual = TM_PAGE("Individual OSDs", kIndivRows);
#endif
}
}
}
