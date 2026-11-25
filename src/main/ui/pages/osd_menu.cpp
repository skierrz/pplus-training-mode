#include "main/lab.h"
#include "main/ui/osd.h"
#include "main/ui/osd_place.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    // In osd::Position order (main/ui/osd_place.h).
    static const char* const kPosition[] = {"HUD", "Sides", "Top Left", "Top Right"};
    TM_STATIC_ASSERT(TM_COUNT(kPosition) == osd::kPositionCount, tm_osd_positions_match);

    static const Option kRows[] = {
        TM_CHOICE("OSD Position", osd.position, kPosition, osd::kPositionSides, lab::osdSettingsChanged),
        TM_TOGGLE("Wavedash", osd.wavedash, 0, NULL),
        TM_TOGGLE("L-Cancel", osd.lCancel, 0, NULL),
        TM_TOGGLE("Act OoS Frame", osd.actOos, 0, NULL),
        TM_TOGGLE("Dashback", osd.dashback, 0, NULL),
        TM_TOGGLE("Powershield", osd.powershield, 0, NULL),
        TM_TOGGLE("SDI Inputs", osd.sdi, 0, NULL),
        TM_TOGGLE("Tech Lockout", osd.lockoutTimers, 0, NULL),
        TM_TOGGLE("Item Throw", osd.itemThrow, 0, NULL),
        TM_TOGGLE("Boost Grab / DACUS", osd.boostGrab, 0, NULL),
        TM_TOGGLE("Act OoLag", osd.actOoLag, 0, NULL),
        TM_TOGGLE("Act OoAirborne", osd.actOoAirborne, 0, NULL),
        TM_TOGGLE("Jump Cancel", osd.jumpCancel, 0, NULL),
        TM_TOGGLE("Fastfall Timing", osd.fastfall, 0, NULL),
        TM_TOGGLE("Frame Advantage", osd.frameAdvantage, 0, NULL),
        TM_TOGGLE("Combo Counter", osd.comboCounter, 0, NULL),
        TM_TOGGLE("Grab Escape", osd.grabBreakout, 0, NULL),
        TM_TOGGLE("Ledgedash Info", osd.ledgedashInfo, 0, NULL),
        TM_TOGGLE("Act OoHitstun", osd.actOoHitstun, 0, NULL),
        TM_TOGGLE("Hit Advantage", osd.frameAdvantageOnHit, 0, NULL),
        TM_INT("Advantage Tolerance", osd.advantageTolerance, 0, 5, 1, "%d Frames", lab::osdSettingsChanged),
        TM_INT("Max OSDs", osd.maxMessages, 1, osd::kPerPlayer, osd::kPerPlayer, "%d", NULL)
    };

    extern const Page kOsdMenu = TM_PAGE("OSDs", kRows);
}
}
}
