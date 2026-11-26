#include "main/ui/pages/olimar.h"

#include <stddef.h>

#include "extra/olimar_pikmin_logic.h"
#include "main/extra.h"
#include "main/settings.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

// Stage & Cast > Olimar submenu (P_OLIMAR): bit-packed storage in
// FighterMechanicsSettings bytes 455..463 mirrored into OlimarMenuState
// scratch bytes while open, plus 5 direct kFlagBit Pull Pool toggles.
namespace tm {
namespace ui {
namespace pages {
#define TM_OLI_SCR(f) static_cast<u16>(kOlimarScratchOffset + offsetof(OlimarMenuState, f))

    void olimarSyncFromSettings()
    {
        main::FighterMechanicsSettings& fm = main::settings().fighterMechanics;
        u8 rawPool = extra::olimarRawPullMask(fm);
        if ((rawPool == 0u || rawPool == main::kOlimarPullPoolAll)
            && fm.olimarPikmin >= 1u && fm.olimarPikmin < main::kOlimarPikminCount)
        {
            extra::olimarSetRawPullMask(fm, static_cast<u8>(1u << (fm.olimarPikmin - 1u)));
            fm.olimarPikmin = main::kPikminDefault;
        }
        OlimarMenuState* st = olimarMenuState();
        st->dpadMode = extra::olimarDpadMode(fm);
        st->whistleColor = extra::olimarWhistleColor(fm);
        u8 common = extra::olimarEffectiveColorMaturityChoice(fm, 0);
        bool allSame = true;
        for (int c = 0; c < extra::kOliColorCount; c++)
        {
            u8 v = extra::olimarEffectiveColorMaturityChoice(fm, c);
            st->colorMat[c] = v;
            if (v != common)
            {
                allSame = false;
            }
        }
        st->allMaturity = allSame ? common : 0u;
        for (int i = 0; i < 4; i++)
        {
            st->startSlot[i] = extra::olimarStartSlot(fm, i);
            st->dpadPreset[i] = extra::olimarDpadPreset(fm, i);
        }
    }

    static void olimarPullChanged()
    {
        // User explicitly edited the 5-bit Pull Pool: retire legacy single-color override.
        main::settings().fighterMechanics.olimarPikmin = main::kPikminDefault;
        main::extraCharacterRng();
    }

    static void olimarScratchChanged()
    {
        main::FighterMechanicsSettings& fm = main::settings().fighterMechanics;
        const OlimarMenuState* st = olimarMenuState();
        extra::olimarSetDpadMode(fm, st->dpadMode);
        extra::olimarSetWhistleColor(fm, st->whistleColor);
        for (int i = 0; i < 4; i++)
        {
            extra::olimarSetStartSlot(fm, i, st->startSlot[i]);
            extra::olimarSetDpadPreset(fm, i, st->dpadPreset[i]);
        }
        main::extraCharacterRng();
    }

    static void olimarAllMatChanged()
    {
        main::FighterMechanicsSettings& fm = main::settings().fighterMechanics;
        OlimarMenuState* st = olimarMenuState();
        u8 v = st->allMaturity < main::kOlimarMaturityCount ? st->allMaturity : 0u;
        fm.olimarMaturity = v;
        for (int c = 0; c < extra::kOliColorCount; c++)
        {
            extra::olimarSetColorMaturityRaw(fm, c, v);
            st->colorMat[c] = v;
        }
        main::extraCharacterRng();
    }

    static void olimarColorMatChanged()
    {
        main::FighterMechanicsSettings& fm = main::settings().fighterMechanics;
        OlimarMenuState* st = olimarMenuState();
        u8 first = st->colorMat[0] < main::kOlimarMaturityCount ? st->colorMat[0] : 0u;
        bool allSame = true;
        for (int c = 0; c < extra::kOliColorCount; c++)
        {
            u8 v = st->colorMat[c] < main::kOlimarMaturityCount ? st->colorMat[c] : 0u;
            extra::olimarSetColorMaturityRaw(fm, c, v);
            if (v != first)
            {
                allSame = false;
            }
        }
        fm.olimarMaturity = allSame ? first : 0u;
        st->allMaturity = fm.olimarMaturity;
        main::extraCharacterRng();
    }

    static void olimarMoveLeadPressed()
    {
        main::extraOlimarAction(main::kOlimarMoveLead, 0u, 0u);
    }

#ifdef __MWERKS__
    extern Page kOlimar = {"Olimar", NULL, 0, {0, 0, 0}};

    void initOlimarPage(Option* buf)
    {
        static const u16 kStrs[5] = {
            kPs_oli_kOlimarDpadMode, kPs_oli_kOlimarColor, kPs_oli_kOlimarMaturity,
            kPs_oli_kOlimarStartSlot, kPs_oli_kOlimarPresets
        };
        static void (*const kCbs[6])() = {
            NULL, olimarPullChanged, olimarScratchChanged, olimarAllMatChanged,
            olimarColorMatChanged, olimarMoveLeadPressed
        };
        const u16 kPullOff = TM_FIELD(fighterMechanics.olimarPullAndWhistle);
        static const RowSpec kSpecs[22] = {
            {kKindChoice, kFlagScratch, TM_OLI_SCR(dpadMode), TM_CHOICE_LO, 0, main::kOlimarDpadModeCount - 1, 0, 2, 0, 0},
            {kKindToggle, kFlagBit, kPullOff, 0x01, 1, 1, 0xFF, 1, 0, 0},
            {kKindToggle, kFlagBit, kPullOff, 0x02, 1, 1, 0xFF, 1, 0, 0},
            {kKindToggle, kFlagBit, kPullOff, 0x04, 1, 1, 0xFF, 1, 0, 0},
            {kKindToggle, kFlagBit, kPullOff, 0x08, 1, 1, 0xFF, 1, 0, 0},
            {kKindToggle, kFlagBit, kPullOff, 0x10, 1, 1, 0xFF, 1, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(whistleColor), TM_CHOICE_LO, 0, main::kOlimarPikminCount - 1, 1, 2, 0, 0},
            {kKindFunc,   0, 0, 0, 0, 0, 0xFF, 5, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(allMaturity), TM_CHOICE_LO, 0, main::kOlimarMaturityCount - 1, 2, 3, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(colorMat) + 0, TM_CHOICE_LO, 0, main::kOlimarMaturityCount - 1, 2, 4, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(colorMat) + 1, TM_CHOICE_LO, 0, main::kOlimarMaturityCount - 1, 2, 4, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(colorMat) + 2, TM_CHOICE_LO, 0, main::kOlimarMaturityCount - 1, 2, 4, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(colorMat) + 3, TM_CHOICE_LO, 0, main::kOlimarMaturityCount - 1, 2, 4, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(colorMat) + 4, TM_CHOICE_LO, 0, main::kOlimarMaturityCount - 1, 2, 4, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(startSlot) + 0, TM_CHOICE_LO, 0, main::kOlimarStartSlotCount - 1, 3, 2, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(startSlot) + 1, TM_CHOICE_LO, 0, main::kOlimarStartSlotCount - 1, 3, 2, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(startSlot) + 2, TM_CHOICE_LO, 0, main::kOlimarStartSlotCount - 1, 3, 2, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(startSlot) + 3, TM_CHOICE_LO, 0, main::kOlimarStartSlotCount - 1, 3, 2, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(dpadPreset) + 0, TM_CHOICE_LO, 0, main::kOlimarPresetCount - 1, 4, 2, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(dpadPreset) + 1, TM_CHOICE_LO, 0, main::kOlimarPresetCount - 1, 4, 2, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(dpadPreset) + 2, TM_CHOICE_LO, 0, main::kOlimarPresetCount - 1, 4, 2, 0, 0},
            {kKindChoice, kFlagScratch, TM_OLI_SCR(dpadPreset) + 3, TM_CHOICE_LO, 0, main::kOlimarPresetCount - 1, 4, 2, 0, 0}
        };
        fillPageRows(kOlimar, buf, 22, kSpecs, kPs_oli_names, kStrs, NULL, kCbs, NULL);
    }
#else
    TM_LABELS_BEGIN(kOlimarDpadMode) TM_L("Off") TM_L("B+D-Pad Pluck") TM_L("Lineup Presets") TM_LABELS_END
    TM_LABELS_BEGIN(kOlimarColor)
        TM_L("Default") TM_L("Red") TM_L("Yellow") TM_L("Blue") TM_L("White") TM_L("Purple")
    TM_LABELS_END
    TM_LABELS_BEGIN(kOlimarMaturity) TM_L("Default") TM_L("Leaf") TM_L("Bud") TM_L("Flower") TM_LABELS_END
    TM_LABELS_BEGIN(kOlimarStartSlot)
        TM_L("Default") TM_L("Red") TM_L("Yellow") TM_L("Blue") TM_L("White") TM_L("Purple") TM_L("Empty")
    TM_LABELS_END
    TM_LABELS_BEGIN(kOlimarPresets)
        TM_L("Start Lineup") TM_L("4 Red") TM_L("4 Yellow") TM_L("4 Blue")
        TM_L("4 White") TM_L("4 Purple") TM_L("2Y + 2P") TM_L("2P + 2W")
        TM_L("2R + 2P") TM_L("2B + 2P") TM_L("P+Y+W+R") TM_L("Y+P+B+R")
        TM_L("Y + P") TM_L("P + W") TM_L("1 Purple") TM_L("1 Yellow")
    TM_LABELS_END

    TM_LABELS_ASSERT(kOlimarDpadMode, main::kOlimarDpadModeCount, tm_olimar_dpad_mode_labels_match);
    TM_LABELS_ASSERT(kOlimarColor, main::kOlimarPikminCount, tm_olimar_color_labels_match);
    TM_LABELS_ASSERT(kOlimarMaturity, main::kOlimarMaturityCount, tm_olimar_maturity_labels_match);
    TM_LABELS_ASSERT(kOlimarStartSlot, main::kOlimarStartSlotCount, tm_olimar_start_slot_labels_match);
    TM_LABELS_ASSERT(kOlimarPresets, main::kOlimarPresetCount, tm_olimar_presets_labels_match);

#define TM_OLI_BIT(name, bit)                                                                                  \
    TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagBit, TM_FIELD(fighterMechanics.olimarPullAndWhistle),         \
           bit, 1, 1, name, NULL, olimarPullChanged, NULL)
#define TM_OLI_SCR_CHOICE(name, off, labels, count, cb)                                                        \
    TM_ROW_CHOICE_P(::tm::ui::kFlagScratch, off, count, 0, name, labels, cb, NULL)

    static const Option kRows[] = {
        TM_OLI_SCR_CHOICE("D-Pad Mode", TM_OLI_SCR(dpadMode), kOlimarDpadMode, main::kOlimarDpadModeCount, olimarScratchChanged),
        TM_OLI_BIT("Pull: Red", 0x01),
        TM_OLI_BIT("Pull: Yellow", 0x02),
        TM_OLI_BIT("Pull: Blue", 0x04),
        TM_OLI_BIT("Pull: White", 0x08),
        TM_OLI_BIT("Pull: Purple", 0x10),
        TM_OLI_SCR_CHOICE("Whistle / Lead", TM_OLI_SCR(whistleColor), kOlimarColor, main::kOlimarPikminCount, olimarScratchChanged),
        TM_FUNC("Move Lead Now", olimarMoveLeadPressed),
        TM_OLI_SCR_CHOICE("All Maturity", TM_OLI_SCR(allMaturity), kOlimarMaturity, main::kOlimarMaturityCount, olimarAllMatChanged),
        TM_OLI_SCR_CHOICE("Red Maturity", TM_OLI_SCR(colorMat) + 0, kOlimarMaturity, main::kOlimarMaturityCount, olimarColorMatChanged),
        TM_OLI_SCR_CHOICE("Yellow Maturity", TM_OLI_SCR(colorMat) + 1, kOlimarMaturity, main::kOlimarMaturityCount, olimarColorMatChanged),
        TM_OLI_SCR_CHOICE("Blue Maturity", TM_OLI_SCR(colorMat) + 2, kOlimarMaturity, main::kOlimarMaturityCount, olimarColorMatChanged),
        TM_OLI_SCR_CHOICE("White Maturity", TM_OLI_SCR(colorMat) + 3, kOlimarMaturity, main::kOlimarMaturityCount, olimarColorMatChanged),
        TM_OLI_SCR_CHOICE("Purple Maturity", TM_OLI_SCR(colorMat) + 4, kOlimarMaturity, main::kOlimarMaturityCount, olimarColorMatChanged),
        TM_OLI_SCR_CHOICE("Start Slot 1", TM_OLI_SCR(startSlot) + 0, kOlimarStartSlot, main::kOlimarStartSlotCount, olimarScratchChanged),
        TM_OLI_SCR_CHOICE("Start Slot 2", TM_OLI_SCR(startSlot) + 1, kOlimarStartSlot, main::kOlimarStartSlotCount, olimarScratchChanged),
        TM_OLI_SCR_CHOICE("Start Slot 3", TM_OLI_SCR(startSlot) + 2, kOlimarStartSlot, main::kOlimarStartSlotCount, olimarScratchChanged),
        TM_OLI_SCR_CHOICE("Start Slot 4", TM_OLI_SCR(startSlot) + 3, kOlimarStartSlot, main::kOlimarStartSlotCount, olimarScratchChanged),
        TM_OLI_SCR_CHOICE("Preset D-Up", TM_OLI_SCR(dpadPreset) + 0, kOlimarPresets, main::kOlimarPresetCount, olimarScratchChanged),
        TM_OLI_SCR_CHOICE("Preset D-Down", TM_OLI_SCR(dpadPreset) + 1, kOlimarPresets, main::kOlimarPresetCount, olimarScratchChanged),
        TM_OLI_SCR_CHOICE("Preset D-Left", TM_OLI_SCR(dpadPreset) + 2, kOlimarPresets, main::kOlimarPresetCount, olimarScratchChanged),
        TM_OLI_SCR_CHOICE("Preset D-Right", TM_OLI_SCR(dpadPreset) + 3, kOlimarPresets, main::kOlimarPresetCount, olimarScratchChanged)
    };
#undef TM_OLI_BIT
#undef TM_OLI_SCR_CHOICE

    extern const Page kOlimar = TM_PAGE("Olimar", kRows);
#endif
#undef TM_OLI_SCR
}
}
}
