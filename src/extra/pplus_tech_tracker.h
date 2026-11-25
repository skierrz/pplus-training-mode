#pragma once

#include "extra/pplus_tech_tracker_core.h"

// Live runtime adapter in the extra module for the 44-fighter P+ signature
// tech tracker and its "TMTT" RAM telemetry block + optional OSD callouts.
namespace tm {
namespace extra {
    void techTrackerAttach();
    void techTrackerDetach();
    void techTrackerReset();
    void techTrackerFrame();
    const TechTrackerBlock* techTrackerBlock();
}
}
