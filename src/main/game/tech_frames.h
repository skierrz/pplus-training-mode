#pragma once

#include <types.h>

// Project+'s first distinguishable frame of a tech per fighter kind (ftKind),
// generated from P+ 3.2.1's pf/fighter by scripts/tech_frames.py. 1-indexed
// (1 = the tech's first frame); kTechDistinguishableMissing (-1) when no value
// was derived.
namespace tm {
namespace game {
    enum {
        kTechDistinguishableCount = 0x32,
        kTechDistinguishableMissing = -1,
        // P+ ground tech animations reveal roll direction well before the end
        // of their 26-frame neutral tech window; hiding ends before frame 21.
        kTechInvisEndFrame = 21,
        // ProjectPunch invis_techs.cpp:50-52 (common.h:153): get-ups hidden on
        // 0-indexed subactionFrame strictly between 5 and 20.
        kGetupInvisBaseFrame = 5,
        kGetupInvisEndFrame = 20
    };

    // First distinguishable frame (1-indexed) for `kind` (ftKind), or
    // kTechDistinguishableMissing (-1) when out of range or not derived.
    int techDistinguishable(int kind);

    // Tech invisibility hide window: `stateFrame` is the 1-indexed frame of
    // the animation being shown (1 on the first), `dist` is
    // techDistinguishable(kind), and `delay` is Tech Invisibility Delay (0-15).
    inline bool techInvisFrame(int stateFrame, int dist, int delay)
    {
        return dist != kTechDistinguishableMissing && stateFrame > dist + delay && stateFrame < kTechInvisEndFrame;
    }

    // ProjectPunch's get-up invisibility rule (invis_techs.cpp:50-52,
    // common.h:153): `subactionFrame` is 0-indexed (soMotionModule::getFrame()),
    // hidden when 5 + delay < subactionFrame < 20.
    inline bool getupInvisFrame(int subactionFrame, int delay)
    {
        return subactionFrame > kGetupInvisBaseFrame + delay && subactionFrame < kGetupInvisEndFrame;
    }

    // Tech Sound cue: fires on the distinguishable frame itself, without Tech
    // Invisibility Delay.
    inline bool techSoundFrame(int stateFrame, int dist)
    {
        return dist != kTechDistinguishableMissing && stateFrame == dist;
    }
}
}
