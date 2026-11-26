#pragma once

#include <types.h>

#include "main/cpu/cpu_settings.h"
#include "main/game/match.h"
#include "main/game/status_ids.h"
#include "shared/game/stick.h"
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
    static inline bool menuOpen() { return x_menuOpen(); }
#else
    bool menuOpen();
#endif
    // True when Frame Advance is active and sideways Wii Remote '-' is bound to
    // Frame Advance or Frame Decrement (gates out sideways - + D-pad chords).
    bool minusBoundToFrameAdvance(const game::MatchState& match);

    // Menu actions.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    static inline void applyPercent(int side) { x_applyPercent(side); }
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
    void toggleDiStickPicker();
    extern char g_diPickLabel[];
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
    // True when Combo Lab's L + D-Pad CPU % shortcut is held on raw system pads
    // (checked by savestate::onPads before it consumes or hides D-Pad Left/Right).
    bool comboLHeldOnPads(const gfPadStatus* pads);
    // True when Olimar D-Pad Mode (B+D-Pad Pluck or Lineup Presets) owns the
    // D-pad in an active Training match with P1 Olimar (suppresses Savestates,
    // Taunts, and Lab D-pad shortcuts while keeping L+D-Pad CPU % active).
    bool olimarDpadActive(const game::MatchState& m);
    void savestateCopying();
    void savestateLoaded();
    // Pre-swap teardown for overlays, OSDs, advantage/technique trackers,
    // info display, and per-player shield hit flags.
    void prepareSwap();

    // Syncs a menu damage setting from live fighter damage when unlocked.
    // Returns true if updated; leaves locked target percents untouched.
    static inline bool syncUnlockedPercentValue(u16& settingPct, bool locked, float liveDamage)
    {
        if (locked)
        {
            return false;
        }
        int dmg = static_cast<int>(liveDamage);
        if (dmg < 0)
        {
            dmg = 0;
        }
        else if (dmg > 999)
        {
            dmg = 999;
        }
        settingPct = static_cast<u16>(dmg);
        return true;
    }

    // True when openMenu() may sync unlocked P1/CPU percent fields to live match
    // damage. Skips whenever a reset-owning mode (Combo Lab / Combo Trainer,
    // active Scenario preset, or active Skill Drill) owns those fields as
    // starting/reset damage.
    static inline bool shouldSyncUnlockedPercentsOnOpen(u8 comboTrainerEnabled, u8 scenarioPreset, u8 activeDrill)
    {
        return comboTrainerEnabled == 0 && scenarioPreset == 0 && activeDrill == 0;
    }

    // Converts a normalized 2D stick vector into Custom DI [-100, +100] coordinates.
    // When `allowNeutral` is false (non-modal C-Stick), neutral stick preserves the
    // existing aimed angle and returns false.
    static inline bool stickToCustomDi(float sx, float sy, bool allowNeutral, s8& outX, s8& outY)
    {
        float mag2 = sx * sx + sy * sy;
        if (mag2 < 0.15f * 0.15f)
        {
            if (!allowNeutral)
            {
                return false;
            }
            sx = sy = 0.0f;
        }
        else if (mag2 > 1.0f)
        {
            float inv = 1.0f / game::stickRoot(mag2);
            sx *= inv;
            sy *= inv;
        }
        int ix = static_cast<int>(sx >= 0.0f ? (sx * 100.0f + 0.5f) : (sx * 100.0f - 0.5f));
        int iy = static_cast<int>(sy >= 0.0f ? (sy * 100.0f + 0.5f) : (sy * 100.0f - 0.5f));
        if (ix < -100) ix = -100;
        else if (ix > 100) ix = 100;
        if (iy < -100) iy = -100;
        else if (iy > 100) iy = 100;
        outX = static_cast<s8>(ix);
        outY = static_cast<s8>(iy);
        return true;
    }

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
