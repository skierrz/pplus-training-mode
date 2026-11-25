#include "main/extra.h"
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

    // In main::TimingMeterMode order (main/settings.h).
    static const char* const kTimingMeter[] = {"Off", "All", "L-Cancel", "Wavedash", "Act-Out"};
    TM_STATIC_ASSERT(TM_COUNT(kTimingMeter) == main::kTimingMeterCount, tm_timing_meter_labels_match);

    static bool advToleranceEnabled()
    {
        const main::OsdSettings& o = main::settings().osd;
        return o.frameAdvantage != 0 || o.frameAdvantageOnHit != 0;
    }

    static const Option kRows[] = {
        // Layout & meta controls.
        TM_CHOICE("OSD Position", osd.position, kPosition, osd::kPositionSides, lab::osdSettingsChanged),
        TM_INT("Max OSDs", osd.maxMessages, 1, osd::kPerPlayer, osd::kPerPlayer, "%d", NULL),
        TM_ROW(kKindChoice, 0, TM_FIELD(osd.timingMeter), 0, TM_COUNT(kTimingMeter) - 1, main::kTimingMeterOff,
               "Overhead Timing Bar", kTimingMeter, NULL, main::extraLoaded),
        TM_ROW(kKindToggle, 0, TM_FIELD(cpu.options.techTrackerOsd), 0, 1, 0, "P+ Tech OSD", NULL, NULL,
               main::extraLoaded),
        // Highest-use P+ competitive OSDs.
        TM_TOGGLE("Wavedash", osd.wavedash, 0, NULL),
        TM_TOGGLE("L-Cancel", osd.lCancel, 0, NULL),
        TM_TOGGLE("Ledgedash Info", osd.ledgedashInfo, 0, NULL),
        TM_TOGGLE("Act OoS Frame", osd.actOos, 0, NULL),
        TM_TOGGLE("Frame Advantage", osd.frameAdvantage, 0, NULL),
        TM_TOGGLE("Hit Advantage", osd.frameAdvantageOnHit, 0, NULL),
        TM_ROW(kKindInt, kFlagHideWhenDisabled, TM_FIELD(osd.advantageTolerance), 0, 5, 1, "Advantage Tolerance",
               "%df", lab::osdSettingsChanged, advToleranceEnabled),
        // Remaining technique & defensive OSDs.
        TM_TOGGLE("Dashback", osd.dashback, 0, NULL),
        TM_TOGGLE("Powershield", osd.powershield, 0, NULL),
        TM_TOGGLE("Act OoLag", osd.actOoLag, 0, NULL),
        TM_TOGGLE("Act OoAirborne", osd.actOoAirborne, 0, NULL),
        TM_TOGGLE("Act OoHitstun", osd.actOoHitstun, 0, NULL),
        TM_TOGGLE("Jump Cancel", osd.jumpCancel, 0, NULL),
        TM_TOGGLE("Fastfall Timing", osd.fastfall, 0, NULL),
        TM_TOGGLE("Boost Grab / DACUS", osd.boostGrab, 0, NULL),
        TM_TOGGLE("SDI Inputs", osd.sdi, 0, NULL),
        TM_TOGGLE("Tech Lockout", osd.lockoutTimers, 0, NULL),
        TM_TOGGLE("Combo Counter", osd.comboCounter, 0, NULL),
        TM_TOGGLE("Grab Escape", osd.grabBreakout, 0, NULL),
        TM_TOGGLE("Item Throw", osd.itemThrow, 0, NULL)
    };

    extern const Page kOsdMenu = TM_PAGE("OSDs", kRows);
}
}
}
