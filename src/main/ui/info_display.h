#pragma once

// Per-fighter info panels (P1 / CPU Info Display): stacked at the top left,
// outlined by player port accent colour, up to eight configurable rows in
// three text sizes.
namespace tm {
namespace ui {
    // Values selectable per row (all clean Brawl/P+ and ProjectPunch readers).
    enum InfoValue {
        kInfoNone = 0,
        kInfoPosition,
        kInfoStateName,
        kInfoStateFrame,
        kInfoVelocitySelf,
        kInfoVelocityKb,
        kInfoVelocityTotal,
        kInfoEngineLStick,
        kInfoSystemLStick,
        kInfoEngineCStick,
        kInfoSystemCStick,
        kInfoEngineTrigger,
        kInfoSystemTrigger,
        kInfoLedgegrabTimer,
        kInfoIntangibility,
        kInfoHitlag,
        kInfoHitstun,
        kInfoShieldHealth,
        kInfoShieldStun,
        kInfoGripStrength,
        kInfoEcbLock,
        kInfoEcbBottom,
        kInfoJumps,
        kInfoWalljumps,
        kInfoCanWalljump,
        kInfoJabCounter,
        kInfoLineInfo,
        kInfoBlastzoneLeftRight,
        kInfoBlastzoneUpDown,
        kInfoDamage,       // ours
        kInfoSubaction,    // ProjectPunch
        kInfoCancelFlags,  // ProjectPunch
        kInfoActionable,   // ours
        kInfoAirborne,     // ours
        kInfoLastHit,      // ours
        kInfoActIn,        // ours: frames until actionable, from the move's scripts (extra module)
        kInfoWallCling,    // P+ wall cling / Knuckles wall climb
        kInfoValueCount
    };

    enum InfoPreset {
        kInfoPresetNone = 0,
        kInfoPresetState,
        kInfoPresetLedge,
        kInfoPresetDamage,
        kInfoPresetDebug, // ProjectPunch's fighter-state text
        kInfoPresetCount
    };

    // From the game-step pad hook, before the step runs. `previousRan`: the
    // step before was simulated (neither frozen nor stopped by Training's
    // pause); the status-frame counts advance only then.
    void infoStep(bool previousRan);

    // True if either panel has a row to show.
    bool infoWanted();
    // Draws the panels (inside a 2D pass).
    void drawInfo();

    // Display Preset: overwrites the side's rows.
    void applyInfoPreset(int side);

    // The menu's label for a value (pages/info_display.cpp), without the
    // not-built marker, and whether the value is built.
    const char* infoValueLabel(int value);
    bool infoValueAvailable(int value);

    // Frames the human (side 0) or CPU (side 1) has been in its status.
    int statusFrames(int side);
    // Restarts the status-frame counts (after a savestate load).
    void infoReset();
}
}
