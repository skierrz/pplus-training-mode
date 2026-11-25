#pragma once

// Clean-room native Brawl ef_common particle sparkle cues for Frame-1 Perfect
// Wavedash and clean Shorthop (controlled by main::settings().osd.techSparkle).
namespace tm {
namespace extra {
    void techSparkleAttach();
    void techSparkleDetach();
    void techSparkleFrame();
    void techSparkleReset();
}
}
