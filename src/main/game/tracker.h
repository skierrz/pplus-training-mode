#pragma once

#include <types.h>

class Fighter;

// Per-fighter state over game frames, fed by the game's own fighter event
// notifications (status changes, animation commands) and one update per game
// step. Every fighter of the match is tracked, Ice Climbers' partner included,
// in static storage.
namespace tm {
namespace game {
    enum {
        kMaxTracked = 8, // four entries, each with a partner
        kNoFrame = -1,
        kMaxEvents = 8   // status changes kept per fighter per frame
    };

    // One status change as the game reported it, with values read at that
    // moment. Several can happen in one game frame.
    struct StatusEvent {
        s16 status;
        s16 prev;
        s16 motionKind;  // subaction when the change is reported: still the
        s16 motionFrame; // previous status's (observed), and its frame
        u32 flags;       // status work flag word 0 (bit 6: short hop)
        s16 lCancel;     // P+'s L-cancel counter (LA int 90)
        u8 airborne;
        u8 order;        // 0 = the frame's first change
    };

    // One game frame of one fighter, as the frame ended unless noted.
    struct FighterFrame {
        s32 frame;      // tracker frame number
        s16 status;
        s16 prevStatus; // status before the last change (kept across frames)
        s16 hitlag;     // hitlag frames left
        s16 hitstun;    // hitstun frames left
        s16 shieldStun; // shield stun frames left (GuardDamage only)
        u8 statusChanges; // status events during the frame
        u8 airborne;
        u32 groups;        // bit g: interrupt group g enabled
        u32 groupsEnabled; // groups an Enable Interrupt Group command turned on during the frame
        u8 cancel;      // the move's cancel window was opened (Enable Cancel)
        u8 actionable;  // could act at some point during the frame (see tracker.cpp)
        u8 attackActive; // the fighter's attack hit something during the frame
        u8 hitReceived;    // hit during the frame (see hits)
        u8 shieldReceived; // shield stun was given (or renewed) during the frame
        u8 hits;           // hits taken during the frame: changes into a damage status
        u8 reserved[2];
    };

    // How a hit's attacker was found.
    enum HitAttribution {
        kByNobody = 0,
        kByInflict = 1,    // its attack connected that frame
        kByElimination = 2 // the defender's only opponent
    };

    struct HitRecord {
        s8 other;   // tracker index of the other fighter, -1 if unknown
        u8 shield;  // 1 if it was a hit on shield
        u8 how;     // HitAttribution
        u8 reserved;
        s32 frame;  // tracker frame, kNoFrame if none yet
    };

    struct Tracked {
        bool used;
        bool partner;     // Ice Climbers' second climber
        bool cpu;
        s8 playerNo;      // HUD slot 0-3
        int entryId;
        Fighter* fighter; // the instance the observers are on
        u32 taskId;
        FighterFrame cur;  // the last finished frame
        FighterFrame prev; // the one before it
        s32 statusStart;     // frame the current status began
        s32 actionableSince; // first frame of the current actionable stretch, or kNoFrame
        s32 busySince;       // first frame of the current non-actionable stretch, or kNoFrame
        s32 groupSince[32];  // per interrupt group: frame it became enabled, kNoFrame while off
        HitRecord lastDealt;
        HitRecord lastReceived;
        // cur's status changes in order; past kMaxEvents only counted.
        u8 eventCount;
        u8 eventsLost;
        StatusEvent events[kMaxEvents];
    };

    void trackerAttach();
    // Takes every observer off its fighter; must run while the fighters exist.
    void trackerDetach();

    // A simulated step's frame is handed over (finished, then the frame
    // listener) exactly once: at the first of trackerHandOver (the render
    // callback) and trackerStep (the next step's pad hook, for a frame no
    // picture followed). Why, and the main loop around it:
    // docs/internals.md, "Frame hand-over".
    void trackerHandOver();

    // From the game-step pad hook, before the step's tasks run: hands over
    // the step before's frame if no render callback did, then puts the
    // observers on only while `live` (running, not paused, not ending).
    void trackerStep(bool live);

    // From the same pad hook, once the lab knows whether the step about to
    // run is simulated: only such a step's frame is handed over.
    void trackerStepRuns(bool runs);

    // A step ran whose frame has not been handed over yet (so the picture
    // on screen is newer than what the frame listener has seen). False in
    // the render callback after trackerHandOver; true there only if the
    // render path was entered without it.
    bool trackerFramePending();
    // A frame was handed over in a render callback and is not counted in
    // trackerFrame yet (it is at the next pad hook or hand-over).
    bool trackerFrameOwed();

    // Savestates: every observer off before memory is copied, back on
    // after; `forget` drops the fighters' history, as after a load. How a
    // load is handed over: docs/internals.md, "Frame hand-over".
    void trackerSuspend();
    void trackerResume(bool forget);

#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    s32 x_trackerFrame();
    const Tracked* x_tracked(int i);
#endif

    // Frames finished since the match module was attached; in the frame
    // listener, the frame being handed over. Reads the same wherever the
    // frame was handed over (docs/internals.md, "Frame hand-over").
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    static inline s32 trackerFrame() { return x_trackerFrame(); }
    int trackedCount();
    static inline const Tracked* tracked(int i) { return x_tracked(i); }
#else
    s32 trackerFrame();
    int trackedCount();
    // Slot i (0..kMaxTracked-1); NULL if unused.
    const Tracked* tracked(int i);
#endif
    // The main (non-partner) fighter of an entry, or NULL.
    const Tracked* trackedEntry(int entryId);
    int trackedIndex(const Tracked* t);
    // Whether the observers took for every tracked fighter (for the log/test).
    bool trackerHealthy();

    // Called once per finished frame, after every fighter's frame is final.
    typedef void (*FrameListener)();
    void setFrameListener(FrameListener listener);

    // Called from inside the game's frame at each status change, after it
    // was recorded, for what must act before that frame is drawn. It must
    // not change the trackers.
    typedef void (*StatusListener)(int tracked, const StatusEvent& e);
    void setStatusListener(StatusListener listener);

    // Writing 1 here (the harness does) logs every tracked fighter each frame.
    extern u8 g_trackerTrace;
}
}
