#pragma once

#include <types.h>

#include "main/game/fighters.h"
#include "main/game/match.h"

struct gfPadStatus;

// Display Options (HUD, hitboxes, character models, environment collision,
// stage model, effects) and the General rows Camera Mode, Game Speed, and
// Move Staling. Every game change is put back when its option goes off, while
// Training's pause menu needs it, and at detach.
namespace tm {
namespace display {
    // Whose fighters an option applies to, in the menu's order.
    enum Who {
        kWhoNone = 0,
        kWhoHuman = 1,
        kWhoCpu = 2,
        kWhoBoth = 3
    };

    enum CameraMode {
        kCameraNormal = 0,
        kCameraZoom,
        kCameraFixed,
        kCameraModeCount
    };

    // Game speed steps.
    enum Speed {
        kSpeed1 = 0,
        kSpeed5of6,
        kSpeed2of3,
        kSpeed1of2,
        kSpeed1of4,
        kSpeedCount
    };

    void attach();
    // Puts back everything changed in the game; while the fighters exist.
    void detach();

    // The game pads as the players hold them, before the lab hides anything
    // (from the lab's game-pad callback, once per game step).
    void capturePads(const gfPadStatus* pads);
    // The system pads, once per main-loop pass, before the lab hides anything
    // (below the normal frame rate a press stays marked as pressed until a
    // game step has run: display.cpp).
    void systemPads(gfPadStatus* pads);

    // Once per game step, before its tasks run (staling, speed, camera).
    void step(const game::MatchState& m, bool menuOpen);

    // From the render hook in a match, before the 2D pass: scene layers, then
    // hitboxes, collision and the DI Display in the main camera.
    void render3d(const game::MatchState& m);
    // Inside the 2D pass (gfx::begin()/end()).
    void render2d(const game::MatchState& m, bool menuOpen);

    // Whether the fighter's player is a CPU in the match setup. A CPU with its
    // AI off (Training's Control, the lab's CPU control) is operated like a
    // human (ftManager's operation type), but keeps the CPU's options.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    bool x_cpuPlayer(const game::FighterRef& ref);
    void* x_cameraController();
#ifndef TM_EXTRA_THUNK_IMPL
    static inline bool cpuPlayer(const game::FighterRef& ref) { return x_cpuPlayer(ref); }
    static inline void* cameraController() { return x_cameraController(); }
#else
    bool cpuPlayer(const game::FighterRef& ref);
    void* cameraController();
#endif
#else
    bool cpuPlayer(const game::FighterRef& ref);

    // The game's CameraController (its blast zones at +0x158: left, right,
    // top, bottom), or NULL if its code was not as expected.
    void* cameraController();
#endif

    // HUD on: the lab's watermark is drawn.
    bool hudShown();
    // Bit p set: player p's model is hidden (Character Models).
    u8 hiddenModels();

    // After a savestate load: the scene's layer bits are the state's, so
    // what this module hid is read back from them.
    void savestateLoaded();
}
}
