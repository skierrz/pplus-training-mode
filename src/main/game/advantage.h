#pragma once

#include <types.h>

// Frame advantage of an attack that connected, on hit or on shield: the
// frame the defender could act minus the frame the attacker could act, both
// as the trackers saw them. Positive means the attacker acts first.
namespace tm {
namespace game {
    struct Advantage {
        s8 attacker;        // tracker index
        s8 defender;        // tracker index
        s8 attackerPlayer;  // HUD slot 0-3
        s8 defenderPlayer;
        u8 shield;          // 1: on shield, 0: on hit
        u8 hits;            // hits in the exchange (multi-hit moves)
        u8 how;             // HitAttribution of the last hit
        u8 hidden;          // 1: past the display limit (logged, not shown)
        s16 value;
        s32 hitFrame;       // tracker frame of the last hit
        s32 attackerFrame;  // first frame the attacker could act
        s32 defenderFrame;  // first frame the defender could act
        s16 attackerStatus; // statuses at those frames, and the ones before
        s16 attackerPrev;
        s16 defenderStatus;
        s16 defenderPrev;
    };

    typedef void (*AdvantageSink)(const Advantage& result);

    // Clears every open exchange; `sink` receives each result, those past
    // the display limit too (`hidden`).
    void advantageReset(AdvantageSink sink);
    // The result is final once the defender has stayed actionable for more
    // than this many frames (ProjectPunch's shield pressure gap tolerance):
    // a new hit within it continues the same pressure.
    void advantageSetTolerance(int frames);
    // Once per finished tracker frame (the tracker's frame listener).
    void advantageFrame();
    // The last result, or NULL before the first.
    const Advantage* lastAdvantage();
}
}
