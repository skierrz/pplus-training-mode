#include "main/settings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    // In main::OsdBgColor order (main/settings.h).
    TM_LABELS_BEGIN(kOsdBgLabels)
        TM_L("P+ Teal") TM_L("Dark Slate") TM_L("Brawl Navy") TM_L("Charcoal") TM_L("Black") TM_L("Crimson") TM_L("Violet")
    TM_LABELS_END
    TM_LABELS_ASSERT(kOsdBgLabels, main::kOsdBgColorCount, tm_osd_bg_labels_match);

    // Opacity steps 0..10 (0%..100% in 10% increments).
    TM_LABELS_BEGIN(kOsdAlphaLabels)
        TM_L("0%") TM_L("10%") TM_L("20%") TM_L("30%") TM_L("40%") TM_L("50%") TM_L("60%") TM_L("70%") TM_L("80%") TM_L("90%") TM_L("100%")
    TM_LABELS_END
    TM_LABELS_ASSERT(kOsdAlphaLabels, main::kOsdAlphaMax + 1, tm_osd_alpha_labels_match);

    // In main::MenuAccent order (main/settings.h).
    TM_LABELS_BEGIN(kMenuAccentLabels)
        TM_L("P+ Teal") TM_L("Brawl Navy") TM_L("Subspace Violet") TM_L("Crimson Steel")
    TM_LABELS_END
    TM_LABELS_ASSERT(kMenuAccentLabels, main::kMenuAccentCount, tm_menu_accent_labels_match);

    // In main::InfoBgColor order (main/settings.h).
    TM_LABELS_BEGIN(kInfoBgLabels)
        TM_L("Default") TM_L("P+ Teal") TM_L("Brawl Navy") TM_L("Charcoal") TM_L("Black") TM_L("Violet") TM_L("Crimson")
    TM_LABELS_END
    TM_LABELS_ASSERT(kInfoBgLabels, main::kInfoBgColorCount, tm_info_bg_labels_match);

    // In main::InfoAlpha order (main/settings.h).
    TM_LABELS_BEGIN(kInfoAlphaLabels)
        TM_L("Default") TM_L("0%") TM_L("20%") TM_L("40%") TM_L("60%") TM_L("80%") TM_L("100%")
    TM_LABELS_END
    TM_LABELS_ASSERT(kInfoAlphaLabels, main::kInfoAlphaCount, tm_info_alpha_labels_match);

    static const Option kRows[] = {
        TM_CHOICE_P("OSD Background", osd.bgColor, kOsdBgLabels, main::kOsdBgColorCount, main::kOsdBgTeal, NULL),
        TM_CHOICE_P("OSD Opacity", osd.bgAlpha, kOsdAlphaLabels, main::kOsdAlphaMax + 1, main::kDefaultOsdAlpha, NULL),
        TM_CHOICE_P("Menu Accent", osd.menuAccent, kMenuAccentLabels, main::kMenuAccentCount, main::kMenuAccentTeal, NULL),
        TM_CHOICE_P("Info Background", osd.infoBgColor, kInfoBgLabels, main::kInfoBgColorCount, main::kInfoBgDefault, NULL),
        TM_CHOICE_P("Info Opacity", osd.infoBgAlpha, kInfoAlphaLabels, main::kInfoAlphaCount, main::kInfoAlphaDefault, NULL)
    };

    extern const Page kTheme = TM_PAGE("Theme", kRows);
}
}
}
