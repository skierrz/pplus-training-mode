#pragma once

// Neutral P+ recording/playback status bar drawn at the bottom centre above
// Brawl's damage HUD: shows current frame, progress track with marker during
// playback, and mode tag (REC / Armed / Ctrl) while recording or controlling CPU.
namespace tm {
namespace record {
    enum HudMode {
        kHudHidden,
        kHudRecord,
        kHudArmed,
        kHudPlayback,
        kHudControl
    };

    struct HudInfo {
        int mode;            // HudMode
        int frame;           // frames since positions were restored
        int end;             // playback length (0: nothing to play)
        bool random;         // Random slot: length shown as "?", no marker
        const char* message; // transient status/refusal message, or NULL
    };

    // Inside a gfx::begin()/end() pass.
    void hudDraw(const HudInfo& info);
}
}
