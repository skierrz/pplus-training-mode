#pragma once

#include <types.h>

#include "main/cpu/fighter_input.h"
#include "main/cpu/view.h"
#include "main/game/match.h"
#include "main/ui/pages/scenarios.h"

class Fighter;
struct soModuleEnumeration;

// Unified P+ Lab Scenario Presets runtime backend (src/extra/).
namespace tm {
namespace game {
    struct FighterRef;
}
namespace main {
    struct Settings;
}
namespace extra {
    struct StageGeometry;
    void scenarioPresetsAttach();
    void scenarioPresetsDetach();
    void scenarioPresetsPrepareSwap();
    void scenarioPresetsRestoreAfterSwap(const ui::pages::ScenarioMenuState& saved);
    void scenarioPresetsOnRender(const game::MatchState& match);
    bool scenarioPresetsWantsDraw(const game::MatchState& match);
    void scenarioPresetsDraw(const game::MatchState& match);
    void scenarioPresetsFrame();
    void scenarioPresetsCall(int call);
    bool scenarioPresetsCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out,
                                u32 lastButtons);
    void scenarioPresetsSavedView(main::Settings& out);
    // Returns 0 (Right), 1 (Left), or 2 (Random) for Combo Trainer's
    // Recovery Trainer loop when Edgeguard scenario is active (2 otherwise).
    int scenarioPresetsEdgeguardSide();
    // Shared fighter warp cleanups and module reads (used by Combo Trainer and Scenario Presets).
    void clearWarpIntang(const game::FighterRef& ref);
    void finishStandingWarp(const game::FighterRef& ref, bool setNoclip, bool fallIfNoGround = false);
    void warpAirClean(const game::FighterRef& ref, float x, float y, bool noclip, float dmg, float lr,
                      bool refreshAir, bool spendDj);
    void warpGroundClean(const game::FighterRef& ref, float x, float y, bool noclip, float dmg, float lr,
                         bool fallIfNoGround = false);
    bool findHmnAndCpu(const game::FighterRef* refs, int n, game::FighterRef& hmnRef, game::FighterRef& cpuRef,
                       bool& haveHmn, bool& haveCpu);
    int readFighterHitlag(const soModuleEnumeration* m);
    bool isFighterAir(const soModuleEnumeration* m);

    // Runs Edgeguard scenario's configured offstage launch/spawn when active.
    bool scenarioPresetsEdgeguardRespawn(const game::FighterRef& ref, const StageGeometry& geom, u32& rng,
                                         bool setNoclip, float damage);
}
}
