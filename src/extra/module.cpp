#include "extra/module.h"

#include <cstdio>
#include <types.h>

#include "extra/adaptive_cpu.h"
#include "extra/character_rng.h"
#include "extra/combo_trainer.h"
#include "extra/di_display.h"
#include "extra/di_picker_core.h"
#include "extra/drill_core_engines.h"
#include "extra/drill_hover_ps_engines.h"
#include "extra/drill_pplus_tech_engines.h"
#include "extra/fighter_swap.h"
#include "extra/frame_data.h"
#include "extra/grab_visualizer.h"
#include "extra/hitbox_colors.h"
#include "extra/hitbox_heatmap.h"
#include "extra/input_viewer.h"
#include "extra/lab_tools.h"
#include "extra/menu_descriptions.h"
#include "extra/pplus_recovery.h"
#include "extra/pplus_tech_tracker.h"
#include "extra/psa_inspector.h"
#include "extra/reversal_slots.h"
#include "extra/scenario_presets.h"
#include "extra/stage_geom.h"
#include "extra/stage_options.h"
#include "extra/ui_assets.h"
#include "main/extra.h"
#include "main/game/match.h"
#include "main/game/tracker.h"
#include "main/gfx/text.h"
#include "main/module.h"
#include "main/ui/info_display.h"
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
            return;
        }
        fighterSwapTick(game::matchState());
        frameDataFrame();
        scenarioPresetsFrame();
        comboTrainerFrame();
        coreDrillEnginesFrame();
        hoverPsFrame();
        pplusTechFrame();
        techTrackerFrame();
        heatmapTick();
        adaptiveCpuFrame();
    }

    static void onComboReset()
    {
        comboTrainerReset();
        coreDrillEnginesSavestateReset();
        hoverPsResetTransient();
        pplusTechReset();
        techTrackerReset();
    }

    static const main::ExtraHooks kHooks = {
        onRender,          alterRow,           alterEdited,        diDraw,
        diReset,           diHideDrawDi,       framesUntilActionable, onFrame,
        frameDataMeasured, characterRng,       stageOptions,       pplusRecoveryStep,
        pplusRecoveryReset, comboTrainerDraw,  onComboReset,       heatmapDraw,
        heatmapReset,      adaptiveCpuBias,    adaptiveCpuReset,   scenarioPresetsCall,
        scenarioPresetsCpuStep, scenarioPresetsSavedView, grabVizDraw,
        fighterSwapRow,    fighterSwapChanged, fighterSwapApply,   fighterSwapBusy,
        optionDesc,        uiAssetsDrawIcon,   uiAssetsDrawPageOverlay,
        ui::infoWanted,    ui::infoStep,       ui::drawInfo,       ui::infoReset,
        ui::applyInfoPreset, inputViewerDraw,  drawColoredHitboxes, diCompassDraw,
        reversalSlotsPick, psaInspectorDraw
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
            frameDataAttach();
            heatmapAttach();
            psaInspectorAttach();
            ui::infoReset();
            main::setExtraHooks(&kHooks);
            TM_LOG("extra: attached (read-only VS) v%s\n", main::kModule.buildVersion);
            return true;
        }
        stageGeomAttach();
        pplusRecoveryAttach();
        reversalSlotsAttach();
        comboTrainerAttach();
        coreDrillEnginesAttach();
        hoverPsAttach();
        pplusTechAttach();
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
        psaInspectorAttach();
        main::setExtraHooks(&kHooks);
        TM_LOG("extra: attached next to match module v%s\n", main::kModule.buildVersion);
        return true;
    }

    static void detach()
    {
        if (s_vsReadOnly)
        {
            psaInspectorDetach();
            heatmapDetach();
            main::setExtraHooks(NULL);
            s_vsReadOnly = false;
            TM_LOG("extra: detaching\n");
            return;
        }
        psaInspectorDetach();
        hitboxColorsDetach();
        inputViewerDetach();
        uiAssetsDetach();
        techTrackerDetach();
        pplusTechDetach();
        fighterSwapDetach();
        // Restore any active Scenario Preset settings snapshot before unloading.
        scenarioPresetsDetach();
        // Project+'s own Draw DI is put back as it was.
        diHideDrawDi(false);
        // Peach's item odds go back to the file's value.
        characterRngDetach();
        stageOptionsDetach();
        heatmapDetach();
        grabVizDetach();
        adaptiveCpuDetach();
        coreDrillEnginesDetach();
        hoverPsDetach();
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
