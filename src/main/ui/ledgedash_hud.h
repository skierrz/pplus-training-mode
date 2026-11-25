#pragma once

// Ledgedash HUD: ProjectPunch's ledgedash frame bar rendered as a compact
// top-centre bar with a summary strip showing GALINT, ledge frames, and
// airdodge angle. Stays visible for 300 frames after each ledgedash.
namespace tm {
namespace ui {
    bool ledgedashHudVisible();
    // The most cells its bar shows (Max Frames, 30 to 64).
    int ledgedashCells();
    // Inside a gfx::begin()/end() pass.
    void drawLedgedashHud();
}
}
