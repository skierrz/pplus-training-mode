#include "extra/drill_practice_driver.h"

#include <GX/GXGeometry.h>
#include <GX/GXTypes.h>
#include <GX/GXVert.h>
#include <ft/fighter.h>
#include <gf/gf_camera.h>
#include <gf/gf_pad_status.h>
#include <so/camera/so_camera_module_impl.h>
#include <so/collision/so_collision_hit_module_impl.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/ground/so_ground_shape_impl.h>
#include <so/posture/so_posture_module_impl.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <so/visibility/so_visibility_module_impl.h>
#include <so/work/so_work_manage_module_impl.h>
#include <string.h>

#include "extra/drill_core_engines.h"
#include "extra/net_alloc.h"
#include "extra/stage_geom.h"
#include "main/cpu/fighter_input.h"
#include "main/cpu/options.h"
#include "main/cpu/view.h"
#include "main/game/fighters.h"
#include "main/game/pad_inmatch.h"
#include "main/game/status_ids.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/lab.h"
#include "main/settings.h"
#include "shared/game/frame.h"
#include "shared/game/mem.h"
#include "shared/game/pad_bits.h"
#include "shared/interface.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kPorts = iface::kPadCount,
            // P+ 3.2.1 verified work variables and structure offsets:
            //  - LA-Basic[79] (0x1000004F): 5-ledge-grab invuln counter (Ledge.asm:495-513)
            //  - LA-Bit[34]   (0x12000022): Flag_Disable_Cliff_Catch (ft/fighter.h:1297)
            //  - RA-Basic[0]  (0x20000000): Cliff_Wait auto-fall timer (0x8087ABC4-AC48)
            //  - RA-Bit[18]   (0x22000012): Cliff_Wait auto-fall trigger (0x8087AC30)
            //  - Customize_Param_Float_Walk_Speed_Max = 3003, Ground_Brake = 3004
            //  - grCollStatus cliff-hang box floats at +0x3C..+0x48 (0x801357F0)
            kLaLedgeGrabs = 0x1000004F,
            kLaDisableCliffCatch = 0x12000022,
            kRaCliffFallTimer = 0x20000000,
            kRaCliffAutoFallBit = 0x22000012,
            kParamWalkSpeedMax = 3003,
            kParamGroundBrake = 3004,
            kCollCliffBoxOffset = 0x3C,
            kFlagStandby = 1 << 0,
            kFlagCamOverride = 1 << 1,
            kFlagPsIntang = 1 << 2,
            kFlagNoReact = 1 << 3,
            kFlagFrameSnap = 1 << 4
        };

        struct SubState {
            PsCpuState psCpu[game::kMaxFighters];
            LedgeDriverState ledge;
            TargetCpuState target[game::kMaxFighters];
            WdZoneState wdZone;
        };

        struct PracticeNet {
            PracticeConfig cfg;
            u8 flags;
            u8 pendingDpad;
            u8 padFresh;
            u8 pad;
            u32 prevHeld[kPorts];
            u32 suppressMask[kPorts];
            SubState sub;
            StageGeometry cachedGeom;
            game::FighterState cachedHmn;
            DrillPracticeDebugBlock debug;
        };

        static PracticeNet* s_net = NULL;

        static u8 getTargetMode(const PracticeNet& net)
        {
            u8 d = net.debug.activeDrill;
            return (d == kPracticeDrillLCancel)
                       ? net.cfg.lcancelTarget
                       : (d == kPracticeDrillHoverFloat ? net.cfg.floatDjcTarget : static_cast<u8>(kTargetOff));
        }

        static float hmnFacing(const soModuleEnumeration* m)
        {
            return (game::isRam(m->m_postureModule) && m->m_postureModule->getLr() < 0.0f) ? -1.0f : 1.0f;
        }

        static void placeCpu(const game::FighterRef& r, float x, float y, float lr, bool resetSlot)
        {
            placeStanding(r, x, y, false, static_cast<float>(main::settings().cpu.percent), lr);
            setFighterCorrect(r, soGroundShapeImpl::Correct_Ground_Cliff_Stop);
            if (resetSlot && r.playerNo >= 0 && r.playerNo < game::kMaxFighters)
            {
                cpu::optionsResetSlot(r.playerNo);
            }
        }

        static void setNoReactPair(const game::FighterRef& r, bool on)
        {
            Fighter* p[2] = {r.fighter, (r.partner != r.fighter) ? r.partner : NULL};
            for (int k = 0; k < 2; k++)
            {
                if (game::isRam(p[k]))
                {
                    cpu::setNoReaction(p[k], on);
                }
            }
        }

        static void applyCameraRect(Fighter* f, float l, float r, float t, float b)
        {
            const soModuleEnumeration* m = game::fighterModules(f);
            if (m != NULL && game::isRam(m->m_cameraModule) && game::isRam(f->m_moduleAccesser))
            {
                Rect2D rc = {l, r, t, b};
                m->m_cameraModule->setCameraRangeGlobalRect(&rc, f->m_moduleAccesser, 0);
                s_net->flags |= kFlagCamOverride;
            }
        }

        static void clearTransient(PracticeNet& net, const game::FighterRef* refs, int n, u8 mask)
        {
            u8 todo = static_cast<u8>(net.flags & mask);
            if (todo == 0u)
            {
                return;
            }
            for (int i = 0; i < n; i++)
            {
                const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
                if (!refs[i].cpu)
                {
                    if ((todo & kFlagCamOverride) != 0u && m != NULL && game::isRam(m->m_cameraModule))
                    {
                        m->m_cameraModule->resetCameraRange(0);
                    }
                    continue;
                }
                if ((todo & kFlagPsIntang) != 0u && m != NULL && game::isRam(m->m_collisionHitModule))
                {
                    m->m_collisionHitModule->setXluFrameGlobal(0, 0);
                    m->m_collisionHitModule->resetStatusGlobal(0);
                }
                if ((todo & kFlagNoReact) != 0u)
                {
                    setNoReactPair(refs[i], false);
                }
            }
            net.flags &= static_cast<u8>(~todo);
        }

        static void updateStandby(PracticeNet& net, bool want, const game::FighterRef* refs, int n,
                                  const StageGeometry& geom)
        {
            if (!want && (net.flags & kFlagStandby) == 0u)
            {
                return;
            }
            net.flags = want ? static_cast<u8>(net.flags | kFlagStandby) : static_cast<u8>(net.flags & ~kFlagStandby);
            float cx = geom.floor.valid ? 0.5f * (geom.floor.leftX + geom.floor.rightX) : 0.0f;
            float fy = geom.floor.valid ? geom.floor.centerY : 0.0f;
            // Park strictly above camera frustum (below top blastzone) so
            // Fighter::processFixCamera never re-enables cursors; setWhole(0)
            // suppresses the off-screen magnifying loupe at 0x8083AA88.
            float py = geom.blast.valid ? (geom.blast.top - 12.0f) : (fy + 140.0f);

            for (int i = 0; i < n; i++)
            {
                if (!refs[i].cpu)
                {
                    continue;
                }
                Fighter* pair[2] = {refs[i].fighter, (refs[i].partner != refs[i].fighter) ? refs[i].partner : NULL};
                for (int k = 0; k < 2; k++)
                {
                    const soModuleEnumeration* m = game::fighterModules(pair[k]);
                    if (m == NULL)
                    {
                        continue;
                    }
                    if (game::isRam(m->m_cameraModule))
                    {
                        m->m_cameraModule->setEnableCamera(!want, -1);
                    }
                    if (game::isRam(m->m_visibilityModule))
                    {
                        m->m_visibilityModule->setWhole(want ? 0 : 1);
                    }
                    // Never turn cursors on for Nana partner (k == 1 on restore).
                    if (want || k == 0)
                    {
                        pair[k]->setCursor(!want);
                        pair[k]->setNameCursor(!want);
                    }
                    if (game::isRam(m->m_collisionHitModule))
                    {
                        if (want)
                        {
                            m->m_collisionHitModule->setXluGlobal(0);
                        }
                        else
                        {
                            m->m_collisionHitModule->cancelXluGlobal(0);
                            m->m_collisionHitModule->resetStatusGlobal(0);
                        }
                        m->m_collisionHitModule->setCheckCatch(!want, 0);
                    }
                    if (want)
                    {
                        if (game::isRam(m->m_groundModule))
                        {
                            m->m_groundModule->setCorrect(soGroundShapeImpl::Correct_None, 0);
                        }
                        cpu::clearKnockback(pair[k]);
                        cpu::placeAt(pair[k], cx, py);
                    }
                }
                if (!want)
                {
                    placeCpu(refs[i], cx + 16.0f, fy, -1.0f, true);
                }
            }
        }

        static void snapHumanToLedge(const game::FighterRef& hmn, const LedgeInfo& ledge, bool zeroGrabs)
        {
            const soModuleEnumeration* ms[2] = {
                game::fighterModules(hmn.fighter),
                (hmn.partner != hmn.fighter) ? game::fighterModules(hmn.partner) : NULL
            };
            if (ms[0] == NULL)
            {
                return;
            }
            CliffHangBox box = {-4.0f, 9.0f, 10.0f, 10.0f};
            if (game::isRam(ms[0]->m_groundModule))
            {
                ms[0]->m_groundModule->selectCliffHangData(0, 0);
                grCollStatus* cs = ms[0]->m_groundModule->getCollStatus(0);
                if (game::isRam(cs))
                {
                    const float* f = reinterpret_cast<const float*>(reinterpret_cast<u32>(cs) + kCollCliffBoxOffset);
                    box.offsetX = f[0];
                    box.offsetY = f[1];
                    box.width = f[2];
                    box.height = f[3];
                }
            }

            // Preserve LA-Basic[79] across refreshAirResources when P+ 5-Grab limit is active.
            int savedGrabs[2] = {0, 0};
            if (!zeroGrabs)
            {
                for (int k = 0; k < 2; k++)
                {
                    if (ms[k] != NULL && game::isRam(ms[k]->m_workManageModule))
                    {
                        savedGrabs[k] = ms[k]->m_workManageModule->getInt(kLaLedgeGrabs);
                    }
                }
            }

            float sx, sy;
            computeCliffSnapSpawn(ledge.x, ledge.y, ledge.dir, box, sx, sy);
            placeFalling(hmn, sx, sy, false, static_cast<float>(main::settings().general.playerPercent), ledge.dir);
            refreshAirResources(hmn, false); // clears LA-Basic[2] and zeroes LA-Basic[79]

            for (int k = 0; k < 2; k++)
            {
                const soModuleEnumeration* fm = ms[k];
                if (fm == NULL)
                {
                    continue;
                }
                if (game::isRam(fm->m_workManageModule))
                {
                    fm->m_workManageModule->offFlag(kLaDisableCliffCatch);
                    if (!zeroGrabs)
                    {
                        fm->m_workManageModule->setInt(savedGrabs[k], kLaLedgeGrabs);
                    }
                }
                if (game::isRam(fm->m_groundModule))
                {
                    fm->m_groundModule->setCorrect(soGroundShapeImpl::Correct_Air, 0);
                    fm->m_groundModule->setCliffCheck(soGroundShapeImpl::Cliff_Check_Always, 0);
                }
            }
        }

        static void resetSubsystems(bool exitStandby)
        {
            if (s_net == NULL)
            {
                return;
            }
            PracticeNet& net = *s_net;
            game::FighterRef refs[game::kMaxFighters];
            int n = game::listFighters(refs, game::kMaxFighters);
            if (exitStandby && (net.flags & kFlagStandby) != 0u)
            {
                StageGeometry geom;
                liveGeometry(geom, false, false);
                updateStandby(net, false, refs, n, geom);
            }
            clearTransient(net, refs, n, kFlagCamOverride | kFlagPsIntang | kFlagNoReact);
            memset(&net.sub, 0, sizeof(net.sub));
            for (int i = 0; i < game::kMaxFighters; i++)
            {
                net.sub.psCpu[i].rng = 0x6A09E667u + static_cast<u32>(i);
                net.sub.psCpu[i].cooldown = 20u;
                net.sub.target[i].patrolDir = 1.0f;
            }
            net.sub.ledge.rng = 0xBB67AE85u;
            net.sub.ledge.snapPending = 1u;
            net.sub.wdZone.dir = 1.0f;
            net.pendingDpad = 0u;
            net.padFresh = 1u;
            net.flags &= static_cast<u8>(~kFlagFrameSnap);
        }
    }

    void drillPracticeAttach()
    {
        freeNetPtr(s_net);
        s_net = allocNetArray<PracticeNet>(1);
        if (s_net == NULL)
        {
            return;
        }
        memset(s_net, 0, sizeof(*s_net));
        practiceConfigDefaults(s_net->cfg);
        s_net->debug.magic = kPracticeDebugMagic;
        s_net->debug.version = kPracticeDebugVersion;
        resetSubsystems(false);
        TM_LOG("drill_practice: debug block at %08x\n", &s_net->debug);
    }

    void drillPracticeDetach()
    {
        resetSubsystems(true);
        freeNetPtr(s_net);
    }

    void drillPracticeReset() { resetSubsystems(false); }

    void drillPracticeSetActiveDrill(u8 drillId)
    {
        if (s_net == NULL)
        {
            return;
        }
        u8 clean = drillId < kPracticeDrillCount ? drillId : static_cast<u8>(kPracticeDrillNone);
        if (clean != s_net->debug.activeDrill)
        {
            s_net->debug.activeDrill = clean;
            resetSubsystems(clean == kPracticeDrillNone);
        }
    }

    PracticeConfig& drillPracticeConfig()
    {
        static PracticeConfig s_fallback;
        static bool s_fallbackInit = false;
        if (s_net == NULL)
        {
            if (!s_fallbackInit)
            {
                practiceConfigDefaults(s_fallback);
                s_fallbackInit = true;
            }
            return s_fallback;
        }
        return s_net->cfg;
    }

    bool drillPracticeClaimsDpadLR() { return s_net != NULL && s_net->debug.activeDrill == kPracticeDrillLedgedash; }
    const WdZoneState* drillPracticeWdZone() { return s_net != NULL ? &s_net->sub.wdZone : NULL; }
    const DrillPracticeDebugBlock* drillPracticeDebugBlock() { return s_net != NULL ? &s_net->debug : NULL; }

    bool drillPracticeOnPad(u32 phase, gfPadStatus* pads)
    {
        if (s_net == NULL || pads == NULL || lab::menuOpen())
        {
            return false;
        }
        PracticeNet& net = *s_net;
        const u8 tMode = getTargetMode(net);
        u32 mask = (net.debug.activeDrill == kPracticeDrillLedgedash) ? (game::kPadDLeft | game::kPadDRight) : 0u;
        if ((tMode == kTargetStationary || tMode == kTargetShielding)
            && main::settings().controls.dpadDown != main::kDownFrameAdvance)
        {
            mask |= game::kPadDDown;
        }
        if (mask == 0u)
        {
            return false;
        }

        bool consumed = false;
        for (int i = 0; i < kPorts; i++)
        {
            if (phase == iface::kPadsSystem)
            {
                bool conn = game::padConnectedInMatch(i, pads[i]);
                u32 held = conn ? pads[i].m_buttonsCurrentFrame.bits : 0u;
                u32 pressed = (net.padFresh || !conn) ? 0u : (held & ~net.prevHeld[i]);
                net.prevHeld[i] = held;
                net.suppressMask[i] &= held;
                if ((held & (game::kPadL | game::kPadR)) == 0u
                    && (pads[i].m_controllerType != gfPadType::WIIMOTE || (held & game::kPadWiiMinus) != 0u))
                {
                    u32 hit = pressed & mask;
                    net.pendingDpad |= static_cast<u8>(hit & 7u);
                    net.suppressMask[i] |= hit;
                }
            }
            if (net.suppressMask[i] != 0u)
            {
                game::clearPadBits(pads[i], net.suppressMask[i]);
                consumed = true;
            }
        }
        if (phase == iface::kPadsSystem)
        {
            net.padFresh = 0u;
        }
        return consumed;
    }

    void drillPracticeFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        PracticeNet& net = *s_net;
        DrillPracticeDebugBlock& dbg = net.debug;
        sanitizePracticeConfig(net.cfg);

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        const u8 d = dbg.activeDrill;
        const u8 tMode = getTargetMode(net);
        StageGeometry& geom = net.cachedGeom;
        liveGeometry(geom, d == kPracticeDrillLedgedash, false);

        bool wantStandby = (d == kPracticeDrillLCancel || d == kPracticeDrillHoverFloat)
                               ? (tMode == kTargetOff)
                               : (d != kPracticeDrillNone && d != kPracticeDrillPowershield);
        updateStandby(net, wantStandby, refs, n, geom);

        const game::FighterRef* hmn = NULL;
        bool anyProj = false;
        for (int i = 0; i < n; i++)
        {
            if (!refs[i].cpu)
            {
                if (hmn == NULL)
                {
                    hmn = &refs[i];
                }
            }
            else if (d == kPracticeDrillPowershield && net.cfg.psAttackMode == kPsAtkProjectile
                     && findProjectileProfile(game::fighterKind(refs[i].entryId, false)) != NULL)
            {
                anyProj = true;
                const soModuleEnumeration* cm = game::fighterModules(refs[i].fighter);
                if (cm != NULL && game::isRam(cm->m_collisionHitModule))
                {
                    cm->m_collisionHitModule->setXluFrameGlobal(4, 0);
                }
                net.flags |= kFlagPsIntang;
            }
        }

        bool snapOk = (hmn != NULL) && game::readState(*hmn, net.cachedHmn);
        net.flags = snapOk ? static_cast<u8>(net.flags | kFlagFrameSnap) : static_cast<u8>(net.flags & ~kFlagFrameSnap);
        const soModuleEnumeration* hm = snapOk ? game::fighterModules(hmn->fighter) : NULL;
        dbg.cpuInStandby = (net.flags & kFlagStandby) != 0u ? 1u : 0u;
        dbg.targetModeActive = tMode;
        dbg.psResolvedMode = anyProj ? static_cast<u8>(kPsAtkProjectile) : static_cast<u8>(kPsAtkPhysical);
        if (d == kPracticeDrillNone || hm == NULL)
        {
            if (d == kPracticeDrillNone)
            {
                clearTransient(net, refs, n, kFlagCamOverride | kFlagPsIntang | kFlagNoReact);
            }
            return;
        }

        const game::FighterState& hmnFs = net.cachedHmn;
        if (static_cast<u32>(d - kPracticeDrillLCancel) <= 2u)
        {
            cpu::setShieldHealth(hmn->fighter, static_cast<float>(cpu::kShieldFull));
        }
        clearTransient(net, refs, n,
                       static_cast<u8>(((d != kPracticeDrillLedgedash
                                         && !(d == kPracticeDrillWavedash && net.cfg.wdTargetZone != 0u))
                                            ? kFlagCamOverride
                                            : 0u)
                                       | (!anyProj ? kFlagPsIntang : 0u)
                                       | (tMode != kTargetStationary ? kFlagNoReact : 0u)));

        int p1Slot = -1;
        if (d == kPracticeDrillLedgedash || d == kPracticeDrillWavedash)
        {
            for (int i = 0; i < game::kMaxTracked; i++)
            {
                const game::Tracked* t = game::tracked(i);
                if (t != NULL && t->used && !t->partner && t->entryId == hmn->entryId)
                {
                    p1Slot = i;
                    break;
                }
            }
        }

        // ---- 2. Ledgedash: Active Cliff-Hang Box Snap & Camera Framing ------
        if (d == kPracticeDrillLedgedash)
        {
            LedgeDriverState& ldg = net.sub.ledge;
            const LedgeCoreState* ls = p1Slot >= 0 ? coreDrillLedgeState(p1Slot) : NULL;
            const LedgeInfo& curLedge = (ldg.activeSide == 0u) ? geom.leftLedge : geom.rightLedge;
            ldg.rng ^= game::frameCounter() * 0x9E3779B9u;

            LedgeDriverInput lin;
            lin.hmnStatus = hmnFs.status;
            lin.hmnOutOfPlay = game::outOfPlay(hmnFs.status, true);
            lin.hmnY = hmnFs.y;
            lin.ledgeY = geom.hasLedges ? curLedge.y : 0.0f;
            lin.ledgeSeq = ls != NULL ? ls->seq : 0u;
            lin.lastLedgeOutcome = ls != NULL ? ls->last.outcome : 0u;
            lin.dpadLeftEdge = (net.pendingDpad & 1u) != 0u;
            lin.dpadRightEdge = (net.pendingDpad & 2u) != 0u;
            net.pendingDpad &= static_cast<u8>(~3u);

            LedgeDriverAction lact;
            stepLedgeDriver(lin, net.cfg, ldg, lact);
            dbg.ledgeActiveSide = ldg.activeSide;
            dbg.ledgeSnapCount = ldg.snapCount;

            if (game::isRam(hm->m_workManageModule))
            {
                if (lact.holdCliffTimer)
                {
                    if (hm->m_workManageModule->getInt(kRaCliffFallTimer) < 120)
                    {
                        hm->m_workManageModule->setInt(180, kRaCliffFallTimer);
                    }
                    hm->m_workManageModule->offFlag(kRaCliffAutoFallBit);
                }
                if (lact.clearGrabCounter)
                {
                    hm->m_workManageModule->setInt(0, kLaLedgeGrabs);
                }
            }
            if (geom.hasLedges)
            {
                const LedgeInfo& clg = (ldg.activeSide == 0u) ? geom.leftLedge : geom.rightLedge;
                if (lact.doSnap)
                {
                    snapHumanToLedge(*hmn, clg, net.cfg.ledgeInvulnLimit == kLedgeInvulnUnlimited);
                }
                if (lact.applyCamera)
                {
                    float cx = clg.x + (clg.dir >= 0.0f ? 12.0f : -12.0f);
                    applyCameraRect(hmn->fighter, cx - 34.0f, cx + 34.0f, clg.y + 36.0f, clg.y - 14.0f);
                }
            }
        }
        // ---- 3. Wavedash / Waveland: GX Target Zone & Steady Framing --------
        else if (d == kPracticeDrillWavedash)
        {
            WdZoneState& z = net.sub.wdZone;
            if (net.cfg.wdTargetZone == 0u)
            {
                z.active = 0u;
            }
            else
            {
                const WdCoreState* ws = p1Slot >= 0 ? coreDrillWavedashState(p1Slot) : NULL;
                if (ws != NULL && !z.syncedSeq)
                {
                    z.lastSeenWdSeq = ws->seq;
                    z.syncedSeq = 1u;
                }
                bool respawn = false;
                if (z.flashFrames > 0u)
                {
                    z.flashFrames--;
                }
                else if (z.respawnPending != 0u)
                {
                    z.respawnPending = 0u;
                    respawn = true;
                }
                if (!z.active || respawn)
                {
                    float brake = 0.08f, walkMax = 1.5f;
                    if (game::isRam(hmn->fighter->m_moduleAccesser))
                    {
                        brake = soValueAccesser::getConstantFloat(hmn->fighter->m_moduleAccesser, kParamGroundBrake, 0);
                        walkMax = soValueAccesser::getConstantFloat(hmn->fighter->m_moduleAccesser, kParamWalkSpeedMax, 0);
                    }
                    spawnWavedashZone(z, hmnFs.x, hmnFs.y, hmnFacing(hm), brake, geom, walkMax);
                }
                if (ws != NULL && ws->seq != z.lastSeenWdSeq)
                {
                    z.lastSeenWdSeq = ws->seq;
                    scoreWavedashZone(z, hmnFs.x, ws->last.outcome == kWdOutcomeSuccess);
                }
                if (z.active)
                {
                    float by = z.y < hmnFs.y ? z.y : hmnFs.y;
                    applyCameraRect(hmn->fighter, (hmnFs.x < z.minX ? hmnFs.x : z.minX) - 18.0f,
                                    (hmnFs.x > z.maxX ? hmnFs.x : z.maxX) + 18.0f, by + 36.0f, by - 12.0f);
                }
            }
            dbg.wdZoneActive = z.active;
            dbg.wdZoneLastOutcome = z.lastOutcome;
            dbg.wdZoneAttempts = z.attempts;
            dbg.wdZoneHits = z.hits;
            dbg.wdZoneStreak = z.streak;
            dbg.wdZoneBestStreak = z.bestStreak;
            dbg.wdZoneMinX = z.minX;
            dbg.wdZoneMaxX = z.maxX;
            dbg.wdZoneY = z.y;
        }
        // ---- 4. L-Cancel & Float/Hover/DJC: Target Management ---------------
        else if (tMode != kTargetOff)
        {
            bool hmnAir = game::isRam(hm->m_situationModule) && hm->m_situationModule->getKind() == Situation_Air;
            bool doRepos = ((net.pendingDpad & 4u) != 0u) && !hmnAir
                           && (tMode == kTargetStationary || tMode == kTargetShielding);
            net.pendingDpad &= static_cast<u8>(~4u);

            for (int i = 0; i < n; i++)
            {
                if (!refs[i].cpu || !game::isRam(refs[i].fighter))
                {
                    continue;
                }
                int p = refs[i].playerNo;
                TargetCpuState& ts = net.sub.target[(p >= 0 && p < game::kMaxFighters) ? p : 0];
                if (tMode == kTargetShielding)
                {
                    cpu::setShieldHealth(refs[i].fighter, static_cast<float>(cpu::kShieldFull));
                }
                else if (tMode == kTargetStationary)
                {
                    setNoReactPair(refs[i], true);
                    net.flags |= kFlagNoReact;
                }
                bool needPlace = doRepos || ((tMode == kTargetStationary || tMode == kTargetShielding) && !ts.anchorValid);
                if (needPlace)
                {
                    computeTargetAnchor(hmnFs.x, hmnFacing(hm), 12.0f, geom, ts.anchorX, ts.anchorY, ts.anchorLr);
                    ts.anchorValid = true;
                }
                else if (tMode == kTargetStationary || tMode == kTargetShielding)
                {
                    game::FighterState cfs;
                    needPlace = game::readState(refs[i], cfs) && game::outOfPlay(cfs.status, true)
                                && cfs.status != game::kStatusDead && cfs.status != game::kStatusStandby;
                }
                if (needPlace)
                {
                    placeCpu(refs[i], ts.anchorX, ts.anchorY, ts.anchorLr, doRepos);
                }
            }
        }
    }

    bool drillPracticeCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons)
    {
        (void)lastButtons;
        if (s_net == NULL || !who.cpu || f == NULL)
        {
            return false;
        }
        PracticeNet& net = *s_net;
        const u8 d = net.debug.activeDrill;
        if (d == kPracticeDrillNone)
        {
            return false;
        }
        if ((net.flags & kFlagStandby) != 0u)
        {
            out.buttons = 0u;
            out.x = out.y = 0.0f;
            return true;
        }
        if ((net.flags & kFlagFrameSnap) == 0u)
        {
            return false;
        }

        const int slot = (who.playerNo >= 0 && who.playerNo < game::kMaxFighters) ? who.playerNo : 0;
        const float floorL = net.cachedGeom.floor.valid ? net.cachedGeom.floor.leftX : -68.0f;
        const float floorR = net.cachedGeom.floor.valid ? net.cachedGeom.floor.rightX : 68.0f;
        const bool canAct = cpu::canAct(v);
        const bool cpuOop = game::outOfPlay(v.status, true);
        bool act = false;
        u32 btn = 0u;
        float sx = 0.0f;

        if (d == kPracticeDrillPowershield)
        {
            PsCpuState& ps = net.sub.psCpu[slot];
            if (ps.phase == kPsCpuIdle && (v.hitstun > 0 || v.hitlag > 0 || v.air))
            {
                return false;
            }
            ps.rng ^= game::frameCounter() * 0x9E3779B9u + static_cast<u32>(slot);
            PsCpuInput in;
            in.cpuFtKind = game::fighterKind(who.entryId, who.sub);
            in.cpuStatus = v.status;
            in.cpuAir = v.air;
            in.cpuCanAct = canAct;
            in.cpuOutOfPlay = cpuOop;
            in.hmnOutOfPlay = game::outOfPlay(net.cachedHmn.status, true);
            in.cpuX = v.x;
            in.cpuLr = v.lr;
            in.hmnX = net.cachedHmn.x;
            in.floorLeft = floorL;
            in.floorRight = floorR;
            PsCpuOutput po;
            stepPsPracticeCpu(in, net.cfg, ps, po);
            net.debug.psResolvedHeight = ps.resolvedHeight;
            act = po.active;
            btn = po.buttons;
            sx = po.stickX;
        }
        else
        {
            const u8 tMode = getTargetMode(net);
            if (tMode != kTargetOff)
            {
                if ((tMode == kTargetShielding && (v.hitstun > 0 || v.air))
                    || (tMode == kTargetMoving && (v.hitstun > 0 || v.hitlag > 0 || v.air)))
                {
                    return false;
                }
                TargetCpuInput tin;
                tin.targetMode = tMode;
                tin.cpuAir = v.air;
                tin.cpuCanAct = canAct;
                tin.cpuOutOfPlay = cpuOop;
                tin.cpuX = v.x;
                tin.cpuLr = v.lr;
                tin.hmnX = net.cachedHmn.x;
                tin.floorLeft = floorL;
                tin.floorRight = floorR;
                TargetCpuOutput to;
                stepTargetCpu(tin, net.sub.target[slot], to);
                act = to.active;
                btn = to.buttons;
                sx = to.stickX;
            }
        }
        if (act)
        {
            out.buttons = btn;
            out.x = sx;
            out.y = 0.0f;
        }
        return act;
    }

    // Draws the sleek low Wavedash / Waveland GX floor ribbon in the 3D main
    // world camera pass (call only when s_camera == kMainCamera).
    void drillPracticeDraw(const game::MatchState& match)
    {
        if (s_net == NULL || !match.running || match.paused || lab::menuOpen())
        {
            return;
        }
        PracticeNet& net = *s_net;
        const WdZoneState& z = net.sub.wdZone;
        if (net.debug.activeDrill != kPracticeDrillWavedash || net.cfg.wdTargetZone == 0u || !z.active)
        {
            return;
        }
        gfCameraManager* camMgr = gfCameraManager::getManager();
        if (!game::isRam(camMgr))
        {
            return;
        }

        static const gfx::Rgba kPal[3][3] = {
            {0x22B8CC48u, 0x0C4858A8u, 0xFFD300F0u}, // Idle Brawl/P+ teal + gold
            {0x4CE68888u, 0x1E8048D0u, 0xFFE866FFu}, // Hit flash
            {0xFF665878u, 0x902820C0u, 0xFF8878F0u}  // Short / Overshoot flash
        };
        const gfx::Rgba* pal = kPal[z.flashFrames > 0u ? (z.lastOutcome == kWdZoneHit ? 1 : 2) : 0];
        const gfx::Rgba bc = pal[2];
        const float x0 = z.minX, x1 = z.maxX, y0 = z.y + 0.12f, y1 = y0 + 0.65f, yt = y0 + 1.2f;
        const float xm = 0.5f * (x0 + x1);

        camMgr->m_cameras[0].setGX();
        gfx::forgetState();
        gfx::prepare();
        GXBegin(GX_QUADS, GX_VTXFMT1, 4);
        gfx::vertex(x0, y1, pal[0]);
        gfx::vertex(x1, y1, pal[0]);
        gfx::vertex(x1, y0, pal[1]);
        gfx::vertex(x0, y0, pal[1]);
        GXEnd();

        // Sleek top/bottom gold rails + left/right/center vertical ticks in one batch.
        gfx::lineBegin(GX_LINES, 10, 14);
        gfx::vertex(x0, y0, bc);
        gfx::vertex(x1, y0, bc);
        gfx::vertex(x0, y1, bc);
        gfx::vertex(x1, y1, bc);
        gfx::vertex(x0, y0, bc);
        gfx::vertex(x0, yt, bc);
        gfx::vertex(x1, y0, bc);
        gfx::vertex(x1, yt, bc);
        gfx::vertex(xm, y0, bc);
        gfx::vertex(xm, yt, bc);
        GXEnd();

        GXSetLineWidth(6, 0);
        gfx::forgetState();
        net.debug.drawCalls++;
    }
}
}
