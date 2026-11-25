#pragma once

#include <types.h>

#include "main/game/advantage.h"
#include "main/game/match.h"

class Fighter;

// The match module's side of the extra module (kind "extra", src/extra/).
// The extra module is linked against this one and calls it directly; this
// module never names anything in it, and reaches it only through the hooks
// it registers here.
namespace tm {
namespace cpu {
    struct Owner;
    struct View;
    struct Frame;
}
namespace main {
    struct Settings;

    // Called once per rendered frame, after the match module has drawn.
    typedef void (*ExtraRender)(u32 renderCall, const game::MatchState& match);

    // How a fighter becomes able to act (the frame data service).
    enum ExtraActHow {
        kActNone = 0,
        kActOpen,   // it can act now
        kActScript, // its subaction script opens the interrupts
        kActEnd,    // its animation ends in a status it is free in
        kActRule,   // its status's own count (landing, shield release)
        kActStun    // hitstun or shield stun runs out
    };

    // A controller as the lab menu reads it (lab.cpp), before anything is
    // hidden from the game: buttons held and newly held (gfPadButtons), and
    // both sticks in pad units.
    struct MenuPad {
        u32 held;
        u32 pressed;
        s8 stickX, stickY;
        u8 rAnalog;
        s8 cStickX, cStickY;
        u8 controllerType; // gfPadType::PadType
        bool connected;
    };

    struct ExtraHooks {
        ExtraRender render;
        // Training lab tools that live in the extra module (extra/lab_tools.h).
        bool (*alterRow)();
        void (*alterEdited)();
        // The DI Display (extra/di_display.h).
        void (*diDraw)(const game::MatchState& match);
        void (*diReset)();
        void (*diHideDrawDi)(bool hide);
        // The frame data service (extra/frame_data.h).
        int (*actIn)(Fighter* fighter, int* how);
        void (*frameData)();
        bool (*advantageMeasured)(const game::Advantage& r);
        // The Character RNG page (extra/character_rng.h): a CharacterRngCall.
        bool (*characterRng)(int call);
        // The Stage Options page (extra/stage_options.h): a StageOptionsCall.
        bool (*stageOptions)(int call);
        // PPlusAI offstage recovery engine (extra/pplus_recovery.h).
        bool (*pplusRecovery)(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons);
        void (*pplusRecoveryReset)(int slot);
        // Combo Trainer backend (extra/combo_trainer.h).
        void (*comboDraw)(const game::MatchState& match);
        void (*comboReset)();
        // Hitbox Heatmap (extra/hitbox_heatmap.h).
        void (*heatmapDraw)(const game::MatchState& match);
        void (*heatmapReset)();
        // Adaptive CPU backend (extra/adaptive_cpu.h).
        int (*adaptiveBias)(int cpuPlayerNo, int list, bool inCounter, float opponentDist, bool commit);
        void (*adaptiveReset)();
        // Unified P+ Lab Scenario Presets (extra/scenario_presets.h).
        void (*scenarioCall)(int call);
        bool (*scenarioCpuStep)(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons);
        void (*scenarioSavedView)(Settings& out);
        // Grab / Tether / Item-Catch Visualizer (extra/grab_visualizer.h).
        void (*grabVizDraw)(const game::MatchState& match);
        // Mid-session Fighter Swap (extra/fighter_swap.h).
        bool (*fighterSwapRow)();
        void (*fighterSwapChanged)();
        void (*fighterSwapApply)();
        bool (*fighterSwapBusy)();
        // Single-line option descriptions (extra/menu_descriptions.h).
        const char* (*optionDesc)(u8 pageId, u8 row);
        // Native Brawl/P+ UI button emblems & page overlays (extra/ui_assets.h).
        bool (*drawUiIcon)(u8 iconId, float x, float y, float w, float h, u32 rgba);
        void (*drawPageOverlay)(u8 pageId, bool railMode);
    };

    // What the match module asks of the Scenario Presets engine.
    enum ScenarioCall {
        kScenarioChanged = 0,
        kScenarioResetNow,
        kScenarioSavestateLoaded,
        kScenarioSettingsReset
    };

    // What the match module asks of the Character RNG / Fighter Mechanics code.
    enum CharacterRngCall {
        kRngApply,     // a game frame has ended, a savestate was loaded, or a row changed
        kRngAny,       // is any fighter with rows on this page in the match?
        kRngPeach,     // is a Peach in the match? (the Peach rows' `enabled`)
        kRngGameWatch, // is a Game & Watch in the match?
        kRngLuigi,     // is a Luigi in the match?
        kRngSamus,
        kRngMewtwo,
        kRngLucario,
        kRngWario,
        kRngDonkeyKong,
        kRngIvysaur,
        kRngRob,
        kRngLucas,
        kRngSquirtle,
        kRngSheik,
        kRngDedede,
        kRngSnake,
        kRngGanondorf,
        kRngIceClimbers
    };

    // What the match module asks of the Stage Options code.
    enum StageOptionsCall {
        kStageTick,            // once per finished game frame
        kStageChanged,         // a Stage Options row changed or settings were reset
        kStageSavestateLoaded, // a lab savestate was just restored
        kStagePage,            // Stage & Cast stage rows active check
        kStageHazardsRow,      // Hazards row `enabled`
        kStagePlatformsRow,    // Platforms row `enabled`
        kStageFodRow           // Left / Right Platform height rows `enabled`
    };

    // From the extra module's attach; NULL from its detach. The hooks must
    // stay valid until they are replaced.
    void setExtraHooks(const ExtraHooks* hooks);

    // From the match module's render callback.
    void extraRender(u32 renderCall, const game::MatchState& match);
    // The lab tools, through the hooks. Without the extra module, the Alter
    // Inputs rows are grey.
    bool extraAlterRow();
    void extraAlterEdited();
    bool extraLoaded();
    // The Character RNG / Fighter Mechanics page. Without the extra module
    // nothing is forced and its rows are grey, as are a character's rows while
    // it is not in the match.
    void extraCharacterRng();
    bool extraPeachRows();
    bool extraGameWatchRows();
    bool extraLuigiRows();
    bool extraSamusRows();
    bool extraMewtwoRows();
    bool extraLucarioRows();
    bool extraWarioRows();
    bool extraDonkeyKongRows();
    bool extraIvysaurRows();
    bool extraRobRows();
    bool extraLucasRows();
    bool extraSquirtleRows();
    bool extraSheikRows();
    bool extraDededeRows();
    bool extraSnakeRows();
    bool extraGanondorfRows();
    bool extraIceClimbersRows();
    // The Main Menu row: any character the page has rows for.
    bool extraCharacterRngPage();
    // Stage Options rows on Stage & Cast. Without the extra module, or on a
    // static stage, all Stage Options rows stay disabled.
    void extraStageOptionsTick();
    void extraStageOptionsChanged();
    void extraStageOptionsSavestateLoaded();
    bool extraStageOptionsPage();
    bool extraStageHazardsRow();
    bool extraStagePlatformsRow();
    bool extraStageFodRow();
    // Updates the Platforms row's choice labels and max index for the active
    // stage preset (implemented in main/ui/pages/stage_cast.cpp, called by
    // extra/stage_options.cpp).
    void setStagePlatformRowPreset(int preset);
    // The DI Display, through the hooks. Without the extra module its row
    // is grey, nothing is drawn and Project+'s own Draw DI is left alone.
    bool extraDiRow();
    void extraDiDraw(const game::MatchState& match);
    void extraDiReset();
    void extraDiHideDrawDi(bool hide);
    // Hitbox Heatmap, through the hooks.
    void extraHeatmapDraw(const game::MatchState& match);
    void extraHeatmapReset();
    // Grab / Tether / Item-Catch Visualizer, through the hooks.
    void extraGrabVizDraw(const game::MatchState& match);
    // The frame data service: frames until the fighter can act, read ahead from
    // its move's scripts (0 now, k when its interrupts open in the k-th coming
    // frame; -1 when unknown, not certain, or without the extra module). `how`
    // receives ExtraActHow.
    int extraActIn(Fighter* fighter, int* how);
    // Once per finished tracker frame, after the advantage tracker.
    void extraFrameData();
    // A measured frame advantage; true if the extra module showed a value
    // when the hit landed and has dealt with this one (the same: nothing
    // more; another: posted as the measured value).
    bool extraAdvantageMeasured(const game::Advantage& r);
    // PPlusAI recovery engine (extra/pplus_recovery.h). Returns false when
    // extra is absent or recovery is not active.
    bool extraPPlusRecovery(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons);
    void extraPPlusRecoveryReset(int slot);
    // Combo Trainer 3D floating state meters and savestate reset.
    void extraComboDraw(const game::MatchState& match);
    void extraComboReset();
    // Adaptive CPU action bias (-1 when off, tracking only, below confidence,
    // or extra is absent) and savestate reset. When `commit` is false, peeks
    // without advancing mixup counters or trigger telemetry.
    int extraAdaptiveBias(int cpuPlayerNo, int list, bool inCounter, float opponentDist, bool commit);
    void extraAdaptiveReset();
    // Unified P+ Lab Scenario Presets (extra/scenario_presets.h).
    void extraScenarioChanged();
    void extraScenarioResetNow();
    void extraScenarioSavestateLoaded();
    void extraScenarioSettingsReset();
    bool extraScenarioCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons);
    void extraScenarioSavedView(Settings& out);
    // Mid-session Fighter Swap (extra/fighter_swap.h).
    bool extraFighterSwapRow();
    void extraFighterSwapChanged();
    void extraFighterSwapApply();
    bool extraFighterSwapBusy();
    // Binds the roster choice labels (stored in extra) and per-side costume
    // upper bounds onto the P_FIGHTER_SWAP page (implemented in
    // main/ui/pages/fighter_swap.cpp, called by extra/fighter_swap.cpp).
    void setFighterSwapLabels(const char* const* names, int maxIndex);
    void setFighterSwapCostumeMax(int side, int maxIndex);
    // Single-line lab menu option descriptions (extra/menu_descriptions.h).
    const char* extraOptionDesc(u8 pageId, u8 row);
    // Native Brawl/P+ UI button emblems & page overlays (extra/ui_assets.h).
    bool extraDrawUiIcon(u8 iconId, float x, float y, float w, float h, u32 rgba);
    void extraDrawPageOverlay(u8 pageId, bool railMode);
}
}
