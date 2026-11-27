#include "extra/module.h"

#include <cstdio>
#include <types.h>

#include "extra/action_log.h"
#include "extra/adaptive_cpu.h"
#ifndef TM_STAGE4C
#define TM_STAGE4C 0
#endif
#if TM_STAGE4C
#include "extra/challenge_backends.h"
#endif
#include "extra/character_rng.h"
#include "extra/combo_break_rush.h"
#include "extra/combo_trainer.h"
#include "extra/di_display.h"
#include "extra/di_picker_core.h"
#include "extra/drill_core_engines.h"
#include "extra/drill_hover_ps_engines.h"
#include "extra/drill_hud.h"
#include "extra/drill_pplus_tech_engines.h"
#include "extra/drill_practice_driver.h"
#include "extra/fighter_swap.h"
#include "extra/frame_data.h"
#include "extra/grab_visualizer.h"
#include "extra/hitbox_colors.h"
#include "extra/hitbox_heatmap.h"
#include "extra/hud_draw.h"
#include "extra/input_viewer.h"
#include "extra/lab_tools.h"
#include "extra/menu_descriptions.h"
#include "extra/olimar_lab.h"
#include "extra/pplus_recovery.h"
#include "extra/pplus_tech_tracker.h"
#include "extra/psa_inspector.h"
#include "extra/reversal_slots.h"
#include "extra/scenario_presets.h"
#include "extra/stage_geom.h"
#include "extra/stage_options.h"
#include "extra/tech_sparkle.h"
#include "extra/timing_meter.h"
#include "extra/ui_assets.h"
#include "main/extra.h"
#include "main/game/match.h"
#include "main/game/status_names.h"
#include "main/game/tracker.h"
#include "main/gfx/text.h"
#include "main/lab.h"
#include "main/module.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "main/ui/info_display.h"
#include "main/ui/settings_modal.h"
#include "shared/game/scene.h"
#include "shared/log.h"
#include "shared/version.h"

// The extra module: loaded next to the match module, into another heap, and
// linked against it (the Makefile hands elf2rel the match module's symbols),
// so it calls and reads the match module directly. It registers its services
// with the extra hooks, and logs one line per match from what it reads in the
// match module (drawn too with EXTRA_PROOF=1).

#ifndef TM_EXTRA_PAD_BYTES
#define TM_EXTRA_PAD_BYTES 0
#endif

namespace tm {
namespace extra {
    enum {
        kLogAtFrame = 120, // tracker frame of the one log line per match
        kLineSize = 96
    };

#if TM_EXTRA_PAD_BYTES > 0
    // Experiments only: zero-filled room, to make the module take more of
    // its heap.
    static u8 s_padding[TM_EXTRA_PAD_BYTES];
#endif

    static bool s_logged = false;
    static bool s_vsReadOnly = false;

    // The match module's version and interface version (its data), the
    // tracker's frame and slot 0's status (its functions), and the trace
    // switch (its zero-filled data).
    static void describe(char* out)
    {
        const iface::ModuleInfo& match = main::kModule;
        const game::Tracked* t = game::tracked(0);
        sprintf(out, "extra: match v%s/%u, frame %d, slot 0 status %d, trace %u", match.buildVersion,
                match.version, game::trackerFrame(), t != NULL ? t->cur.status : -1, game::g_trackerTrace);
    }

    static void onRender(u32 call, const game::MatchState& match)
    {
#if TM_EXTRA_PAD_BYTES > 0
        s_padding[call % sizeof(s_padding)] = 1;
#endif
        if (!s_vsReadOnly)
        {
            fighterSwapTick(match);
            scenarioPresetsOnRender(match);
            drillPracticeApplyHudVisibility(match);
        }
        if (!match.running)
        {
            return;
        }
        char line[kLineSize];
        if (!s_logged && game::trackerFrame() >= kLogAtFrame)
        {
            s_logged = true;
            describe(line);
            TM_LOG("%s (render call %u)\n", line, call);
        }
        bool modalUp = ui::settingsModalOpen();
        bool drawDrill = !s_vsReadOnly && !modalUp && (drillHudHook(0, NULL) & 1u) != 0u;
        bool drawAdap = !s_vsReadOnly && !modalUp && !match.paused && !lab::menuOpen()
                        && main::settings().cpu.options.control == cpu::kControlLab
                        && main::settings().cpu.options.adaptiveMode != cpu::kAdaptiveOff;
        bool drawScen = !s_vsReadOnly && !modalUp && !match.paused && !lab::menuOpen() && scenarioPresetsWantsDraw(match);
        bool drawRush = !s_vsReadOnly && !modalUp && !match.paused && !lab::menuOpen() && comboBreakRushActive();
        bool drawLog = !modalUp && !match.paused && !lab::menuOpen()
                       && main::settings().general.actionLog != main::kActionLogOff;
        bool drawCpuTag = !s_vsReadOnly && !modalUp && !match.paused && !lab::menuOpen() && match.training;
        if (drawDrill || drawAdap || drawScen || drawRush || drawLog || drawCpuTag)
        {
            gfx::begin();
            if (drawDrill)
            {
                drillHudDraw(match);
            }
            if (drawAdap)
            {
                adaptiveCpuDraw(match);
            }
            if (drawScen)
            {
                scenarioPresetsDraw(match);
            }
            if (drawRush)
            {
                comboBreakRushDraw(match);
            }
            if (drawLog)
            {
                actionLogDraw(match);
            }
            if (drawCpuTag)
            {
                drawCpuStatusTag(match, lab::menuOpen());
            }
            gfx::end();
        }
#ifdef TM_EXTRA_PROOF
        // Off by default (make EXTRA_PROOF=1): the line would change the
        // screenshots the other tests compare.
        describe(line);
        gfx::begin();
        gfx::text(16.0f, 444.0f, line, 0xFFFF40FF, 0.5f);
        gfx::end();
#endif
    }

    // Once per finished tracker frame.
    static void onFrame()
    {
        if (s_vsReadOnly)
        {
            frameDataFrame();
            heatmapTick();
            actionLogFrame();
            return;
        }
        fighterSwapTick(game::matchState());
        frameDataFrame();
        scenarioPresetsFrame();
        comboTrainerFrame();
        coreDrillEnginesFrame();
        hoverPsFrame();
        pplusTechFrame();
#if TM_STAGE4C
        challengeBackendsFrame();
#endif
        drillHudFrame();
        drillPracticeFrame();
        techTrackerFrame();
        techSparkleFrame();
        timingMeterFrame();
        heatmapTick();
        adaptiveCpuFrame();
        comboBreakRushFrame();
        actionLogFrame();
    }

    static void onComboReset()
    {
        comboTrainerReset();
        coreDrillEnginesSavestateReset();
        hoverPsResetTransient();
        pplusTechReset();
#if TM_STAGE4C
        challengeBackendsReset();
#endif
        drillHudSavestateReset();
        drillPracticeReset();
        techTrackerReset();
        techSparkleReset();
        timingMeterReset();
        comboBreakRushReset();
        actionLogReset();
    }

    static float osdTopStartYHook(bool isTopRight)
    {
        return hudOsdTopStartY(isTopRight, s_vsReadOnly);
    }

    static bool onCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons)
    {
        if (drillPracticeCpuStep(who, v, f, out, lastButtons))
        {
            return true;
        }
        return scenarioPresetsCpuStep(who, v, f, out, lastButtons);
    }

    static void onTimingAndPracticeDraw(const game::MatchState& match)
    {
        timingMeterDraw(match);
        drillPracticeDraw(match);
        comboBreakRushDraw3d(match);
#if TM_STAGE4C
        challengeBackendsDraw3d(match);
#endif
    }

    static u32 onDrillHook(u32 phase, gfPadStatus* pads)
    {
        u32 bits = drillHudHook(phase, pads);
        if (pads == NULL && comboBreakRushActive())
        {
            if (phase == 2u)
            {
                ui::layout::setLabBand(ui::layout::comboBreakHudBox(gfx::widescreen()));
            }
            bits |= 1u | 8u;
        }
        if (pads == NULL && phase == 2u && main::settings().general.actionLog != main::kActionLogOff)
        {
            int maxP = 2;
            for (int i = 0; i < game::kMaxTracked; i++)
            {
                const game::Tracked* t = game::tracked(i);
                if (t != NULL && t->used && !t->partner && t->playerNo + 1 > maxP)
                {
                    maxP = t->playerNo + 1;
                }
            }
            if (maxP > 2)
            {
                bool wide = gfx::widescreen();
                bool timerOn = (bits & 1u) == 0u;
                ui::layout::Box cur = ui::layout::topBand();
                ui::layout::Box strip = ui::layout::actionLogBox(wide, maxP, timerOn);
                ui::layout::Box merged = cur;
                if (merged.x1 <= merged.x0 || merged.y1 <= merged.y0)
                {
                    merged = strip;
                }
                else
                {
                    if (strip.x0 < merged.x0) merged.x0 = strip.x0;
                    if (strip.y0 < merged.y0) merged.y0 = strip.y0;
                    if (strip.x1 > merged.x1) merged.x1 = strip.x1;
                    if (strip.y1 > merged.y1) merged.y1 = strip.y1;
                }
                ui::layout::setLabBand(merged);
            }
        }
        return bits;
    }

    static const main::ExtraHooks kHooks = {
        onRender,          alterRow,           alterEdited,        diDraw,
        diReset,           diHideDrawDi,       framesUntilActionable, onFrame,
        frameDataMeasured, frameDataShieldHoldFullMove, characterRng, stageOptions,
        pplusRecoveryStep, pplusRecoveryReset, comboTrainerDraw,   onComboReset,
        heatmapDraw,       heatmapReset,       adaptiveCpuBias,    adaptiveCpuReset,
        scenarioPresetsCall, onCpuStep,        scenarioPresetsSavedView, grabVizDraw,
        fighterSwapRow,    fighterSwapChanged, fighterSwapApply,   fighterSwapBusy,
        optionDesc,        uiAssetsDrawPageOverlay,
        ui::infoWanted,    ui::infoStep,       ui::drawInfo,       ui::infoReset,
        ui::applyInfoPreset, inputViewerDraw,  drawColoredHitboxes, diCompassDraw,
        reversalSlotsPick, psaInspectorDraw,   onTimingAndPracticeDraw, onDrillHook,
        osdTopStartYHook,  olimarLabAction,    NULL
    };

    static bool attach(iface::Interface* iface)
    {
        if (iface->version != iface::kVersion || main::kModule.magic != iface::kModuleMagic)
        {
            return false;
        }
        s_logged = false;
        s_vsReadOnly = !game::trainingGameMode();
        if (s_vsReadOnly)
        {
            game::statusNamesAttach();
            frameDataAttach();
            heatmapAttach();
            psaInspectorAttach();
            actionLogAttach();
            ui::infoReset();
            main::setExtraHooks(&kHooks);
            TM_LOG("extra: attached (read-only VS) v%s\n", main::kModule.buildVersion);
            return true;
        }
        game::statusNamesAttach();
        menuDescAttach();
        stageGeomAttach();
        pplusRecoveryAttach();
        comboTrainerAttach();
        coreDrillEnginesAttach();
        hoverPsAttach();
        pplusTechAttach();
#if TM_STAGE4C
        challengeBackendsAttach();
#endif
        techTrackerAttach();
        adaptiveCpuAttach();
        scenarioPresetsAttach();
        diAttach();
        frameDataAttach();
        characterRngAttach();
        stageOptionsAttach();
        heatmapAttach();
        grabVizAttach();
        fighterSwapAttach();
        uiAssetsAttach();
        inputViewerAttach();
        hitboxColorsAttach();
        reversalSlotsAttach();
        techSparkleAttach();
        psaInspectorAttach();
        timingMeterAttach();
        drillPracticeAttach();
        drillHudAttach();
        comboBreakRushAttach();
        actionLogAttach();
        main::setExtraHooks(&kHooks);
        TM_LOG("extra: attached next to match module v%s\n", main::kModule.buildVersion);
        return true;
    }

    static void detach()
    {
        if (s_vsReadOnly)
        {
            actionLogDetach();
            psaInspectorDetach();
            heatmapDetach();
            frameDataDetach();
            game::statusNamesDetach();
            main::setExtraHooks(NULL);
            s_vsReadOnly = false;
            TM_LOG("extra: detaching\n");
            return;
        }
        actionLogDetach();
        comboBreakRushDetach();
        drillHudDetach();
        drillPracticeDetach();
        timingMeterDetach();
        psaInspectorDetach();
        techSparkleDetach();
        reversalSlotsReset();
        hitboxColorsDetach();
        inputViewerDetach();
        uiAssetsDetach();
        techTrackerDetach();
#if TM_STAGE4C
        challengeBackendsDetach();
#endif
        pplusTechDetach();
        fighterSwapDetach();
        // Restore any active Scenario Preset settings snapshot before unloading.
        scenarioPresetsDetach();
        // Project+'s own Draw DI is put back as it was.
        diHideDrawDi(false);
        diDetach();
        frameDataDetach();
        // Peach's item odds go back to the file's value.
        characterRngDetach();
        stageOptionsDetach();
        heatmapDetach();
        grabVizDetach();
        adaptiveCpuDetach();
        coreDrillEnginesDetach();
        hoverPsDetach();
        comboTrainerDetach();
        pplusRecoveryDetach();
        stageGeomDetach();
        menuDescDetach();
        game::statusNamesDetach();
        main::setExtraHooks(NULL);
        TM_LOG("extra: detaching\n");
    }

    const iface::ModuleInfo kModule = {
        iface::kModuleMagic,
        iface::kVersion,
        iface::kKindExtra,
        TM_VERSION,
        attach,
        detach
    };
}
}
