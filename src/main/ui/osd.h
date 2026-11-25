#pragma once

#include <types.h>

#include "main/gfx/draw.h"
#include "main/ui/osd_place.h"

// Per-player on-screen display (OSD) message stacks. Each message lives
// kLifetime game frames with a shrinking progress strip, glides into place,
// and folds away on expiry; a new message with the same id replaces the old
// one in that player's stack. Static storage only.
namespace tm {
namespace osd {
    enum {
        kPlayers = 4,
        kPerPlayer = 6,
        kMaxLines = 3,
        kLineChars = 32,
        kLifetime = 150, // game frames (2.5 s)
        kAnimFrames = 5
    };

    // Message ids; posting with an existing id replaces that message in place.
    enum Id {
        kIdWavedash = 0,
        kIdLCancel,
        kIdActOos,
        kIdDashback,
        kIdPowershield,
        kIdSdi,
        kIdLockout,
        kIdItemThrow,
        kIdBoostGrab,
        kIdMiscellaneous,
        kIdActOoLag,
        kIdActOoAirborne,
        kIdJumpCancel,
        kIdFastfall,
        kIdFrameAdvantage,
        kIdCombo,
        kIdGrabBreakout,
        kIdCliffwait,
        kIdActOoHitstun,
        kIdFrameAdvantageHit,
        kIdGalint,
        kIdSavestate,
        kIdPPlusTech
    };

    // Neutral P+ status colours: neutral, positive, negative, caution.
    static const gfx::Rgba kWhite = 0xF1F5F9FF;
    static const gfx::Rgba kGreen = 0x4ADE80FF;
    static const gfx::Rgba kRed = 0xFF948AFF;
    static const gfx::Rgba kYellow = 0xFACC15FF;

    // Sign-based frame advantage colour: positive green, zero neutral, negative red.
    inline gfx::Rgba frameAdvantageColor(int value)
    {
        return value > 0 ? kGreen : (value < 0 ? kRed : kWhite);
    }

    // Wavedash angle bands derived from P+'s shallowest downward airdodge
    // (stick (96, -28) -> 16.26 deg after P+'s radius-100 cap, 0.010325 scale,
    // and 0.28 dead zone at 0x8056EDB8 / 0x800481A0): 16.2..22.0 deg green
    // (long), 22.0..30.0 deg yellow (mid), else neutral white.
    inline gfx::Rgba wavedashAngleColor(float angle)
    {
        if (angle < 16.2f || angle > 30.0f)
        {
            return kWhite;
        }
        return angle < 22.0f ? kGreen : kYellow;
    }

    struct Message;

    void reset();

    // An osd::Position (osd_place.h); any other value falls back to HUD.
    void setPosition(int position);

    // Posts a message for `player` (HUD slot 0-3), human or CPU. Splits text
    // on '\n' (at most kMaxLines of kLineChars), each line starting in `color`.
    Message* post(int player, int id, gfx::Rgba color, const char* format, ...);
    void lineColor(Message* message, int line, gfx::Rgba color);

    // True if any active message will be drawn on this frame.
    bool visible();
    // Inside a gfx::begin()/end() pass.
    void draw();
}
}
