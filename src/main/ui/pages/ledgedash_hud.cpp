#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    // ProjectPunch's ledgedash frame bar settings (range 30..64, default 50).
    static const Option kRows[] = {
        TM_TOGGLE("Enable", general.ledgeHud, 0, NULL),
        TM_INT("Max Frames", general.ledgeMaxFrames, 30, 64, 50, "%d", NULL),
        TM_TOGGLE("Show GALINT", general.ledgeGalint, 1, NULL),
        TM_TOGGLE("Show Summary", general.ledgePanel, 1, NULL)
    };

    extern const Page kLedgedashHud = TM_PAGE("Ledgedash HUD", kRows);
}
}
}
