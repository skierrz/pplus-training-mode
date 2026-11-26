#include "main/module.h"

#include <cstdarg>
#include <cstdio>
#ifdef __MWERKS__
#include <it/it_manager.h>
#endif
#include <types.h>

#include "main/cpu/actions.h"
#include "main/cpu/cpu.h"
#include "main/display/display.h"
#include "main/extra.h"
#include "main/game/code_menu.h"
#include "main/game/freeze.h"
#include "main/game/match_log.h"
#include "main/game/overlays.h"
#include "main/game/tracker.h"
#include "main/game/training_controller.h"
#include "main/game/training_damage.h"
#include "main/gfx/draw.h"
#include "main/lab.h"
#include "main/record/record.h"
#include "main/savestate/savestate.h"
#include "main/services.h"
#include "main/settings.h"
#include "main/storage/storage.h"
#include "main/ui/match_hud.h"
#include "main/ui/pages/pages.h"
#include "main/ui/settings_modal.h"
#include "shared/drill_session.h"
#include "shared/game/code_menu.h"
#include "shared/game/scene.h"
#include "shared/log.h"
#include "shared/perf.h"
#include "shared/version.h"

#ifdef TM_PERF
#include "main/gfx/text.h"
#include "main/savestate/snapshot.h"
#endif
#ifdef TM_EXPERIMENTS
#include "main/experiments/heap_stats.h"
#include "main/experiments/padding.h"
#endif

namespace tm {
namespace main {
    static void ignoreLog(const char*, ...)
    {
    }

    static u8* noSettings()
    {
        return NULL;
    }

    static const iface::Services kNoServices = {ignoreLog, noSettings};
    static iface::Interface* s_iface = NULL;
    static bool s_vsReadOnly = false;
    static bool s_vsPrevRan = false;

#pragma force_active on
    const iface::Services& services()
    {
        return s_iface != NULL ? s_iface->services : kNoServices;
    }
#pragma force_active reset

#ifdef __MWERKS__
    void logf(const char* fmt, ...)
    {
#ifdef TM_PERF
        perf::add(perf::kCountLogs, 1);
#endif
        if (s_iface == NULL)
        {
            return;
        }
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        buf[sizeof(buf) - 1] = '\0';
        s_iface->services.log("[" TM_PLUGIN_NAME "] %s", buf);
    }
#endif

#ifdef TM_PERF
    // The frame's end for the match module's sections, and the calibration
    // the test script asks for (savestate/snapshot.cpp).
    static void perfOnRender()
    {
        perf::Block* bp = perf::blockPtr();
        if (bp == NULL)
        {
            return;
        }
        perf::Block& b = *bp;
        if ((b.control & perf::kControlCalibrate) != 0)
        {
            b.control &= ~static_cast<u32>(perf::kControlCalibrate);
            savestate::perfCalibrate();
            u32 t[3];
            gfx::perfTextCalibrate(t);
            TM_LOG("perf: calibrate text ticks: print200=%u glyph200=%u prepare50=%u\n", t[0], t[1], t[2]);
        }
        if ((b.control & perf::kControlDamageSlot) != 0)
        {
            u32 how = (b.control >> 8) & 0xFF;
            b.control &= ~static_cast<u32>(perf::kControlDamageSlot | 0xFF00);
            savestate::perfDamageSlot(how);
        }
        if ((b.control & perf::kControlInvalidRead) != 0)
        {
            b.control &= ~static_cast<u32>(perf::kControlInvalidRead);
            static const u32 kAddrs[4] = {0x00000010, 0x7E000010, 0x81800010, 0x94000010};
            for (u32 i = 0; i < 4; i++)
            {
                u32 v = *reinterpret_cast<volatile const u32*>(kAddrs[i]);
                TM_LOG("perf: probe read of %08x gave %08x\n", kAddrs[i], v);
            }
        }
        perf::frameEnd();
    }
#endif

    static void onRender(u32 call)
    {
        game::MatchState match = game::matchState();
        game::logMatchChange(match, call);
#ifdef TM_EXPERIMENTS
        experiments::heapStatsOnRender(call, match);
        experiments::touchPadding(call);
#endif
        // The frame just drawn goes to the trackers, displays and drills before
        // we draw, so what it causes is on its own picture; a frame held by
        // frame advance or Training's pause is handed over once
        // (game/tracker.h).
        {
            TM_PERF_SCOPE(kTrackerHandOver);
            game::trackerHandOver();
        }
        // Draw the save flash first so the HUD and overlays render above it.
        if (!s_vsReadOnly)
        {
            TM_PERF_SCOPE(kSavestateRender);
            savestate::onRender(match);
        }
        {
            TM_PERF_SCOPE(kLabRender);
            lab::onRender(call, match);
        }
        if (!s_vsReadOnly && match.running && !match.paused && !lab::menuOpen() && !ui::settingsModalOpen()
            && record::hudVisible())
        {
            TM_PERF_SCOPE(kRecordHud);
            gfx::begin();
            record::drawHud();
            gfx::end();
        }
        {
            TM_PERF_SCOPE(kExtraRender);
            extraRender(call, match);
        }
        // after everything that asks for the match timer
        if (!s_vsReadOnly)
        {
            TM_PERF_SCOPE(kMatchHud);
            ui::matchHudApply(match);
        }
        if (!s_vsReadOnly)
        {
            TM_PERF_SCOPE(kStorage);
            storage::onRender(match);
        }
#ifdef TM_PERF
        perfOnRender();
#endif
    }

    // Savestates run at the top of the main loop, before the lab sees the pads.
    static void onPads(u32 phase, gfPadStatus* pads)
    {
        if (phase == iface::kPadsGame)
        {
            TM_PERF_SCOPE(kPadsDamage);
            game::trainingDamageStep(game::matchState());
        }
        {
            TM_PERF_SCOPE(kPadsSavestate);
            savestate::onPads(phase, pads);
        }
        {
            TM_PERF_SCOPE(kPadsLab);
            lab::onPads(phase, pads);
        }
    }

    static void (*s_recordPads)(u32, gfPadStatus*) = NULL;

    // Skip Brawl/P+'s unused Training preloads of 5 Pokemon (PokemonResource,
    // 1,024 KiB) and 1 Assist Trophy (AssistFigureResource, 410 KiB) by
    // clearing itManager+0x149F before scMelee::start sets +0xFF5 = 1
    // (itManager::processBegin checks lbz r0, 0xFF5(r28) at 0x809AD580 and
    // lbz r0, 0x149F(r28) at 0x809AD58C; zero g_mtRand churn).
    static void skipTrainingItemPreload()
    {
#ifdef __MWERKS__
        if (!game::trainingGameMode())
        {
            return;
        }
        itManager* mgr = itManager::getInstance();
        if (game::isRam(mgr))
        {
            *reinterpret_cast<u8*>(reinterpret_cast<u32>(mgr) + 0x149Fu) = 0;
        }
#endif
    }

    // While P+'s Code Menu is open (state 4), it pauses gfTaskScheduler and
    // masks pads; close our lab menu if open and keep every pad consumer
    // (recording, savestates, lab, drills) quiet until it closes.
    static void onPadsOuter(u32 phase, gfPadStatus* pads)
    {
        if (phase == iface::kPadsSystem)
        {
            skipTrainingItemPreload();
        }
        if (game::codeMenuOpen())
        {
            if (phase == iface::kPadsSystem && lab::menuOpen())
            {
                lab::closeMenu();
            }
            if (phase == iface::kPadsGame)
            {
                game::trackerStepRuns(false);
            }
            return;
        }
        if (s_recordPads != NULL)
        {
            s_recordPads(phase, pads);
        }
    }

    static void (*s_cpuInput)(const iface::FighterInput* call) = NULL;

    static void onFighterInput(const iface::FighterInput* call)
    {
        if (s_cpuInput != NULL)
        {
            TM_PERF_SCOPE(kInputCpu);
            s_cpuInput(call);
        }
        {
            TM_PERF_SCOPE(kInputSavestate);
            savestate::onFighterInput(call);
        }
        {
            TM_PERF_SCOPE(kInputLab);
            lab::onFighterInput(call);
        }
    }

    // Passive pad callback for opted-in local offline Versus: updates read-only
    // stick capture, fighter trackers, and Info Display clocks without masking
    // pads, opening the lab menu, freezing gameplay, or mutating fighters.
    static void onPadsReadOnly(u32 phase, gfPadStatus* pads)
    {
        if (phase != iface::kPadsGame)
        {
            return;
        }
        if (game::codeMenuOpen())
        {
            game::trackerStepRuns(false);
            return;
        }
        display::capturePads(pads);
        game::MatchState m = game::matchState();
        bool live = m.running && !m.paused && !game::heldByOthers();
        {
            TM_PERF_SCOPE(kTrackerStep);
            game::trackerStep(live);
        }
        if (extraInfoWanted())
        {
            extraInfoStep(s_vsPrevRan);
        }
        s_vsPrevRan = live;
        game::trackerStepRuns(live);
    }

    static bool attach(iface::Interface* iface)
    {
        if (iface->version != iface::kVersion || iface->size != sizeof(iface::Interface))
        {
            return false;
        }
        u8* block = iface->services.settings();
        if (block == NULL)
        {
            return false;
        }
        s_iface = iface;
        s_vsReadOnly = !game::trainingGameMode();
        s_vsPrevRan = false;
#ifdef TM_PERF
        perf::attach();
        perf::announce("main", perf::kMainNames, perf::kMainSections);
#endif
#ifdef __MWERKS__
        ui::pages::pagesAttach();
        cpu::actionsAttach();
#endif
        bool reset = loadSettings(block);
        if (s_vsReadOnly)
        {
            lab::attach();
            iface->callbacks.render = onRender;
            iface->callbacks.pads = onPadsReadOnly;
            iface->callbacks.fighterPad = NULL;
            iface->callbacks.fighterInput = NULL;
            iface->callbacks.fighterFixCamera = NULL;
            TM_LOG("main: read-only offline Versus displays attached\n");
            return true;
        }
        TM_LOG("main: attached in %s / %s; settings v%d at %08x %s\n", game::sceneName(), game::sequenceName(),
               kSettingsVersion, block, reset ? "set to defaults" : "kept");
        // Mark that a Training match ran so returning to Training CSS reopens
        // the boot launcher (while backing out of Stage Select does not).
        drills::Session* ds = drills::sessionIn(block);
        if (ds != NULL)
        {
            ds->matchRan = 1;
        }
        // The SD card's settings (first match of the session), before the
        // features read them.
        storage::attach(block);
        game::codeMenuMatchAttach();
        lab::attach();
        savestate::attach();
        iface->callbacks.render = onRender;
        iface->callbacks.pads = onPads;
        iface->callbacks.fighterPad = lab::onFighterPad;
        iface->callbacks.fighterFixCamera = game::overlaysFixCamera;
        cpu::moduleAttach(iface);
        // The CPU driver fills the fighter-input callback; the savestates see
        // each input after it.
        s_cpuInput = iface->callbacks.fighterInput;
        iface->callbacks.fighterInput = onFighterInput;
        // Recording wraps both: it takes and reads each fighter's input
        // around the CPU driver and the taunt strip, and handles the pads
        // after the savestate (loop top) and the lab (record.h).
        record::attach(iface);
        s_recordPads = iface->callbacks.pads;
        iface->callbacks.pads = onPadsOuter;
#ifdef TM_EXPERIMENTS
        experiments::logPadding();
#endif
        return true;
    }

    static void detach()
    {
#ifdef TM_EXPERIMENTS
        experiments::heapStatsOnDetach();
#endif
        if (s_vsReadOnly)
        {
            lab::detach();
#ifdef __MWERKS__
            cpu::actionsDetach();
            ui::pages::pagesDetach();
#endif
            TM_LOG("main: detaching\n");
#ifdef TM_PERF
            perf::detach();
#endif
            s_vsReadOnly = false;
            s_vsPrevRan = false;
            s_iface = NULL;
            return;
        }
        storage::detach();
        record::detach();
        savestate::detach();
        cpu::moduleDetach();
        lab::detach();
#ifdef __MWERKS__
        cpu::actionsDetach();
        ui::pages::pagesDetach();
#endif
        game::codeMenuMatchDetach();
        game::trainingDamageRestore();
        game::trainingControllerRestore();
        TM_LOG("main: detaching\n");
#ifdef TM_PERF
        perf::detach();
#endif
        s_recordPads = NULL;
        s_iface = NULL;
    }

    const iface::ModuleInfo kModule = {
        iface::kModuleMagic,
        iface::kVersion,
        iface::kKindMatch,
        TM_VERSION,
        attach,
        detach
    };
}
}
