#pragma once

#include <types.h>

#include "extra/timing_meter_core.h"
#include "main/game/match.h"

// Overhead 3D world-space Timing Helper Meter (L-Cancel, Wavedash, Act-Out)
// anchored above each fighter's ECB top, stepping above Combo Trainer meters.
namespace tm {
namespace extra {
    void timingMeterAttach();
    void timingMeterDetach();
    void timingMeterReset();
    void timingMeterFrame();
    void timingMeterDraw(const game::MatchState& match);
}
}
