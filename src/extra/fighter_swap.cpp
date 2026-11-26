// Mid-session Fighter Swap runtime (gmPlayerInitData + stLoaderPlayer +
// set_guard). Heavy roster data and swap execution live in extra so match
// stays lean.
#define TM_FIGHTER_SWAP_IMPL 1
#include "extra/fighter_swap.h"

#include <ft/ft_manager.h>
#include <gm/gm_global.h>
#include <string.h>

#include "extra/adaptive_cpu.h"
#include "extra/character_rng.h"
#include "extra/combo_trainer.h"
#include "extra/di_display.h"
#include "extra/drill_core_engines.h"
#include "extra/drill_hover_ps_engines.h"
#include "extra/drill_pplus_tech_engines.h"
#include "extra/fighter_swap_core.h"
#include "extra/frame_data.h"
#include "extra/hitbox_heatmap.h"
#include "extra/net_alloc.h"
#include "extra/pplus_recovery.h"
#include "extra/scenario_presets.h"
#include "main/cpu/driver.h"
#include "main/cpu/options.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/game/match.h"
#include "main/lab.h"
#include "main/record/record.h"
#include "main/savestate/savestate.h"
#include "main/services.h"
#include "main/storage/notice.h"
#include "main/ui/menu.h"
#include "main/ui/pages/fighter_swap.h"
#include "main/ui/pages/scenarios.h"
#include "shared/drill_session.h"
#include "shared/game/frame.h"
#include "shared/game/mem.h"
#include "shared/game/menu_sound.h"
#include "shared/log.h"
#include "shared/version.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kDebugMagic = 0x544D4653, // "TMFS"
            // gmGlobalModeMelee + 0x98: four 0x5C-byte gmPlayerInitData structs.
            kModePlayers = 0x98,
            // gmSelCharData: +0x7 hasChoice, +0x8 icon words, +0xB8 players,
            // +0x43AD8 P+ AltZ/AltR word per port (AltCostume.asm:72-82).
            kSelHasChoice = 0x07,
            kSelIcons = 0x08,
            kSelPlayers = 0xB8,
            kSelAltCostume = 0x43AD8,
            kPlayerSize = 0x5C,
            kPlayerKind = 0x00,
            kPlayerState = 0x01,
            kPlayerColorFile = 0x05,
            kPlayerColor = 0x06,
            kStateHuman = 0,
            kStateCpu = 1,
            // main.dol muMenu costume helpers (RSBE01 rev 2, P+ 3.2.1):
            // 0x800AF8D0 getNumCharColor(int selch),
            // 0x800AF93C getFighterColorFileNo(int selch, int costumeIdx),
            // 0x80585B00 P+ extended CSS/costume table (16 bytes per selch).
            kGetNumColorAddr = 0x800AF8D0,
            kGetColorFileAddr = 0x800AF93C,
            kCharColorTableAddr = 0x80585B00,
            kCharColorEntrySize = 16,
            kCharColorMaxIndex = 56,
            // sora_melee AltCostume.asm marker at 0x8084CF38 (b +0x1C; "AltR\0AltZ").
            kAltCostumeMarkerAddr = 0x8084CF38,
            kSwapSettleFrames = 2,
            kSwapTimeoutFrames = 600
        };

        enum RefusalCode {
            kRefuseNone = 0,
            kRefuseDrill = 1,
            kRefuseState = 2,
            kRefuseLoading = 3,
            kRefuseSetBudget = 4,
            kRefuseNoChange = 5
        };

        const u32 kGetNumColorWords[4] = {0x3C808058u, 0x546025B6u, 0x38845B00u, 0x38600000u};
        const u32 kGetColorFileWords[4] = {0x3CA08058u, 0x54632036u, 0x38A55B00u, 0x5480083Cu};
        const u32 kAltCostumeWords[4] = {0x4800001Cu, 0x416C7452u, 0x00416C74u, 0x5A000000u};

        typedef int (*GetNumColorFn)(int selch);
        typedef int (*GetColorFileFn)(int selch, int costumeIdx);

        struct PendingSlot {
            bool active;
            int oldEntryId;
            u8 wantKind;
            u8 wantColorFile;
        };

        // Compact RAM-inspectable telemetry block for live Dolphin tests ("TMFS").
        struct FighterSwapDebugBlock {
            u32 magic;            // +0x00 "TMFS"
            u32 scratchAddr;      // +0x04 FighterSwapMenuState in ui::menuScratch()
            u32 swapCount;        // +0x08 total swaps initiated
            u32 completedCount;   // +0x0C total swaps settled
            u32 refusalCount;     // +0x10 total swaps refused
            u8 inProgress;        // +0x14 1 while stLoaderPlayer transition is active
            u8 lastRefusal;       // +0x15 RefusalCode
            u8 costumeHelperOk;   // +0x16 1 if muMenu costume helpers verified
            u8 altCostumeOk;      // +0x17 1 if AltCostume.asm marker verified
            s8 p1Slot;            // +0x18 active human player index (0..3, -1 if none)
            s8 cpuSlot;           // +0x19 active CPU player index (0..3, -1 if none)
            u8 p1MaxCostume;      // +0x1A current max costume index for P1 selection
            u8 cpuMaxCostume;     // +0x1B current max costume index for CPU selection
            u8 lastP1Kind;        // +0x1C last written P1 gmCharacterKind
            u8 lastP1ColorFile;   // +0x1D last written P1 m_colorFileNo
            u8 lastCpuKind;       // +0x1E last written CPU gmCharacterKind
            u8 lastCpuColorFile;  // +0x1F last written CPU m_colorFileNo
        };
        TM_STATIC_ASSERT(sizeof(FighterSwapDebugBlock) == 0x20, tm_fighter_swap_debug_block_size);

        struct FighterSwapNet {
            const char* rosterNames[kFighterSwapCount];
            PendingSlot pending[game::kMaxFighters];
            FighterSwapDebugBlock debug;
            ui::pages::ScenarioMenuState savedScenarioMenu;
        };

        FighterSwapNet* s_net = NULL;
        bool s_costumeHelperOk = false;
        bool s_altCostumeOk = false;
        bool s_synced = false;
        bool s_swapInProgress = false;
        u8 s_prevSelection[2] = {0xFF, 0xFF};
        u32 s_settleFrames = 0;
        u32 s_waitFrames = 0;
        u32 s_lastGameFrame = 0;
        bool s_hadActiveScenario = false;

        bool codeMatches(u32 addr, const u32* words, int count)
        {
            const u32* p = reinterpret_cast<const u32*>(addr);
            for (int i = 0; i < count; i++)
            {
                if (p[i] != words[i])
                {
                    return false;
                }
            }
            return true;
        }

        bool drillActive()
        {
            const drills::Session* s = drills::sessionIn(main::services().settings());
            return s != NULL && s->drill != drills::kDrillNone;
        }

        u8* modePlayers()
        {
            GameGlobal* g = g_GameGlobal;
            if (!game::isRam(g) || !game::isRam(g->m_modeMelee))
            {
                return NULL;
            }
            return reinterpret_cast<u8*>(g->m_modeMelee) + kModePlayers;
        }

        u8* selCharData()
        {
            GameGlobal* g = g_GameGlobal;
            if (!game::isRam(g) || !game::isRam(g->m_selCharData))
            {
                return NULL;
            }
            return reinterpret_cast<u8*>(g->m_selCharData);
        }

        bool selchTableValid(int cssIcon)
        {
            if (!s_costumeHelperOk || cssIcon < 0 || cssIcon > kCharColorMaxIndex)
            {
                return false;
            }
            const void* entry = reinterpret_cast<const void*>(
                kCharColorTableAddr + static_cast<u32>(cssIcon) * kCharColorEntrySize);
            u32 ptr = game::wordAt(entry, 8);
            return game::isRam(reinterpret_cast<const void*>(ptr));
        }

        int queryNumColors(u8 cssIcon)
        {
            if (!selchTableValid(cssIcon))
            {
                return 1;
            }
            int n = reinterpret_cast<GetNumColorFn>(kGetNumColorAddr)(cssIcon);
            if (n <= 0)
            {
                return 1;
            }
            if (n > kFighterSwapMaxCostume + 1)
            {
                n = kFighterSwapMaxCostume + 1;
            }
            return n;
        }

        u8 queryColorFileNo(u8 cssIcon, int costumeIdx)
        {
            if (!selchTableValid(cssIcon))
            {
                return 0;
            }
            int n = queryNumColors(cssIcon);
            int idx = fighterSwapClampCostume(costumeIdx, n);
            int fileNo = reinterpret_cast<GetColorFileFn>(kGetColorFileAddr)(cssIcon, idx);
            return (fileNo >= 0 && fileNo <= 255) ? static_cast<u8>(fileNo) : 0;
        }

        int detectLiveCostumeIndex(u8 cssIcon, u8 colorFileNo, u8 colorNo)
        {
            if (!selchTableValid(cssIcon))
            {
                return 0;
            }
            int n = queryNumColors(cssIcon);
            u8 files[kFighterSwapMaxCostume + 1];
            for (int i = 0; i < n; i++)
            {
                files[i] = queryColorFileNo(cssIcon, i);
            }
            return fighterSwapFindCostumeIndex(colorFileNo, colorNo, files, n);
        }

        // Finds the first active human and first active CPU player slots by
        // looping over all kMaxFighters entries in m_playersInitData.
        void findTargetSlots(const u8* players, int& p1Slot, int& cpuSlot)
        {
            p1Slot = -1;
            cpuSlot = -1;
            if (players == NULL)
            {
                return;
            }
            for (int p = 0; p < game::kMaxFighters; p++)
            {
                u8 st = players[p * kPlayerSize + kPlayerState];
                if (st == kStateHuman && p1Slot < 0)
                {
                    p1Slot = p;
                }
                else if (st == kStateCpu && cpuSlot < 0)
                {
                    cpuSlot = p;
                }
            }
        }

        int liveEntryId(int playerNo)
        {
            ftManager* mgr = g_ftManager;
            if (!game::isRam(mgr) || playerNo < 0 || playerNo >= game::kMaxFighters)
            {
                return -1;
            }
            int id = mgr->getEntryId(playerNo);
            return mgr->isValidEntryId(id) ? id : -1;
        }

        void updateSideCostumeBound(int side, u8 rosterIdx, u8& costumeVal, bool resetOnFighterChange)
        {
            const FighterSwapEntry* entry = fighterSwapAt(rosterIdx);
            int n = entry != NULL ? queryNumColors(entry->cssIcon) : 1;
            int maxIdx = n > 0 ? n - 1 : 0;
            main::setFighterSwapCostumeMax(side, maxIdx);
            if (s_net != NULL)
            {
                if (side == 0)
                {
                    s_net->debug.p1MaxCostume = static_cast<u8>(maxIdx);
                }
                else
                {
                    s_net->debug.cpuMaxCostume = static_cast<u8>(maxIdx);
                }
            }
            if (resetOnFighterChange && s_prevSelection[side] != 0xFF && s_prevSelection[side] != rosterIdx)
            {
                costumeVal = 0;
            }
            else
            {
                costumeVal = static_cast<u8>(fighterSwapClampCostume(costumeVal, n));
            }
            s_prevSelection[side] = rosterIdx;
        }

        void syncMenuToLive()
        {
            if (s_net == NULL)
            {
                return;
            }
            u8* players = modePlayers();
            int p1Slot = -1;
            int cpuSlot = -1;
            findTargetSlots(players, p1Slot, cpuSlot);
            s_net->debug.p1Slot = static_cast<s8>(p1Slot);
            s_net->debug.cpuSlot = static_cast<s8>(cpuSlot);
            ui::pages::FighterSwapMenuState* st = ui::pages::fighterSwapMenuState();
            int slots[2] = {p1Slot, cpuSlot};
            u8* fighters[2] = {&st->p1Fighter, &st->cpuFighter};
            u8* costumes[2] = {&st->p1Costume, &st->cpuCostume};
            for (int side = 0; side < 2; side++)
            {
                int slot = slots[side];
                if (players == NULL || slot < 0)
                {
                    *fighters[side] = 0;
                    *costumes[side] = 0;
                    s_prevSelection[side] = 0;
                    updateSideCostumeBound(side, 0, *costumes[side], false);
                    continue;
                }
                const u8* pd = players + slot * kPlayerSize;
                int idx = fighterSwapIndexOfKind(pd[kPlayerKind]);
                if (idx < 0)
                {
                    idx = 0;
                }
                *fighters[side] = static_cast<u8>(idx);
                const FighterSwapEntry* entry = fighterSwapAt(idx);
                int cosIdx = entry != NULL
                                 ? detectLiveCostumeIndex(entry->cssIcon, pd[kPlayerColorFile], pd[kPlayerColor])
                                 : 0;
                *costumes[side] = static_cast<u8>(cosIdx);
                s_prevSelection[side] = static_cast<u8>(idx);
                updateSideCostumeBound(side, static_cast<u8>(idx), *costumes[side], false);
            }
            s_synced = true;
        }

        const char* refuseNoticeText(u8 code)
        {
            switch (code)
            {
            case kRefuseDrill:
                return "Fighter Swap unavailable during a Skill Drill";
            case kRefuseSetBudget:
                return "Fighter Swap refused: max 3 fighter sets in match";
            case kRefuseLoading:
                return "Fighter Swap busy: another load is in progress";
            default:
                return "Fighter Swap unavailable in this match state";
            }
        }

        void recordRefusal(u8 code, const char* why)
        {
            if (s_net != NULL)
            {
                s_net->debug.refusalCount++;
                s_net->debug.lastRefusal = code;
            }
            TM_LOG("swap: refused: %s\n", why);
            storage::notice(storage::kNoticeError, "%s", refuseNoticeText(code));
            ui::menuSound(game::kSoundError);
        }

        bool canSwapNow()
        {
            game::MatchState m = game::matchState();
            return s_net != NULL && m.running && m.training && !drillActive() && !s_swapInProgress
                   && !savestate::loadingFiles();
        }
    }

    void fighterSwapAttach()
    {
        freeNetPtr(s_net);
        s_net = allocNetArray<FighterSwapNet>(1);
        s_costumeHelperOk = codeMatches(kGetNumColorAddr, kGetNumColorWords, 4)
                            && codeMatches(kGetColorFileAddr, kGetColorFileWords, 4);
        s_altCostumeOk = codeMatches(kAltCostumeMarkerAddr, kAltCostumeWords, 4);
        s_synced = false;
        s_swapInProgress = false;
        s_hadActiveScenario = false;
        s_prevSelection[0] = 0xFF;
        s_prevSelection[1] = 0xFF;
        s_settleFrames = 0;
        s_waitFrames = 0;
        s_lastGameFrame = 0;
        memset(ui::pages::fighterSwapMenuState(), 0, sizeof(ui::pages::FighterSwapMenuState));
        main::setFighterSwapCostumeMax(0, 0);
        main::setFighterSwapCostumeMax(1, 0);
        if (s_net == NULL)
        {
            main::setFighterSwapLabels(NULL, 0);
            return;
        }
        const FighterSwapEntry* roster = fighterSwapRoster();
        for (int i = 0; i < kFighterSwapCount; i++)
        {
            s_net->rosterNames[i] = roster[i].name;
        }
        s_net->debug.magic = kDebugMagic;
        s_net->debug.scratchAddr = reinterpret_cast<u32>(ui::pages::fighterSwapMenuState());
        s_net->debug.costumeHelperOk = s_costumeHelperOk ? 1 : 0;
        s_net->debug.altCostumeOk = s_altCostumeOk ? 1 : 0;
        s_net->debug.p1Slot = -1;
        s_net->debug.cpuSlot = -1;
        main::setFighterSwapLabels(s_net->rosterNames, kFighterSwapCount - 1);
        TM_LOG("swap: debug block %08x; costumes %s, alt %s\n", &s_net->debug, s_costumeHelperOk ? "ok" : "fallback",
               s_altCostumeOk ? "ok" : "off");
    }

    void fighterSwapDetach()
    {
        s_swapInProgress = false;
        s_hadActiveScenario = false;
        s_synced = false;
        main::setFighterSwapLabels(NULL, 0);
        main::setFighterSwapCostumeMax(0, 0);
        main::setFighterSwapCostumeMax(1, 0);
        freeNetPtr(s_net);
    }

    void fighterSwapTick(const game::MatchState& match)
    {
        if (s_net == NULL)
        {
            return;
        }
        if (!lab::menuOpen())
        {
            s_synced = false;
        }
        if (!s_swapInProgress)
        {
            s_net->debug.inProgress = 0;
            return;
        }
        s_net->debug.inProgress = 1;
        if (!match.running || !match.training)
        {
            s_swapInProgress = false;
            s_net->debug.inProgress = 0;
            return;
        }
        // Only advance swap completion tracking on simulated unpaused frames.
        if (match.paused || lab::menuOpen())
        {
            return;
        }
        u32 curFrame = game::frameCounter();
        if (curFrame == s_lastGameFrame)
        {
            return;
        }
        s_lastGameFrame = curFrame;
        s_waitFrames++;

        bool allReady = !savestate::loadingFiles();
        if (allReady)
        {
            game::FighterRef refs[game::kMaxFighters];
            int n = game::listFighters(refs, game::kMaxFighters);
            for (int p = 0; p < game::kMaxFighters; p++)
            {
                if (!s_net->pending[p].active)
                {
                    continue;
                }
                bool foundNew = false;
                for (int i = 0; i < n; i++)
                {
                    if (refs[i].playerNo == p && refs[i].entryId != s_net->pending[p].oldEntryId
                        && game::fighterModules(refs[i].fighter) != NULL)
                    {
                        foundNew = true;
                        break;
                    }
                }
                if (!foundNew)
                {
                    allReady = false;
                    break;
                }
            }
        }

        if (allReady)
        {
            if (++s_settleFrames >= kSwapSettleFrames)
            {
                s_swapInProgress = false;
                s_net->debug.inProgress = 0;
                s_net->debug.completedCount++;
                memset(s_net->pending, 0, sizeof(s_net->pending));
                TM_LOG("swap: complete after %u frames\n", s_waitFrames);
                if (s_hadActiveScenario)
                {
                    s_hadActiveScenario = false;
                    scenarioPresetsRestoreAfterSwap(s_net->savedScenarioMenu);
                }
            }
        }
        else
        {
            s_settleFrames = 0;
            if (s_waitFrames >= kSwapTimeoutFrames)
            {
                s_swapInProgress = false;
                s_net->debug.inProgress = 0;
                s_hadActiveScenario = false;
                memset(s_net->pending, 0, sizeof(s_net->pending));
                TM_LOG("swap: wait timed out after %u frames\n", s_waitFrames);
            }
        }
    }

    bool fighterSwapRow()
    {
        if (!canSwapNow())
        {
            return false;
        }
        if (!s_synced)
        {
            syncMenuToLive();
        }
        return true;
    }

    void fighterSwapChanged()
    {
        if (!s_synced)
        {
            return;
        }
        ui::pages::FighterSwapMenuState* st = ui::pages::fighterSwapMenuState();
        updateSideCostumeBound(0, st->p1Fighter, st->p1Costume, true);
        updateSideCostumeBound(1, st->cpuFighter, st->cpuCostume, true);
    }

    void fighterSwapApply()
    {
        if (s_net == NULL)
        {
            return;
        }
        FighterSwapDebugBlock& s_debug = s_net->debug;
        PendingSlot* s_pending = s_net->pending;
        if (drillActive())
        {
            recordRefusal(kRefuseDrill, "drill active");
            return;
        }
        game::MatchState m = game::matchState();
        if (!m.running || !m.training)
        {
            recordRefusal(kRefuseState, "not in a running Training match");
            return;
        }
        if (s_swapInProgress || savestate::loadingFiles())
        {
            recordRefusal(kRefuseLoading, "swap or file load already in progress");
            return;
        }
        u8* players = modePlayers();
        if (players == NULL)
        {
            recordRefusal(kRefuseState, "match setup unreadable");
            return;
        }

        int p1Slot = -1;
        int cpuSlot = -1;
        findTargetSlots(players, p1Slot, cpuSlot);
        s_debug.p1Slot = static_cast<s8>(p1Slot);
        s_debug.cpuSlot = static_cast<s8>(cpuSlot);
        if (p1Slot < 0 && cpuSlot < 0)
        {
            recordRefusal(kRefuseState, "no active fighters");
            return;
        }

        ui::pages::FighterSwapMenuState* st = ui::pages::fighterSwapMenuState();
        u8 targetKinds[game::kMaxFighters];
        u8 targetCostumes[game::kMaxFighters];
        u8 targetColorFiles[game::kMaxFighters];
        u8 targetIcons[game::kMaxFighters];
        bool active[game::kMaxFighters];
        bool wantChange[game::kMaxFighters];

        for (int p = 0; p < game::kMaxFighters; p++)
        {
            const u8* pd = players + p * kPlayerSize;
            u8 state = pd[kPlayerState];
            active[p] = (state == kStateHuman || state == kStateCpu);
            targetKinds[p] = pd[kPlayerKind];
            targetColorFiles[p] = pd[kPlayerColorFile];
            targetCostumes[p] = pd[kPlayerColor];
            targetIcons[p] = 0;
            wantChange[p] = false;
        }

        int sideSlots[2] = {p1Slot, cpuSlot};
        u8 sideFighters[2] = {st->p1Fighter, st->cpuFighter};
        u8 sideCostumes[2] = {st->p1Costume, st->cpuCostume};
        bool anyChange = false;
        bool cpuChanged = false;
        u8 newCpuKind = 0;

        for (int side = 0; side < 2; side++)
        {
            int p = sideSlots[side];
            if (p < 0 || !active[p])
            {
                continue;
            }
            const FighterSwapEntry* entry = fighterSwapAt(sideFighters[side]);
            if (entry == NULL)
            {
                continue;
            }
            int numColors = queryNumColors(entry->cssIcon);
            u8 cosIdx = static_cast<u8>(fighterSwapClampCostume(sideCostumes[side], numColors));
            u8 colorFile = queryColorFileNo(entry->cssIcon, cosIdx);
            const u8* pd = players + p * kPlayerSize;
            targetKinds[p] = entry->gmKind;
            targetCostumes[p] = cosIdx;
            targetColorFiles[p] = colorFile;
            targetIcons[p] = entry->cssIcon;
            if (entry->gmKind != pd[kPlayerKind] || colorFile != pd[kPlayerColorFile])
            {
                wantChange[p] = true;
                anyChange = true;
                if (side == 1)
                {
                    cpuChanged = true;
                    newCpuKind = entry->gmKind;
                }
            }
        }

        if (!anyChange)
        {
            s_debug.lastRefusal = kRefuseNoChange;
            TM_LOG("swap: no changes selected\n");
            lab::closeMenu();
            return;
        }

        // Enforce <= 3 distinct fighter sets while extra holds OverlayFighter4.
        if (!fighterSwapFitsBudget(targetKinds, active, game::kMaxFighters, true))
        {
            recordRefusal(kRefuseSetBudget, "would require 4th fighter set while extra holds OverlayFighter4");
            return;
        }

        // ---- 1. Pre-swap cleanup barrier across all subsystems ----
        const ui::pages::ScenarioMenuState* curScen = ui::pages::scenarioMenuState();
        s_hadActiveScenario = curScen->preset != ui::pages::kPresetOff;
        if (s_hadActiveScenario)
        {
            s_net->savedScenarioMenu = *curScen;
        }
        scenarioPresetsPrepareSwap();
        record::prepareSwap();
        savestate::resetForSwap();
        cpu::optionsPrepareSwap(cpuChanged, cpuChanged ? fighterSwapCounterFtKind(newCpuKind) : -1);
        lab::prepareSwap();
        diReset();
        comboTrainerReset();
        coreDrillEnginesSavestateReset();
        hoverPsResetTransient();
        pplusTechReset();
        heatmapReset();
        adaptiveCpuReset();
        frameDataReset();
        for (int slot = 0; slot < cpu::kMaxSlots; slot++)
        {
            pplusRecoveryReset(slot);
        }
        // Detach/restore Peach FitPeach.pac odds & Dedede FitDedede.pac Waddle
        // weights BEFORE stLoaderPlayer frees the outgoing fighter's .pac files.
        characterRngPrepareSwap();

        // ---- 2. Write gmPlayerInitData / gmSelCharData for every changed player ----
        memset(s_net->pending, 0, sizeof(s_net->pending));
        u8* sel = selCharData();
        for (int p = 0; p < game::kMaxFighters; p++)
        {
            if (!wantChange[p])
            {
                continue;
            }
            u8* pd = players + p * kPlayerSize;
            u8 oldKind = pd[kPlayerKind];
            s_pending[p].active = true;
            s_pending[p].oldEntryId = liveEntryId(p);
            s_pending[p].wantKind = targetKinds[p];
            s_pending[p].wantColorFile = targetColorFiles[p];

            pd[kPlayerKind] = targetKinds[p];
            pd[kPlayerColorFile] = targetColorFiles[p];
            pd[kPlayerColor] = targetCostumes[p];

            if (sel != NULL)
            {
                // Clear P+ AltZ/AltR word so AltCostume.asm does not overwrite
                // m_colorFileNo to 61/62 on the next frame.
                if (s_altCostumeOk)
                {
                    u32* altWord = reinterpret_cast<u32*>(sel + kSelAltCostume + p * kPlayerSize);
                    if (game::isRam(altWord))
                    {
                        *altWord = 0;
                    }
                }
                // Keep gmSelCharData (+0xB8 players & +0x8 CSS icon words) in
                // sync for recording exports and returning to CSS.
                u8* sp = sel + kSelPlayers + p * kPlayerSize;
                sp[kPlayerKind] = targetKinds[p];
                sp[kPlayerColorFile] = targetCostumes[p];
                sp[kPlayerColor] = targetCostumes[p];
                *reinterpret_cast<u32*>(sel + kSelIcons + 4 * p) = static_cast<u32>(targetIcons[p]) << 8;
                sel[kSelHasChoice] = 1;
            }

            if (p == p1Slot)
            {
                s_debug.lastP1Kind = targetKinds[p];
                s_debug.lastP1ColorFile = targetColorFiles[p];
            }
            else if (p == cpuSlot)
            {
                s_debug.lastCpuKind = targetKinds[p];
                s_debug.lastCpuColorFile = targetColorFiles[p];
            }

            TM_LOG("swap: P%d kind %u->%u costume %u (file %u, icon 0x%02x)\n", p + 1, oldKind, targetKinds[p],
                   targetCostumes[p], targetColorFiles[p], targetIcons[p]);
        }

        s_swapInProgress = true;
        s_settleFrames = 0;
        s_waitFrames = 0;
        s_lastGameFrame = game::frameCounter();
        s_synced = false;
        s_debug.inProgress = 1;
        s_debug.lastRefusal = kRefuseNone;
        s_debug.swapCount++;

        // Close the lab menu cleanly so stLoaderPlayer begins the swap(s) at once.
        lab::closeMenu();
    }

    bool fighterSwapBusy()
    {
        return s_swapInProgress;
    }
}
}
