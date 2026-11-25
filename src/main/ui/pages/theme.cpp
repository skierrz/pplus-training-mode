#include "main/settings.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    // In main::OsdBgColor order (main/settings.h).
    static const char* const kOsdBgLabels[] = {
        "P+ Teal", "Dark Slate", "Brawl Navy", "Charcoal", "Black", "Crimson", "Violet"
    };
    TM_STATIC_ASSERT(TM_COUNT(kOsdBgLabels) == main::kOsdBgColorCount, tm_osd_bg_labels_match);

    // Opacity steps 0..10 (0%..100% in 10% increments).
    static const char* const kOsdAlphaLabels[] = {
        "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"
    };
    TM_STATIC_ASSERT(TM_COUNT(kOsdAlphaLabels) == main::kOsdAlphaMax + 1, tm_osd_alpha_labels_match);

    // In main::MenuAccent order (main/settings.h).
    static const char* const kMenuAccentLabels[] = {
        "P+ Teal", "Brawl Navy", "Subspace Violet", "Crimson Steel"
    };
    TM_STATIC_ASSERT(TM_COUNT(kMenuAccentLabels) == main::kMenuAccentCount, tm_menu_accent_labels_match);

    // In main::InfoBgColor order (main/settings.h).
    static const char* const kInfoBgLabels[] = {
        "Default", "P+ Teal", "Brawl Navy", "Charcoal", "Black", "Violet", "Crimson"
    };
    TM_STATIC_ASSERT(TM_COUNT(kInfoBgLabels) == main::kInfoBgColorCount, tm_info_bg_labels_match);

    // In main::InfoAlpha order (main/settings.h).
    static const char* const kInfoAlphaLabels[] = {
        "Default", "0%", "20%", "40%", "60%", "80%", "100%"
    };
    TM_STATIC_ASSERT(TM_COUNT(kInfoAlphaLabels) == main::kInfoAlphaCount, tm_info_alpha_labels_match);

    static const Option kRows[] = {
        TM_CHOICE("OSD Background", osd.bgColor, kOsdBgLabels, main::kOsdBgTeal, NULL),
        TM_CHOICE("OSD Opacity", osd.bgAlpha, kOsdAlphaLabels, main::kDefaultOsdAlpha, NULL),
        TM_CHOICE("Menu Accent", osd.menuAccent, kMenuAccentLabels, main::kMenuAccentTeal, NULL),
        TM_CHOICE("Info Background", osd.infoBgColor, kInfoBgLabels, main::kInfoBgDefault, NULL),
        TM_CHOICE("Info Opacity", osd.infoBgAlpha, kInfoAlphaLabels, main::kInfoAlphaDefault, NULL)
    };

    extern const Page kTheme = TM_PAGE("Theme", kRows);
}
}
}
