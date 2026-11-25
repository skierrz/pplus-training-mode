#include "extra/scenario_presets.h"

#include <ft/fighter.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/ground/so_ground_shape_impl.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/status/so_status_module_impl.h>
#include <so/stop/so_stop_module_impl.h>
#include <string.h>

#include "extra/combo_trainer.h"
#include "extra/drill_core_engines.h"
#include "extra/drill_hover_ps_engines.h"
#include "extra/drill_hud.h"
#include "extra/drill_pplus_tech_engines.h"
#include "extra/net_alloc.h"
#include "extra/pplus_recovery.h"
#include "extra/scenario_presets_core.h"
#include "extra/stage_geom.h"
#include "main/cpu/options.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/game/tracker.h"
#include "main/lab.h"
#include "main/services.h"
#include "main/settings.h"
#include "main/storage/notice.h"
#include "main/ui/pages/scenarios.h"
#include "shared/drill_session.h"
#include "shared/game/frame.h"
#include "shared/game/mem.h"
#include "shared/log.h"
#include "shared/version.h"

namespace tm {
namespace extra {
    enum {
        kScenarioDebugMagic = 0x544D5343, // "TMSC"
        kStatusPassId = 0x72              // Fighter::Status::Pass (ft/fighter.h:134)
    };

    // Compact RAM-inspectable telemetry block for live Dolphin tests.
    struct ScenarioDebugBlock {
        u32 magic;                // +0x00: kScenarioDebugMagic
        u32 frame;                // +0x04
        u32 scratchAddr;          // +0x08: ScenarioMenuState in ui::menuScratch()
        u32 resetCount;           // +0x0C
        u32 completionCount;      // +0x10
        u32 sdiHitCount;          // +0x14
        u32 sdiHitlagFrames;      // +0x18
        u32 grabEscapeFrames;     // +0x1C
        u32 shieldHitCount;       // +0x20
        float hmnSpawnX;          // +0x24
        float hmnSpawnY;          // +0x28
        float cpuSpawnX;          // +0x2C
        float cpuSpawnY;          // +0x30
        float sdiDispX;           // +0x34
        float sdiDispY;           // +0x38
        u8 activePreset;          // +0x3C
        u8 hasPlatform;           // +0x3D
        u8 snapshotActive;        // +0x3E
        u8 sdiEscaped;            // +0x3F
        u8 lastCorrectWritten[2]; // +0x40..0x41 (0=HMN, 1=CPU)
        u8 observedCorrect[2];    // +0x42..0x43 (0=HMN, 1=CPU)
        u8 restorePending[2];     // +0x44..0x45
        u8 sawShieldDrop;         // +0x46
        u8 sawWallTech;           // +0x47
        u8 sawSlideOff;           // +0x48
        u8 sawGrabEscape;         // +0x49
        u8 autoReset;             // +0x4A
        u8 side;                  // +0x4B
        u8 multiHitMove;          // +0x4C
        u8 resolvedMultiHit;      // +0x4D
        u8 resolvedPlatformSlot;  // +0x4E
        u8 resolvedLedgetechAtk;  // +0x4F
    };
    TM_STATIC_ASSERT(sizeof(ScenarioDebugBlock) == 0x50, tm_scenario_debug_block_size);

    struct ScenarioNet {
        ScenarioState state;
        ScenarioCpuState cpuState;
        ScenarioDebugBlock debug;
    };

    static ScenarioNet* s_net = NULL;
    static s8 s_placedPlayer[2] = {-1, -1};
    static bool s_wasPaused = false;
    static bool s_resumeFromPause = false;

    static ScenarioConfig readMenuConfig()
    {
        const ui::pages::ScenarioMenuState* m = ui::pages::scenarioMenuState();
        ScenarioConfig raw;
        raw.preset = m->preset;
        raw.autoReset = m->autoReset;
        raw.side = m->side;
        raw.multiHitMove = m->multiHitMove;
        raw.oosAction = m->oosAction;
        raw.ledgetechSpawn = m->ledgetechSpawn;
        raw.ledgetechDj = m->ledgetechDj;
        raw.ledgetechAttack = m->ledgetechAttack;
        raw.shieldDropPlat = m->shieldDropPlat;
        raw.shieldDropSpacing = m->shieldDropSpacing;
        raw.shieldDropFacing = m->shieldDropFacing;
        return sanitizeScenarioConfig(raw);
    }

    static void initScratchDefaults()
    {
        ui::pages::ScenarioMenuState* m = ui::pages::scenarioMenuState();
        memset(m, 0, sizeof(*m));
        m->preset = kScenarioOff;
        m->autoReset = 1;
        m->side = kScenarioSideRight;
        m->multiHitMove = kMultiHitAuto;
        m->oosAction = kOosGrab;
        m->ledgetechSpawn = kLtSpawnCloseLow;
        m->ledgetechDj = kLtDjAvailable;
        m->ledgetechAttack = kLtAtkDtilt;
        m->shieldDropPlat = kSdPlatLeft;
        m->shieldDropSpacing = kSdSpaceRandom;
        m->shieldDropFacing = kSdFaceToward;
    }

    static void syncDebugBlock(Fighter* hmn, Fighter* cpu)
    {
        if (s_net == NULL)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioDebugBlock& s_debug = s_net->debug;
        ScenarioConfig cfg = readMenuConfig();
        s_debug.magic = kScenarioDebugMagic;
        s_debug.frame = game::frameCounter();
        s_debug.scratchAddr = reinterpret_cast<u32>(ui::pages::scenarioMenuState());
        s_debug.resetCount = s_state.resetCount;
        s_debug.completionCount = s_state.completionCount;
        s_debug.sdiHitCount = s_state.sdiHitCount;
        s_debug.sdiHitlagFrames = s_state.sdiHitlagFrames;
        s_debug.grabEscapeFrames = s_state.grabEscapeFrames;
        s_debug.shieldHitCount = s_state.shieldHitCount;
        s_debug.hmnSpawnX = s_state.hmnSpawnX;
        s_debug.hmnSpawnY = s_state.hmnSpawnY;
        s_debug.cpuSpawnX = s_state.cpuSpawnX;
        s_debug.cpuSpawnY = s_state.cpuSpawnY;
        s_debug.sdiDispX = s_state.sdiDispX;
        s_debug.sdiDispY = s_state.sdiDispY;
        s_debug.activePreset = s_state.activePreset;
        s_debug.hasPlatform = s_state.hasPlatform ? 1 : 0;
        s_debug.snapshotActive = s_state.snapshotActive ? 1 : 0;
        s_debug.sdiEscaped = s_state.sdiEscaped ? 1 : 0;
        s_debug.lastCorrectWritten[0] = s_state.lastCorrectWritten[0];
        s_debug.lastCorrectWritten[1] = s_state.lastCorrectWritten[1];
        int hObs = hmn != NULL ? getFighterCorrect(hmn) : -1;
        int cObs = cpu != NULL ? getFighterCorrect(cpu) : -1;
        s_debug.observedCorrect[0] = hObs >= 0 ? static_cast<u8>(hObs) : 0xFF;
        s_debug.observedCorrect[1] = cObs >= 0 ? static_cast<u8>(cObs) : 0xFF;
        s_debug.restorePending[0] = s_state.restoreCorrect[0] ? 1 : 0;
        s_debug.restorePending[1] = s_state.restoreCorrect[1] ? 1 : 0;
        s_debug.sawShieldDrop = s_state.sawShieldDrop ? 1 : 0;
        s_debug.sawWallTech = s_state.sawWallTech ? 1 : 0;
        s_debug.sawSlideOff = s_state.sawSlideOff ? 1 : 0;
        s_debug.sawGrabEscape = s_state.sawGrabEscape ? 1 : 0;
        s_debug.autoReset = cfg.autoReset;
        s_debug.side = cfg.side;
        s_debug.multiHitMove = cfg.multiHitMove;
        s_debug.resolvedMultiHit = s_state.resolvedMultiHit;
        s_debug.resolvedPlatformSlot = s_state.resolvedPlatformSlot;
        s_debug.resolvedLedgetechAtk = s_state.resolvedLedgetechAttack;
    }

    static bool sawIntraFrameStatus(int playerNo, int targetStatus)
    {
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* t = game::tracked(i);
            if (t == NULL || t->playerNo != playerNo)
            {
                continue;
            }
            if (t->cur.status == targetStatus)
            {
                return true;
            }
            for (int k = 0; k < t->eventCount; k++)
            {
                if (t->events[k].status == targetStatus)
                {
                    return true;
                }
            }
        }
        return false;
    }

    static bool findPlayerRef(const game::FighterRef* refs, int n, int playerNo, game::FighterRef& out)
    {
        for (int i = 0; i < n; i++)
        {
            if (refs[i].playerNo == playerNo)
            {
                out = refs[i];
                return true;
            }
        }
        return false;
    }

    static void applyPercentsIfChanged(u16 beforeHmn, u16 beforeCpu, const main::Settings& s)
    {
        if (s.general.playerPercent != beforeHmn)
        {
            lab::applyPercent(main::kSideHuman);
        }
        if (s.cpu.percent != beforeCpu)
        {
            lab::applyPercent(main::kSideCpu);
        }
    }

    void scenarioPresetsAttach()
    {
        freeNetPtr(s_net);
        s_net = allocNetArray<ScenarioNet>(1);
        s_placedPlayer[0] = -1;
        s_placedPlayer[1] = -1;
        s_wasPaused = false;
        s_resumeFromPause = false;
        initScratchDefaults();
        if (s_net == NULL)
        {
            return;
        }
        scenarioStateInit(s_net->state);
        scenarioCpuStateReset(s_net->cpuState);
        drills::Session* sess = drills::sessionIn(main::services().settings());
        if (sess != NULL && sess->pendingScenario >= kScenarioComboLab && sess->pendingScenario < kScenarioCount)
        {
            u8 p = sess->pendingScenario;
            sess->pendingScenario = 0;
            ui::pages::scenarioMenuState()->preset = p;
            TM_LOG("scenarios: armed preset %u from menu session\n", p);
        }
        memset(&s_net->debug, 0, sizeof(s_net->debug));
        syncDebugBlock(NULL, NULL);
        TM_LOG("scenarios: debug block %08x\n", &s_net->debug);
    }

    // Clears lingering respawn or ledge intangibility/invincibility when a
    // scenario warps a fighter into a fresh rep (soCollisionHitModule).
    static void clearWarpIntang(const game::FighterRef& ref)
    {
        Fighter* pair[2] = {ref.fighter, (ref.partner != NULL && ref.partner != ref.fighter) ? ref.partner : NULL};
        for (int i = 0; i < 2; i++)
        {
            if (pair[i] == NULL)
            {
                continue;
            }
            const soModuleEnumeration* m = game::fighterModules(pair[i]);
            if (m != NULL && game::isRam(m->m_collisionHitModule))
            {
                m->m_collisionHitModule->setXluFrameGlobal(0, 0);
                m->m_collisionHitModule->setInvincibleFrameGlobal(0, true, 0);
                m->m_collisionHitModule->resetStatusGlobal(0);
            }
        }
    }

    static void finishStandingWarp(const game::FighterRef& ref, bool setNoclip)
    {
        Fighter* pair[2] = {ref.fighter, (ref.partner != NULL && ref.partner != ref.fighter) ? ref.partner : NULL};
        for (int i = 0; i < 2; i++)
        {
            if (pair[i] == NULL)
            {
                continue;
            }
            const soModuleEnumeration* m = game::fighterModules(pair[i]);
            if (m == NULL || !game::isRam(m->m_groundModule) || !game::isRam(m->m_statusModule)
                || !game::isRam(pair[i]->m_moduleAccesser))
            {
                continue;
            }
            m->m_groundModule->setCorrect(soGroundShapeImpl::Correct_Ground_Cliff_Stop, 0);
            if (m->m_groundModule->attachGround(0))
            {
                m->m_groundModule->setStGroundForce(0);
            }
            m->m_statusModule->changeStatusForce(game::kStatusWait, pair[i]->m_moduleAccesser);
            m->m_groundModule->setCorrect(
                setNoclip ? soGroundShapeImpl::Correct_None : soGroundShapeImpl::Correct_Ground_Cliff_Stop, 0);
        }
    }

    void scenarioPresetsDetach()
    {
        if (s_net != NULL)
        {
            main::Settings& s = main::settings();
            u16 prevHmn = s.general.playerPercent;
            u16 prevCpu = s.cpu.percent;
            restoreScenarioSnapshot(s_net->state, s.cpu.options, s.comboTrainer, &s.general, &s.cpu);
            applyPercentsIfChanged(prevHmn, prevCpu, s);
            s_net->state.activePreset = kScenarioOff;
            freeNetPtr(s_net);
        }
        s_placedPlayer[0] = -1;
        s_placedPlayer[1] = -1;
        s_wasPaused = false;
        s_resumeFromPause = false;
    }

    void scenarioPresetsPrepareSwap()
    {
        if (s_net == NULL)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        ui::pages::ScenarioMenuState* m = ui::pages::scenarioMenuState();
        bool wasActive = s_state.activePreset != kScenarioOff || m->preset != kScenarioOff || s_state.snapshotActive;
        if (s_state.restoreCorrect[0] || s_state.restoreCorrect[1])
        {
            game::FighterRef refs[game::kMaxFighters];
            int n = game::listFighters(refs, game::kMaxFighters);
            for (int slot = 0; slot < 2; slot++)
            {
                if (!s_state.restoreCorrect[slot])
                {
                    continue;
                }
                game::FighterRef target;
                if (s_placedPlayer[slot] >= 0 && findPlayerRef(refs, n, s_placedPlayer[slot], target))
                {
                    setFighterCorrect(target, s_state.restoreCorrectKind[slot]);
                }
            }
        }
        m->preset = kScenarioOff;
        main::Settings& s = main::settings();
        u16 prevHmn = s.general.playerPercent;
        u16 prevCpu = s.cpu.percent;
        applyScenarioConfig(s_state, readMenuConfig(), s.cpu.options, s.comboTrainer, &s.general, &s.cpu);
        applyPercentsIfChanged(prevHmn, prevCpu, s);
        scenarioTransientReset(s_state);
        scenarioCpuStateReset(s_cpuState);
        s_placedPlayer[0] = -1;
        s_placedPlayer[1] = -1;
        s_wasPaused = false;
        s_resumeFromPause = false;
        syncDebugBlock(NULL, NULL);
        if (wasActive)
        {
            TM_LOG("scenarios: deactivated for fighter swap\n");
        }
    }

    void scenarioPresetsRestoreAfterSwap(const ui::pages::ScenarioMenuState& saved)
    {
        if (s_net == NULL || saved.preset == kScenarioOff || saved.preset >= kScenarioCount)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        *ui::pages::scenarioMenuState() = saved;
        main::Settings& s = main::settings();
        u16 prevHmn = s.general.playerPercent;
        u16 prevCpu = s.cpu.percent;
        applyScenarioConfig(s_state, readMenuConfig(), s.cpu.options, s.comboTrainer, &s.general, &s.cpu);
        applyPercentsIfChanged(prevHmn, prevCpu, s);
        requestScenarioManualReset(s_state);
        scenarioCpuStateReset(s_cpuState);
        syncDebugBlock(NULL, NULL);
        TM_LOG("scenarios: restored preset %u after fighter swap\n", saved.preset);
    }

    void scenarioPresetsOnRender(const game::MatchState& match)
    {
        if (s_net == NULL || !match.running || game::trainingEnding())
        {
            s_wasPaused = false;
            s_resumeFromPause = false;
            return;
        }
        if (s_wasPaused && !match.paused && s_net->state.activePreset != kScenarioOff)
        {
            s_resumeFromPause = true;
        }
        s_wasPaused = match.paused;
    }

    static void cycleShieldDropPlatform()
    {
        if (s_net == NULL)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        ui::pages::ScenarioMenuState* m = ui::pages::scenarioMenuState();
        if (s_state.activePreset != kScenarioShieldDrop && m->preset != kScenarioShieldDrop)
        {
            return;
        }
        StageGeometry geom;
        liveGeometry(geom, true, true);
        int slotIdx[3];
        classifyPlatforms(geom, slotIdx);
        u8 avail[3];
        int count = 0;
        for (int s = 0; s < 3; s++)
        {
            if (slotIdx[s] >= 0)
            {
                avail[count++] = static_cast<u8>(s);
            }
        }
        if (count == 0)
        {
            storage::notice(storage::kNoticeWarn, "No platforms on this stage");
            return;
        }
        u8 cur = m->shieldDropPlat < 3 ? m->shieldDropPlat : s_state.resolvedPlatformSlot;
        u8 nextSlot = avail[0];
        for (int i = 0; i < count; i++)
        {
            if (avail[i] > cur)
            {
                nextSlot = avail[i];
                break;
            }
        }
        m->shieldDropPlat = nextSlot;
        s_state.lastShieldDropPlat = nextSlot;
        requestScenarioManualReset(s_state);
        scenarioCpuStateReset(s_cpuState);
        static const char* const kSlotNames[3] = {"Left", "Right", "Top"};
        storage::notice(storage::kNoticeOk, "Platform: %s", kSlotNames[nextSlot]);
        TM_LOG("scenarios: shield drop platform -> %s\n", kSlotNames[nextSlot]);
    }

    void scenarioPresetsCall(int call)
    {
        if (s_net == NULL)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        main::Settings& s = main::settings();
        switch (call)
        {
        case main::kScenarioChanged:
        {
            u16 prevHmn = s.general.playerPercent;
            u16 prevCpu = s.cpu.percent;
            applyScenarioConfig(s_state, readMenuConfig(), s.cpu.options, s.comboTrainer, &s.general, &s.cpu);
            applyPercentsIfChanged(prevHmn, prevCpu, s);
            scenarioCpuStateReset(s_cpuState);
            break;
        }
        case main::kScenarioResetNow:
            requestScenarioManualReset(s_state);
            scenarioCpuStateReset(s_cpuState);
            break;
        case main::kScenarioSavestateLoaded:
            scenarioTransientReset(s_state);
            scenarioCpuStateReset(s_cpuState);
            stageGeomReset();
            break;
        case main::kScenarioSettingsReset:
            discardScenarioSnapshot(s_state);
            scenarioCpuStateReset(s_cpuState);
            initScratchDefaults();
            break;
        case main::kScenarioCyclePlatform:
            cycleShieldDropPlatform();
            break;
        default:
            break;
        }
        syncDebugBlock(NULL, NULL);
    }

    int scenarioPresetsEdgeguardSide()
    {
        if (s_net == NULL || s_net->state.activePreset != kScenarioEdgeguard)
        {
            return kScenarioSideRandom;
        }
        ScenarioConfig cfg = readMenuConfig();
        return cfg.side < kScenarioSideCount ? static_cast<int>(cfg.side) : static_cast<int>(kScenarioSideRandom);
    }

    void scenarioPresetsFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        ScenarioDebugBlock& s_debug = s_net->debug;
        ScenarioConfig cfg = readMenuConfig();
        // Fast path when Off: zero per-frame fighter reads, writes, or overrides.
        if (cfg.preset == kScenarioOff && s_state.activePreset == kScenarioOff && !s_state.restoreCorrect[0]
            && !s_state.restoreCorrect[1])
        {
            s_debug.frame = game::frameCounter();
            s_debug.activePreset = kScenarioOff;
            s_debug.snapshotActive = s_state.snapshotActive ? 1 : 0;
            s_debug.autoReset = cfg.autoReset;
            s_debug.side = cfg.side;
            s_debug.multiHitMove = cfg.multiHitMove;
            s_resumeFromPause = false;
            return;
        }

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        game::FighterRef hmnRef;
        game::FighterRef cpuRef;
        bool haveHmn = false;
        bool haveCpu = false;
        for (int i = 0; i < n; i++)
        {
            if (!refs[i].cpu && !haveHmn)
            {
                hmnRef = refs[i];
                haveHmn = true;
            }
            else if (refs[i].cpu && !haveCpu)
            {
                cpuRef = refs[i];
                haveCpu = true;
            }
        }
        if (!haveHmn || !haveCpu)
        {
            main::Settings& s = main::settings();
            u16 prevHmn = s.general.playerPercent;
            u16 prevCpu = s.cpu.percent;
            applyScenarioConfig(s_state, cfg, s.cpu.options, s.comboTrainer, &s.general, &s.cpu);
            applyPercentsIfChanged(prevHmn, prevCpu, s);
            syncDebugBlock(haveHmn ? hmnRef.fighter : NULL, haveCpu ? cpuRef.fighter : NULL);
            return;
        }

        // Returning from Brawl Training pause: re-apply preset overrides,
        // re-claim CPU brain, and trigger a clean scenario reset!
        if (s_resumeFromPause)
        {
            s_resumeFromPause = false;
            if (s_state.activePreset != kScenarioOff)
            {
                main::Settings& s = main::settings();
                reapplyActiveScenarioOverrides(s_state, cfg, s.cpu.options, s.comboTrainer, &s.general, &s.cpu);
                lab::applyPercent(main::kSideHuman);
                lab::applyPercent(main::kSideCpu);
                requestScenarioManualReset(s_state);
                scenarioCpuStateReset(s_cpuState);
                TM_LOG("scenarios: resumed preset %u after pause\n", s_state.activePreset);
            }
        }

        game::FighterState hmnFs;
        game::FighterState cpuFs;
        if (!game::readState(hmnRef, hmnFs) || !game::readState(cpuRef, cpuFs))
        {
            return;
        }

        const soModuleEnumeration* hm = game::fighterModules(hmnRef.fighter);
        const soModuleEnumeration* cm = game::fighterModules(cpuRef.fighter);
        if (hm == NULL || cm == NULL)
        {
            return;
        }

        cpu::View hmnView;
        cpu::View cpuView;
        bool haveHmnView = cpu::readView(hmnRef.fighter, hmnView);
        bool haveCpuView = cpu::readView(cpuRef.fighter, cpuView);

        ScenarioStepInput in;
        memset(&in, 0, sizeof(in));
        in.hmnStatus = hmnFs.status;
        in.hmnHitlag = game::isRam(hm->m_stopModule) ? hm->m_stopModule->getHitStopRealFrame() : 0;
        if (in.hmnHitlag < 0)
        {
            in.hmnHitlag = 0;
        }
        in.hmnDamageHitlag = in.hmnHitlag > 0 && game::isRam(hm->m_stopModule) && hm->m_stopModule->isDamage();
        in.hmnHitstun = hmnFs.hitstun;
        in.hmnShieldStun = hmnFs.shieldStun;
        in.hmnX = hmnFs.x;
        in.hmnY = hmnFs.y;
        in.hmnAir = game::isRam(hm->m_situationModule) && hm->m_situationModule->getKind() == Situation_Air;
        in.hmnOutOfPlay = game::outOfPlay(hmnFs.status, true);
        in.hmnActionable = haveHmnView ? cpu::canAct(hmnView) : (in.hmnHitlag == 0 && in.hmnHitstun == 0);
        in.hmnEnteredPass = hmnFs.status == kStatusPassId || sawIntraFrameStatus(hmnRef.playerNo, kStatusPassId);

        in.cpuStatus = cpuFs.status;
        in.cpuHitlag = game::isRam(cm->m_stopModule) ? cm->m_stopModule->getHitStopRealFrame() : 0;
        if (in.cpuHitlag < 0)
        {
            in.cpuHitlag = 0;
        }
        in.cpuHitstun = cpuFs.hitstun;
        in.cpuShieldStun = cpuFs.shieldStun;
        in.cpuFtKind = game::fighterKind(cpuRef.entryId, false);
        in.cpuX = cpuFs.x;
        in.cpuY = cpuFs.y;
        in.cpuAir = game::isRam(cm->m_situationModule) && cm->m_situationModule->getKind() == Situation_Air;
        in.cpuOutOfPlay = game::outOfPlay(cpuFs.status, true);
        in.cpuActionable = haveCpuView ? cpu::canAct(cpuView) : (in.cpuHitlag == 0 && in.cpuHitstun == 0);

        bool needFreshGeom = s_state.pendingReset || cfg.preset != s_state.activePreset || cfg.side != s_state.lastSide
                             || cfg.multiHitMove != s_state.lastMultiHitMove
                             || cfg.shieldDropPlat != s_state.lastShieldDropPlat;
        bool needPlatforms = cfg.preset == kScenarioShieldDrop || cfg.preset == kScenarioSlideOff
                             || s_state.activePreset == kScenarioShieldDrop
                             || s_state.activePreset == kScenarioSlideOff;
        liveGeometry(in.geom, needFreshGeom, needPlatforms);

        // Stir RNG with live frame counter so random choices vary cleanly across warps.
        s_state.rng ^= game::frameCounter() * 0x9E3779B9u;
        if (s_state.rng == 0)
        {
            s_state.rng = 0x51A7C0DEu;
        }

        ScenarioStepAction act;
        main::Settings& s = main::settings();
        stepScenario(s_state, cfg, in, s.cpu.options, s.comboTrainer, act, &s.general, &s.cpu);
        if (act.applyHmnPercent)
        {
            lab::applyPercent(main::kSideHuman);
        }
        if (act.applyCpuPercent)
        {
            lab::applyPercent(main::kSideCpu);
        }

        // Apply 1-frame noclip restores to the exact placed fighter(s).
        for (int slot = 0; slot < 2; slot++)
        {
            if (act.restoreCorrect[slot])
            {
                game::FighterRef target = slot == 0 ? hmnRef : cpuRef;
                if (s_placedPlayer[slot] >= 0)
                {
                    findPlayerRef(refs, n, s_placedPlayer[slot], target);
                }
                setFighterCorrect(target, act.restoreCorrectKind[slot]);
                if (act.restoreCorrectKind[slot] == kCorrectAir)
                {
                    bool spendDj = slot == 0 && cfg.preset == kScenarioLedgetech && cfg.ledgetechDj == kLtDjSpent;
                    refreshAirResources(target, spendDj);
                }
            }
        }

        // Cancel spurious Landing lag (0x16..0x19) if Brawl snaps a fighter
        // warped from air/Rebirth onto a standing floor right after the 1-frame
        // noclip restore (frames 2..3 post-warp).
        if (!act.warpHmn && !act.warpCpu && s_state.activePreset != kScenarioOff && s_state.sinceResetFrames >= 2
            && s_state.sinceResetFrames <= 3)
        {
            for (int slot = 0; slot < 2; slot++)
            {
                if (s_state.restoreCorrectKind[slot] != kCorrectGroundCliffStop)
                {
                    continue;
                }
                game::FighterRef target = slot == 0 ? hmnRef : cpuRef;
                if (s_placedPlayer[slot] >= 0)
                {
                    findPlayerRef(refs, n, s_placedPlayer[slot], target);
                }
                Fighter* pair[2] = {target.fighter,
                                    (target.partner != NULL && target.partner != target.fighter) ? target.partner : NULL};
                for (int p = 0; p < 2; p++)
                {
                    if (pair[p] == NULL)
                    {
                        continue;
                    }
                    const soModuleEnumeration* tm = game::fighterModules(pair[p]);
                    int st = tm != NULL ? game::fighterStatus(tm) : -1;
                    if (st >= game::kStatusLanding && st <= game::kStatusLandingFallSpecial
                        && game::isRam(tm->m_statusModule) && game::isRam(pair[p]->m_moduleAccesser))
                    {
                        tm->m_statusModule->changeStatusForce(game::kStatusWait, pair[p]->m_moduleAccesser);
                    }
                }
            }
        }

        if (act.warpHmn)
        {
            if (act.spawns.hmnFalling)
            {
                placeFalling(hmnRef, act.spawns.hmnX, act.spawns.hmnY, act.setNoclip[0], -1.0f, act.spawns.hmnLr);
                if (act.refreshHmnAir)
                {
                    refreshAirResources(hmnRef, act.hmnSpendDj);
                }
            }
            else
            {
                placeStanding(hmnRef, act.spawns.hmnX, act.spawns.hmnY, act.setNoclip[0], -1.0f, act.spawns.hmnLr);
                finishStandingWarp(hmnRef, act.setNoclip[0]);
            }
            clearWarpIntang(hmnRef);
            s_placedPlayer[0] = static_cast<s8>(hmnRef.playerNo);
        }
        if (act.warpCpu)
        {
            if (act.spawns.cpuFalling)
            {
                placeFalling(cpuRef, act.spawns.cpuX, act.spawns.cpuY, act.setNoclip[1], -1.0f, act.spawns.cpuLr);
                if (act.refreshCpuAir)
                {
                    refreshAirResources(cpuRef, false);
                }
            }
            else
            {
                placeStanding(cpuRef, act.spawns.cpuX, act.spawns.cpuY, act.setNoclip[1], -1.0f, act.spawns.cpuLr);
                finishStandingWarp(cpuRef, act.setNoclip[1]);
            }
            clearWarpIntang(cpuRef);
            s_placedPlayer[1] = static_cast<s8>(cpuRef.playerNo);
        }
        if (act.resetComboTrainer)
        {
            comboTrainerReset();
            coreDrillEnginesSavestateReset();
            hoverPsResetTransient();
            pplusTechReset();
            cpu::optionsPrepareSwap(false, 0);
            for (int i = 0; i < n; i++)
            {
                if (refs[i].cpu && refs[i].playerNo >= 0 && refs[i].playerNo < game::kMaxFighters)
                {
                    pplusRecoveryReset(refs[i].playerNo);
                }
            }
            scenarioCpuStateReset(s_cpuState);
        }

        syncDebugBlock(hmnRef.fighter, cpuRef.fighter);
    }

    bool scenarioPresetsCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out,
                                u32 lastButtons)
    {
        if (s_net == NULL || !who.cpu || f == NULL)
        {
            return false;
        }
        ScenarioState& s_state = s_net->state;
        ScenarioCpuState& s_cpuState = s_net->cpuState;
        const bool psDrill = (s_state.activePreset == kScenarioOff)
                             && (drillHudActiveId() == drills::kDrillPowershield)
                             && !drillHudModalOpen();
        if (!psDrill)
        {
            if (s_state.activePreset != kScenarioMultiHitSdi && s_state.activePreset != kScenarioLedgetech
                && s_state.activePreset != kScenarioSlideOff)
            {
                return false;
            }
        }

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        game::FighterRef hmnRef;
        bool haveHmn = false;
        for (int i = 0; i < n; i++)
        {
            if (!refs[i].cpu)
            {
                hmnRef = refs[i];
                haveHmn = true;
                break;
            }
        }
        if (!haveHmn)
        {
            return false;
        }
        game::FighterState hmnFs;
        const soModuleEnumeration* hm = game::fighterModules(hmnRef.fighter);
        if (hm == NULL || !game::readState(hmnRef, hmnFs))
        {
            return false;
        }

        ScenarioConfig cfg = readMenuConfig();
        ScenarioCpuInput in;
        in.preset = s_state.activePreset;
        in.multiHitMove = cfg.multiHitMove;
        in.ledgetechAttack = s_state.resolvedLedgetechAttack;
        in.cpuFtKind = game::fighterKind(who.entryId, who.sub);
        in.cpuStatus = v.status;
        in.cpuAir = v.air;
        in.cpuCanAct = cpu::canAct(v);
        in.cpuTakeoffNext = cpu::takeoffNext(v);
        in.cpuX = v.x;
        in.cpuY = v.y;
        in.cpuVx = v.vx;
        in.cpuVy = v.vy;
        in.cpuLr = v.lr;
        in.hmnX = hmnFs.x;
        in.hmnY = hmnFs.y;
        in.hmnAir = game::isRam(hm->m_situationModule) && hm->m_situationModule->getKind() == Situation_Air;
        in.hmnHitstun = hmnFs.hitstun;
        in.hmnOutOfPlay = game::outOfPlay(hmnFs.status, true);
        in.cpuOutOfPlay = game::outOfPlay(v.status, true);
        in.lastButtons = lastButtons;

        ScenarioCpuOutput cpuOut;
        if (psDrill)
        {
            stepPowershieldDrillCpu(in, s_cpuState, cpuOut);
        }
        else
        {
            stepScenarioCpu(in, s_cpuState, cpuOut);
        }
        if (!cpuOut.active)
        {
            return false;
        }
        out.buttons = cpuOut.buttons;
        out.x = cpuOut.stickX;
        out.y = cpuOut.stickY;
        return true;
    }

    void scenarioPresetsSavedView(main::Settings& out)
    {
        if (s_net == NULL)
        {
            return;
        }
        projectScenarioSavedView(s_net->state, out.cpu.options, out.comboTrainer, &out.general, &out.cpu);
    }
}
}
