#pragma once

#include <types.h>

#include "main/game/tracker.h"

// Colour overlays (HMN / CPU Color Overlays) and fighter hiding (the overlays'
// Invisible choice, tech invisibility, and Display Options > Character Models;
// a fighter is hidden while any of them asks for it, and shown again only when
// none does), applied through the game's colour-blend and visibility modules.
// Everything set is remembered per fighter and restored when it no longer
// applies, when the match pauses, and when the module detaches.
namespace tm {
namespace game {
    // Choices of an overlay group, in the menu's order.
    enum OverlayChoice {
        kOverlayNone = 0,
        kOverlayRed,
        kOverlayGreen,
        kOverlayBlue,
        kOverlayYellow,
        kOverlayWhite,
        kOverlayBlack,
        kOverlayCyan,
        kOverlayOrange,
        kOverlayRemove,
        kOverlayCollision, // the fighter's hitboxes and hurtboxes drawn, no colour
        kOverlayInvisible,
        kOverlaySound,     // a sound as the group starts to apply, no colour
        kOverlayChoiceCount
    };

    // State groups, in the menu's order (the last that applies wins).
    enum OverlayGroup {
        kGroupActionable,
        kGroupHitstun,
        kGroupInvincible,
        kGroupLedgeActionable,
        kGroupMissedLCancel,
        kGroupCanFastfall,
        kGroupAutocancel,
        kGroupCrouch,
        kGroupWait,
        kGroupWalk,
        kGroupDash,
        kGroupRun,
        kGroupJumps, // not built yet
        kGroupFullHop,
        kGroupShortHop,
        kGroupIasa,
        kGroupShieldStun,
        kOverlayGroupCount
    };

    struct OverlayConfig {
        const u8* groups[2]; // per side (human, CPU): OverlayChoice per group
        u8 techInvis;        // main::TechInvisibility
        u8 techDelay;        // delay in frames after the distinguishable frame, 0-15
        u8 hideModels;       // bit p: player p's model hidden (Display Options)
        bool quiet;          // no Play Sound (the lab replaying frames on its own)
    };

    // The group that applies to a fighter: among active groups (bit g of
    // `active`) with a choice other than None, the last in menu order; -1 if
    // none.
    inline int overlayGroup(const u8* groups, u32 active)
    {
        for (int g = kOverlayGroupCount - 1; g >= 0; g--)
        {
            int c = groups[g];
            if (c != kOverlayNone && c < kOverlayChoiceCount && (active & (1u << g)) != 0)
            {
                return g;
            }
        }
        return -1;
    }

    // Whether Play Sound fires on this step: the active group is set to Play
    // Sound and differs from the group that applied on the previous step.
    inline bool overlaySoundDue(int before, int group, const u8* groups)
    {
        return group >= 0 && group != before && groups[group] == kOverlaySound;
    }

    void overlaysAttach();
    void overlaysDetach();
    // Once per game step, after the trackers and techniques took the frame
    // that ended. `live`: the match runs unpaused; otherwise everything set
    // is put back.
    void overlaysApply(bool live, const OverlayConfig& config);
    // From Fighter::processFixCamera (stub cursor hook), after the game's own
    // setCursor / setNameCursor calls: keeps both off while we hide `fighter`.
    void overlaysFixCamera(void* fighter);
    void overlaysClear();

    // Whether the fighter's overlay on the last step was Show Collision (its
    // hitboxes and hurtboxes are then drawn whatever Display Options >
    // Hitboxes says; display.cpp). False while the match is paused.
    bool overlayShowsCollision(const Fighter* fighter);
    // Whether the tech invisibility applies to the tracked fighter's
    // current animation (shared with the tests through the log).
    bool techHidden(int tracked, const OverlayConfig& config);
}
}
