#pragma once

#include <types.h>

#include "main/cpu/cpu_settings.h"
#include "main/game/match.h"
#include "main/game/status_ids.h"
#include "shared/interface.h"

struct gfPadStatus;

// The lab: the menu (Start in the match, or Z in Training's pause menu per
// Controls > Lab Menu Button), the input it takes from the game, and the
// features its settings drive.
namespace tm {
namespace lab {
    void attach();
    // Puts back anything the lab changed in the game (the stop bit).
    void detach();

    // From the stub's pad hooks (iface::PadPhase).
    void onPads(u32 phase, gfPadStatus* pads);
    // The stub's fighter-pad hook (iface::Callbacks::fighterPad).
    void onFighterPad(u32 port, gfPadStatus* pad);
    // From the fighter-input hook: takes the taunt of a D-pad direction the
    // lab has bound out of the player's controls (DPad Up, DPad Down).
    void onFighterInput(const iface::FighterInput* call);
    // From the render hook, inside nothing: opens its own 2D pass if needed.
    void onRender(u32 call, const game::MatchState& match);
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    bool x_menuOpen();
    void x_applyPercent(int side);
#endif
    // True while the lab menu is open (other displays hide then).
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    inline bool menuOpen() { return x_menuOpen(); }
#else
    bool menuOpen();
#endif
    // True when Frame Advance is active and sideways Wii Remote '-' is bound to
    // Frame Advance or Frame Decrement (gates out sideways - + D-pad chords).
    bool minusBoundToFrameAdvance(const game::MatchState& match);

    // Menu actions.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    inline void applyPercent(int side) { x_applyPercent(side); }
#else
    void applyPercent(int side);
#endif
    void closeMenu();
    void frameAdvanceChanged();
    void osdSettingsChanged();
    // Resets every lab setting back to its default. The row asks twice: the
    // first A arms confirmation until the menu reopens.
    void resetLabSettings();
    void resetAllPressed();
    extern char g_resetLabel[];
    // Main Menu > Exit and every drill's Exit row: closes the menu and ends the
    // match as Training's Finish does, to its character select, without its
    // pause (game::endTrainingMatch). Outside a running Training match it only
    // closes the menu.
    void exitMatch();
    // Main Menu > Training Menu, and Z in the menu outside drills: closes the lab menu into
    // Training's pause menu (opened with Start, the game gets one Start press;
    // with Z, the pause is already open).
    void openTrainingMenu();

    // Savestates: true while the lab is in a transition a save or load must not
    // cut into (menu open, Start during frame advance, the post-pause catch-up,
    // the match ending after Exit); and the lab's part after a load.
    bool savestateBusy();
    void savestateCopying();
    void savestateLoaded();
    // Pre-swap teardown for overlays, OSDs, advantage/technique trackers,
    // info display, and per-player shield hit flags.
    void prepareSwap();

    // Per-frame HMN shield upkeep decision for one fighter (main or partner).
    // Returns true when shield health should be held at cpu::kShieldFull.
    static inline bool stepPlayerShield(u8 mode, int status, bool& hit)
    {
        bool inGuard = status == game::kStatusGuardOn || status == game::kStatusGuard
                       || status == game::kStatusGuardDamage;
        if (!inGuard)
        {
            hit = false;
        }
        else if (status == game::kStatusGuardDamage)
        {
            hit = true;
        }
        return mode == cpu::kShieldsOn || (mode == cpu::kShieldsUntilHit && !hit);
    }
}
}
