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

    static const Option kRows[] = {
        TM_CHOICE("OSD Background", osd.bgColor, kOsdBgLabels, main::kOsdBgTeal, NULL),
        TM_CHOICE("OSD Opacity", osd.bgAlpha, kOsdAlphaLabels, main::kDefaultOsdAlpha, NULL)
    };

    extern const Page kTheme = TM_PAGE("Theme", kRows);
}
}
}
