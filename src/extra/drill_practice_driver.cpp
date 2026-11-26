#include "extra/drill_practice_driver.h"

#include <GX/GXTypes.h>
#include <GX/GXVert.h>
#include <ft/fighter.h>
#include <gf/gf_camera.h>
#include <gf/gf_pad_status.h>
#include <so/camera/so_camera_module_impl.h>
#include <so/collision/so_collision_hit_module_impl.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/ground/so_ground_shape_impl.h>
#include <so/kinetic/so_kinetic_module_impl.h>
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
            //  - LA-Basic[2]  (0x10000002): Int_Cliff_No_Catch_Frame (ft/fighter.h:1178)
            //  - LA-Basic[79] (0x1000004F): 5-ledge-grab invuln counter (Ledge.asm:495-513)
            //  - LA-Bit[34]   (0x12000022): Flag_Disable_Cliff_Catch (ft/fighter.h:1297)
            //  - RA-Basic[0]  (0x20000000): Cliff_Wait auto-fall timer (0x8087ABC4-AC48)
            //  - RA-Bit[18]   (0x22000012): Cliff_Wait auto-fall trigger (0x8087AC30)
            //  - Customize_Param_Float_Ground_Brake = 3004 (ft_value_accesser.h:15)
            //  - grCollStatus cliff-hang box floats at +0x3C..+0x48 (0x801357F0)
            //  - Fighter cursor flags byte at +0x138: bit 0x04 cursor, 0x02 nameCursor
            kLaCliffNoCatch = 0x10000002,
            kLaLedgeGrabs = 0x1000004F,
            kLaDisableCliffCatch = 0x12000022,
            kRaCliffFallTimer = 0x20000000,
            kRaCliffAutoFallBit = 0x22000012,
            kParamGroundBrake = 3004,
            kCollCliffBoxOffset = 0x3C,
            kFighterCursorFlags = 0x138,
            kCursorBit = 0x04,
            kNameCursorBit = 0x02
        };

        struct StandbySlot {
            bool active;
            u8 savedWhole;
            u8 savedCursor;
            u8 savedNameCursor;
        };

        struct PracticeNet {
            PracticeConfig cfg;
            u8 activeDrill;
            bool cpuInStandby;
            bool cameraOverrideActive;
            bool psCpuIntangible;
            u8 pendingDpadLeft;
            u8 pendingDpadRight;
            u8 pendingDpadDown;
            u8 padFresh;
            u32 prevHeld[kPorts];
            u32 suppressMask[kPorts];
            StandbySlot standby[game::kMaxFighters];
            PsCpuState psCpu;
            LedgeDriverState ledge;
            TargetCpuState target;
            WdZoneState wdZone;
            DrillPracticeDebugBlock debug;
        };

        static PracticeNet* s_net = NULL;

#pragma dont_inline on
        static bool ramOk(const void* p)
        {
            return game::isRam(p);
        }

        static const soModuleEnumeration* modsOf(Fighter* f)
        {
            return ramOk(f) ? game::fighterModules(f) : NULL;
        }

        static bool findPrimaryHuman(const game::FighterRef* refs, int n, game::FighterRef& out)
        {
            for (int i = 0; i < n; i++)
            {
                if (!refs[i].cpu)
                {
                    out = refs[i];
                    return true;
                }
            }
            return false;
        }

        static int findHumanTrackedSlot(int entryId)
        {
            for (int i = 0; i < game::kMaxTracked; i++)
            {
                const game::Tracked* t = game::tracked(i);
                if (t != NULL && t->used && !t->partner && t->entryId == entryId)
                {
                    return i;
                }
            }
            return -1;
        }

        static float readLr(const soModuleEnumeration* m)
        {
            return (m != NULL && ramOk(m->m_postureModule) && m->m_postureModule->getLr() < 0.0f) ? -1.0f : 1.0f;
        }

        static void applyCameraRect(Fighter* f, float left, float right, float top, float bottom)
        {
            const soModuleEnumeration* m = modsOf(f);
            if (m == NULL || !ramOk(m->m_cameraModule) || !ramOk(f->m_moduleAccesser))
            {
                return;
            }
            Rect2D r;
            r.m_left = left;
            r.m_right = right;
            r.m_up = top;
            r.m_down = bottom;
            m->m_cameraModule->setCameraRangeGlobalRect(&r, f->m_moduleAccesser, 0);
            s_net->cameraOverrideActive = true;
        }

        static void clearTransientState(const game::FighterRef* refs, int n, bool clearCam, bool clearIntang)
        {
            bool doCam = clearCam && s_net->cameraOverrideActive;
            bool doIntang = clearIntang && s_net->psCpuIntangible;
            if (!doCam && !doIntang)
            {
                return;
            }
            for (int i = 0; i < n; i++)
            {
                const soModuleEnumeration* m = modsOf(refs[i].fighter);
                if (m == NULL)
                {
                    continue;
                }
                if (doCam && !refs[i].cpu && ramOk(m->m_cameraModule))
                {
                    m->m_cameraModule->resetCameraRange(0);
                }
                if (doIntang && refs[i].cpu && ramOk(m->m_collisionHitModule))
                {
                    m->m_collisionHitModule->setXluFrameGlobal(0, 0);
                    m->m_collisionHitModule->resetStatusGlobal(0);
                }
            }
            if (doCam)
            {
                s_net->cameraOverrideActive = false;
            }
            if (doIntang)
            {
                s_net->psCpuIntangible = false;
            }
        }

        static void standbyApplySingle(Fighter* f, StandbySlot* slot, bool hide, bool firstEnter, float parkX, float parkY)
        {
            const soModuleEnumeration* m = modsOf(f);
            if (m == NULL)
            {
                return;
            }
            if (hide)
            {
                if (firstEnter && slot != NULL)
                {
                    u8 whole = ramOk(m->m_visibilityModule) ? m->m_visibilityModule->getWhole() : 1;
                    u8 cf = game::byteAt(f, kFighterCursorFlags);
                    slot->active = true;
                    slot->savedWhole = whole != 0 ? whole : 1;
                    slot->savedCursor = (cf & kCursorBit) != 0 ? 1 : 0;
                    slot->savedNameCursor = (cf & kNameCursorBit) != 0 ? 1 : 0;
                }
                if (ramOk(m->m_visibilityModule))
                {
                    m->m_visibilityModule->setWhole(0);
                }
                f->setCursor(false);
                f->setNameCursor(false);
                if (ramOk(m->m_collisionHitModule))
                {
                    m->m_collisionHitModule->setXluGlobal(0);
                    m->m_collisionHitModule->setXluFrameGlobal(999, 0);
                    m->m_collisionHitModule->setCheckCatch(false, 0);
                }
                if (ramOk(m->m_cameraModule))
                {
                    m->m_cameraModule->setEnableCamera(false, -1);
                }
                if (ramOk(m->m_groundModule))
                {
                    m->m_groundModule->setCorrect(soGroundShapeImpl::Correct_None, 0);
                }
                if (ramOk(m->m_kineticModule))
                {
                    m->m_kineticModule->clearSpeedAll();
                }
                cpu::clearKnockback(f);
                cpu::placeAt(f, parkX, parkY);
                return;
            }

            if (ramOk(m->m_cameraModule))
            {
                m->m_cameraModule->setEnableCamera(true, -1);
            }
            if (ramOk(m->m_collisionHitModule))
            {
                m->m_collisionHitModule->cancelXluGlobal(0);
                m->m_collisionHitModule->setXluFrameGlobal(0, 0);
                m->m_collisionHitModule->setCheckCatch(true, 0);
                m->m_collisionHitModule->resetStatusGlobal(0);
            }
            if (ramOk(m->m_visibilityModule))
            {
                u8 whole = (slot != NULL && slot->active) ? slot->savedWhole : 1;
                m->m_visibilityModule->setWhole(whole != 0 ? whole : 1);
            }
            f->setCursor(slot == NULL || !slot->active || slot->savedCursor != 0);
            f->setNameCursor(slot == NULL || !slot->active || slot->savedNameCursor != 0);
        }

        static void applyStandbyTransition(u8 trans, const game::FighterRef* refs, int n, const StageGeometry& geom)
        {
            if (trans == kStandbyNoop)
            {
                return;
            }
            float centerX = geom.floor.valid ? 0.5f * (geom.floor.leftX + geom.floor.rightX) : 0.0f;
            float floorY = geom.floor.valid ? geom.floor.centerY : 0.0f;
            float parkY = floorY + 80.0f;
            if (geom.blast.valid && parkY > geom.blast.top - 28.0f)
            {
                parkY = geom.blast.top - 28.0f;
            }

            bool hiding = (trans == kStandbyEnter || trans == kStandbyHold);
            float cpuPct = static_cast<float>(main::settings().cpu.percent);
            for (int i = 0; i < n; i++)
            {
                if (!refs[i].cpu)
                {
                    continue;
                }
                int p = refs[i].playerNo;
                StandbySlot* slot = (p >= 0 && p < game::kMaxFighters) ? &s_net->standby[p] : NULL;
                bool first = (trans == kStandbyEnter) || (slot != NULL && !slot->active);
                standbyApplySingle(refs[i].fighter, slot, hiding, first, centerX, parkY);
                if (refs[i].partner != NULL && refs[i].partner != refs[i].fighter)
                {
                    standbyApplySingle(refs[i].partner, NULL, hiding, false, centerX, parkY);
                }
                if (!hiding)
                {
                    if (slot != NULL)
                    {
                        slot->active = false;
                    }
                    float sx = geom.floor.valid ? geomClamp(centerX + 16.0f + static_cast<float>(i) * 4.0f,
                                                            geom.floor.leftX + 10.0f, geom.floor.rightX - 10.0f)
                                                : 16.0f;
                    placeStanding(refs[i], sx, floorY, false, cpuPct, -1.0f);
                    setFighterCorrect(refs[i], soGroundShapeImpl::Correct_Ground_Cliff_Stop);
                    if (p >= 0 && p < game::kMaxFighters)
                    {
                        cpu::optionsResetSlot(p);
                    }
                }
            }
        }

        static void snapHumanToLedge(const game::FighterRef& hmn, const LedgeInfo& ledge, bool zeroGrabs)
        {
            const soModuleEnumeration* m = modsOf(hmn.fighter);
            if (m == NULL)
            {
                return;
            }
            CliffHangBox box = defaultCliffHangBox();
            if (ramOk(m->m_groundModule))
            {
                m->m_groundModule->selectCliffHangData(0, 0);
                grCollStatus* cs = m->m_groundModule->getCollStatus(0);
                if (ramOk(cs))
                {
                    const float* fbox = reinterpret_cast<const float*>(reinterpret_cast<u32>(cs) + kCollCliffBoxOffset);
                    box.offsetX = fbox[0];
                    box.offsetY = fbox[1];
                    box.width = fbox[2];
                    box.height = fbox[3];
                }
            }

            float sx = 0.0f;
            float sy = 0.0f;
            computeCliffSnapSpawn(ledge.x, ledge.y, ledge.dir, box, sx, sy);
            placeFalling(hmn, sx, sy, false, static_cast<float>(main::settings().general.playerPercent), ledge.dir);
            refreshAirResources(hmn, false);

            Fighter* pair[2] = {hmn.fighter, (hmn.partner != NULL && hmn.partner != hmn.fighter) ? hmn.partner : NULL};
            for (int k = 0; k < 2; k++)
            {
                const soModuleEnumeration* fm = modsOf(pair[k]);
                if (fm == NULL)
                {
                    continue;
                }
                if (ramOk(fm->m_workManageModule))
                {
                    fm->m_workManageModule->setInt(0, kLaCliffNoCatch);
                    fm->m_workManageModule->offFlag(kLaDisableCliffCatch);
                    if (zeroGrabs)
                    {
                        fm->m_workManageModule->setInt(0, kLaLedgeGrabs);
                    }
                }
                if (ramOk(fm->m_groundModule))
                {
                    fm->m_groundModule->setCorrect(soGroundShapeImpl::Correct_Air, 0);
                    fm->m_groundModule->setCliffCheck(soGroundShapeImpl::Cliff_Check_Always, 0);
                    fm->m_groundModule->selectCliffHangData(0, 0);
                }
                if (ramOk(fm->m_collisionHitModule))
                {
                    fm->m_collisionHitModule->setXluFrameGlobal(0, 0);
                    fm->m_collisionHitModule->setInvincibleFrameGlobal(0, true, 0);
                    fm->m_collisionHitModule->resetStatusGlobal(0);
                }
            }
        }

        static void resetSubsystems(bool exitStandby)
        {
            game::FighterRef refs[game::kMaxFighters];
            int n = game::listFighters(refs, game::kMaxFighters);
            if (exitStandby && s_net->cpuInStandby)
            {
                StageGeometry geom;
                liveGeometry(geom, false, false);
                s_net->cpuInStandby = false;
                applyStandbyTransition(kStandbyExit, refs, n, geom);
            }
            clearTransientState(refs, n, true, true);
            psCpuStateReset(s_net->psCpu);
            ledgeDriverReset(s_net->ledge);
            targetCpuReset(s_net->target);
            wdZoneReset(s_net->wdZone);
            s_net->pendingDpadLeft = 0u;
            s_net->pendingDpadRight = 0u;
            s_net->pendingDpadDown = 0u;
            s_net->padFresh = 1u;
        }

        static void syncDebugBlock(u8 psMode, u8 targetMode)
        {
            DrillPracticeDebugBlock& d = s_net->debug;
            d.magic = kPracticeDebugMagic;
            d.version = kPracticeDebugVersion;
            d.activeDrill = s_net->activeDrill;
            d.cpuInStandby = s_net->cpuInStandby ? 1u : 0u;
            d.psResolvedMode = psMode;
            d.psResolvedHeight = s_net->psCpu.resolvedHeight;
            d.ledgeActiveSide = s_net->ledge.activeSide;
            d.wdZoneActive = s_net->wdZone.active;
            d.wdZoneLastOutcome = s_net->wdZone.lastOutcome;
            d.targetModeActive = targetMode;
            d.ledgeSnapCount = s_net->ledge.snapCount;
            d.wdZoneAttempts = s_net->wdZone.attempts;
            d.wdZoneHits = s_net->wdZone.hits;
            d.wdZoneStreak = s_net->wdZone.streak;
            d.wdZoneBestStreak = s_net->wdZone.bestStreak;
            d.wdZoneMinX = s_net->wdZone.minX;
            d.wdZoneMaxX = s_net->wdZone.maxX;
            d.wdZoneY = s_net->wdZone.y;
        }

        static void emitVerts(const float (*pts)[2], int count, gfx::Rgba col)
        {
            for (int i = 0; i < count; i++)
            {
                gfx::vertex(pts[i][0], pts[i][1], col);
            }
        }
#pragma dont_inline reset
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
        s_net->padFresh = 1u;
        psCpuStateReset(s_net->psCpu);
        ledgeDriverReset(s_net->ledge);
        targetCpuReset(s_net->target);
        wdZoneReset(s_net->wdZone);
        syncDebugBlock(kPsAtkPhysical, kTargetOff);
        TM_LOG("drill_practice: debug block at %08x\n", &s_net->debug);
    }

    void drillPracticeDetach()
    {
        if (s_net == NULL)
        {
            return;
        }
        resetSubsystems(true);
        freeNetPtr(s_net);
    }

    void drillPracticeReset()
    {
        if (s_net == NULL)
        {
            return;
        }
        resetSubsystems(true);
        syncDebugBlock(kPsAtkPhysical, kTargetOff);
    }

    void drillPracticeSetActiveDrill(u8 drillId)
    {
        if (s_net == NULL)
        {
            return;
        }
        u8 clean = drillId < kPracticeDrillCount ? drillId : static_cast<u8>(kPracticeDrillNone);
        if (clean == s_net->activeDrill)
        {
            return;
        }
        s_net->activeDrill = clean;
        resetSubsystems(false);
    }

    PracticeConfig& drillPracticeConfig()
    {
        static PracticeConfig s_fallback;
        if (s_net == NULL)
        {
            practiceConfigDefaults(s_fallback);
            return s_fallback;
        }
        return s_net->cfg;
    }

    const WdZoneState* drillPracticeWdZone()
    {
        return s_net != NULL ? &s_net->wdZone : NULL;
    }

    const DrillPracticeDebugBlock* drillPracticeDebugBlock()
    {
        return s_net != NULL ? &s_net->debug : NULL;
    }

    bool drillPracticeOnPad(u32 phase, gfPadStatus* pads)
    {
        if (s_net == NULL || pads == NULL || s_net->activeDrill == kPracticeDrillNone || lab::menuOpen())
        {
            return false;
        }
        const u8 d = s_net->activeDrill;
        const bool wantLedge = (d == kPracticeDrillLedgedash);
        const u8 tMode = activeTargetMode(d, s_net->cfg);
        const bool wantRepos = (tMode == kTargetStationary || tMode == kTargetShielding);
        if (!wantLedge && !wantRepos)
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
                u32 pressed = (s_net->padFresh || !conn) ? 0u : (held & ~s_net->prevHeld[i]);
                s_net->prevHeld[i] = held;
                s_net->suppressMask[i] &= held;

                if (wantLedge && (pressed & (game::kPadDLeft | game::kPadDRight)) != 0u)
                {
                    bool isLeft = (pressed & game::kPadDLeft) != 0u;
                    if (isLeft)
                    {
                        s_net->pendingDpadLeft = 1u;
                    }
                    else
                    {
                        s_net->pendingDpadRight = 1u;
                    }
                    s_net->suppressMask[i] |= isLeft ? game::kPadDLeft : game::kPadDRight;
                    consumed = true;
                }
                if (wantRepos && (pressed & game::kPadDDown) != 0u)
                {
                    s_net->pendingDpadDown = 1u;
                    s_net->suppressMask[i] |= game::kPadDDown;
                    consumed = true;
                }
            }
            if (s_net->suppressMask[i] != 0u)
            {
                game::clearPadBits(pads[i], s_net->suppressMask[i]);
                consumed = true;
            }
        }
        if (phase == iface::kPadsSystem)
        {
            s_net->padFresh = 0u;
        }
        return consumed;
    }

    void drillPracticeFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        sanitizePracticeConfig(s_net->cfg);

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        const u8 d = s_net->activeDrill;
        StageGeometry geom;
        liveGeometry(geom, d == kPracticeDrillLedgedash, false);

        u8 standbyTrans = stepStandbyState(shouldCpuStandby(d, s_net->cfg), s_net->cpuInStandby);
        applyStandbyTransition(standbyTrans, refs, n, geom);

        game::FighterRef hmn;
        game::FighterState hmnFs;
        if (d == kPracticeDrillNone || !findPrimaryHuman(refs, n, hmn) || !game::readState(hmn, hmnFs))
        {
            if (d == kPracticeDrillNone)
            {
                clearTransientState(refs, n, true, true);
            }
            syncDebugBlock(kPsAtkPhysical, kTargetOff);
            return;
        }
        const soModuleEnumeration* hm = modsOf(hmn.fighter);
        if (hm == NULL)
        {
            return;
        }

        u8 resolvedPsMode = kPsAtkPhysical;
        const u8 tMode = activeTargetMode(d, s_net->cfg);

        // Full P1 shield upkeep in Powershield, L-Cancel, and Float / Hover & DJC.
        if (d == kPracticeDrillPowershield || d == kPracticeDrillLCancel || d == kPracticeDrillHoverFloat)
        {
            cpu::setShieldHealth(hmn.fighter, static_cast<float>(cpu::kShieldFull));
        }

        // ---- 1. Powershield: CPU intangibility in Projectile mode -----------
        if (d == kPracticeDrillPowershield)
        {
            bool anyProj = false;
            for (int i = 0; i < n; i++)
            {
                if (!refs[i].cpu)
                {
                    continue;
                }
                const soModuleEnumeration* cm = modsOf(refs[i].fighter);
                if (cm == NULL)
                {
                    continue;
                }
                u8 rMode = resolvePsAttackMode(s_net->cfg.psAttackMode, game::fighterKind(refs[i].entryId, false));
                if (resolvedPsMode == kPsAtkPhysical)
                {
                    resolvedPsMode = rMode;
                }
                if (rMode == kPsAtkProjectile)
                {
                    anyProj = true;
                    if (ramOk(cm->m_collisionHitModule))
                    {
                        cm->m_collisionHitModule->setXluFrameGlobal(4, 0);
                    }
                    s_net->psCpuIntangible = true;
                }
            }
            clearTransientState(refs, n, true, !anyProj);
        }
        else
        {
            clearTransientState(refs, n, d != kPracticeDrillLedgedash && d != kPracticeDrillWavedash, true);
        }

        // ---- 2. Ledgedash: Active Cliff-Hang Box Snap & Camera Framing ------
        if (d == kPracticeDrillLedgedash)
        {
            const int p1Slot = findHumanTrackedSlot(hmn.entryId);
            const LedgeCoreState* ls = p1Slot >= 0 ? coreDrillLedgeState(p1Slot) : NULL;
            const LedgeInfo& curLedge = (s_net->ledge.activeSide == 0u) ? geom.leftLedge : geom.rightLedge;

            s_net->ledge.rng ^= game::frameCounter() * 0x9E3779B9u;
            LedgeDriverInput lin;
            lin.hmnStatus = hmnFs.status;
            lin.hmnOutOfPlay = game::outOfPlay(hmnFs.status, true);
            lin.hmnY = hmnFs.y;
            lin.ledgeY = geom.hasLedges ? curLedge.y : 0.0f;
            lin.ledgeSeq = ls != NULL ? ls->seq : 0u;
            lin.lastLedgeOutcome = ls != NULL ? ls->last.outcome : 0u;
            lin.dpadLeftEdge = s_net->pendingDpadLeft != 0u;
            lin.dpadRightEdge = s_net->pendingDpadRight != 0u;
            s_net->pendingDpadLeft = 0u;
            s_net->pendingDpadRight = 0u;

            LedgeDriverAction lact;
            stepLedgeDriver(lin, s_net->cfg, s_net->ledge, lact);

            if (ramOk(hm->m_workManageModule))
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
                if (lact.doSnap)
                {
                    const LedgeInfo& targetLedge = (lact.snapSide == 0u) ? geom.leftLedge : geom.rightLedge;
                    snapHumanToLedge(hmn, targetLedge, s_net->cfg.ledgeInvulnLimit == kLedgeInvulnUnlimited);
                }
                if (lact.applyCamera)
                {
                    const LedgeInfo& camLedge = (s_net->ledge.activeSide == 0u) ? geom.leftLedge : geom.rightLedge;
                    float cl, cr, ct, cb;
                    computeLedgeCameraRect(camLedge.x, camLedge.y, camLedge.dir, cl, cr, ct, cb);
                    applyCameraRect(hmn.fighter, cl, cr, ct, cb);
                }
            }
        }
        // ---- 3. Wavedash / Waveland: GX Target Zone & Steady Framing --------
        else if (d == kPracticeDrillWavedash)
        {
            WdZoneState& z = s_net->wdZone;
            if (z.flashFrames > 0u)
            {
                z.flashFrames--;
            }
            if (s_net->cfg.wdTargetZone == 0u)
            {
                z.active = 0u;
                clearTransientState(refs, n, true, false);
            }
            else
            {
                const int p1Slot = findHumanTrackedSlot(hmn.entryId);
                const WdCoreState* ws = p1Slot >= 0 ? coreDrillWavedashState(p1Slot) : NULL;
                float brake = ramOk(hmn.fighter->m_moduleAccesser)
                                  ? soValueAccesser::getConstantFloat(hmn.fighter->m_moduleAccesser, kParamGroundBrake, 0)
                                  : 0.08f;
                const float hmnLr = readLr(hm);
                if (ws != NULL && !z.syncedSeq)
                {
                    z.lastSeenWdSeq = ws->seq;
                    z.syncedSeq = 1u;
                }
                if (!z.active)
                {
                    spawnWavedashZone(z, hmnFs.x, hmnFs.y, hmnLr, brake, geom);
                }
                if (ws != NULL && ws->seq != z.lastSeenWdSeq)
                {
                    z.lastSeenWdSeq = ws->seq;
                    scoreWavedashZone(z, hmnFs.x, ws->last.outcome == kWdOutcomeSuccess);
                    spawnWavedashZone(z, hmnFs.x, hmnFs.y, hmnLr, brake, geom);
                }
                if (z.active)
                {
                    float cl, cr, ct, cb;
                    computeWavedashCameraRect(hmnFs.x, hmnFs.y, z, cl, cr, ct, cb);
                    applyCameraRect(hmn.fighter, cl, cr, ct, cb);
                }
            }
        }
        // ---- 4. L-Cancel & Float/Hover/DJC: Target Management ---------------
        else if ((d == kPracticeDrillLCancel || d == kPracticeDrillHoverFloat) && tMode != kTargetOff)
        {
            bool hmnAir = ramOk(hm->m_situationModule) && hm->m_situationModule->getKind() == Situation_Air;
            bool doRepos = (s_net->pendingDpadDown != 0u) && !hmnAir
                           && (tMode == kTargetStationary || tMode == kTargetShielding);
            s_net->pendingDpadDown = 0u;

            float cpuPct = static_cast<float>(main::settings().cpu.percent);
            for (int i = 0; i < n; i++)
            {
                if (!refs[i].cpu || !ramOk(refs[i].fighter))
                {
                    continue;
                }
                if (tMode == kTargetShielding)
                {
                    cpu::setShieldHealth(refs[i].fighter, static_cast<float>(cpu::kShieldFull));
                }
                bool needPlace = doRepos;
                if (doRepos)
                {
                    computeTargetAnchor(hmnFs.x, readLr(hm), 12.0f, geom, s_net->target.anchorX, s_net->target.anchorY,
                                        s_net->target.anchorLr);
                    s_net->target.anchorValid = true;
                }
                else if (tMode == kTargetStationary || tMode == kTargetShielding)
                {
                    game::FighterState cfs;
                    needPlace = game::readState(refs[i], cfs) && game::outOfPlay(cfs.status, true)
                                && cfs.status != game::kStatusDead && cfs.status != game::kStatusStandby;
                }
                if (needPlace)
                {
                    float ax = s_net->target.anchorValid ? s_net->target.anchorX : 14.0f;
                    float ay = s_net->target.anchorValid ? s_net->target.anchorY : (geom.floor.valid ? geom.floor.centerY : 0.0f);
                    float alr = s_net->target.anchorValid ? s_net->target.anchorLr : -1.0f;
                    placeStanding(refs[i], ax, ay, false, cpuPct, alr);
                    setFighterCorrect(refs[i], soGroundShapeImpl::Correct_Ground_Cliff_Stop);
                    if (doRepos && refs[i].playerNo >= 0 && refs[i].playerNo < game::kMaxFighters)
                    {
                        cpu::optionsResetSlot(refs[i].playerNo);
                    }
                }
            }
        }

        syncDebugBlock(resolvedPsMode, tMode);
    }

    bool drillPracticeCpuStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons)
    {
        (void)lastButtons;
        if (s_net == NULL || !who.cpu || f == NULL || s_net->activeDrill == kPracticeDrillNone)
        {
            return false;
        }
        if (s_net->cpuInStandby)
        {
            out.buttons = 0u;
            out.x = 0.0f;
            out.y = 0.0f;
            return true;
        }

        const u8 d = s_net->activeDrill;
        if (d != kPracticeDrillPowershield && d != kPracticeDrillLCancel && d != kPracticeDrillHoverFloat)
        {
            return false;
        }

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        game::FighterRef hmn;
        game::FighterState hmnFs;
        if (!findPrimaryHuman(refs, n, hmn) || !game::readState(hmn, hmnFs))
        {
            return false;
        }
        StageGeometry geom;
        liveGeometry(geom, false, false);
        const float floorL = geom.floor.valid ? geom.floor.leftX : -68.0f;
        const float floorR = geom.floor.valid ? geom.floor.rightX : 68.0f;

        if (d == kPracticeDrillPowershield)
        {
            s_net->psCpu.rng ^= game::frameCounter() * 0x9E3779B9u;
            PsCpuInput in;
            in.cpuFtKind = game::fighterKind(who.entryId, who.sub);
            in.cpuStatus = v.status;
            in.cpuAir = v.air;
            in.cpuCanAct = cpu::canAct(v);
            in.cpuOutOfPlay = game::outOfPlay(v.status, true);
            in.hmnOutOfPlay = game::outOfPlay(hmnFs.status, true);
            in.cpuX = v.x;
            in.cpuLr = v.lr;
            in.hmnX = hmnFs.x;
            in.floorLeft = floorL;
            in.floorRight = floorR;

            PsCpuOutput po;
            stepPsPracticeCpu(in, s_net->cfg, s_net->psCpu, po);
            if (!po.active)
            {
                return false;
            }
            out.buttons = po.buttons;
            out.x = po.stickX;
            out.y = po.stickY;
            return true;
        }

        const u8 tMode = activeTargetMode(d, s_net->cfg);
        if (tMode == kTargetOff)
        {
            return false;
        }
        TargetCpuInput tin;
        tin.targetMode = tMode;
        tin.cpuAir = v.air;
        tin.cpuCanAct = cpu::canAct(v);
        tin.cpuOutOfPlay = game::outOfPlay(v.status, true);
        tin.cpuX = v.x;
        tin.cpuLr = v.lr;
        tin.hmnX = hmnFs.x;
        tin.floorLeft = floorL;
        tin.floorRight = floorR;

        TargetCpuOutput to;
        stepTargetCpu(tin, s_net->target, to);
        if (!to.active)
        {
            return false;
        }
        out.buttons = to.buttons;
        out.x = to.stickX;
        out.y = to.stickY;
        return true;
    }

    // Draws the Wavedash / Waveland GX floor target zone in the 3D world camera.
    void drillPracticeDraw(const game::MatchState& match)
    {
        if (s_net == NULL || !match.running || match.paused || lab::menuOpen()
            || s_net->activeDrill != kPracticeDrillWavedash || s_net->cfg.wdTargetZone == 0u || !s_net->wdZone.active)
        {
            return;
        }
        gfCameraManager* camMgr = gfCameraManager::getManager();
        if (!ramOk(camMgr))
        {
            return;
        }

        const WdZoneState& z = s_net->wdZone;
        camMgr->m_cameras[0].setGX();
        gfx::forgetState();

        gfx::Rgba fillTop = 0x22B8CC48u;
        gfx::Rgba fillBot = 0x0C4858A8u;
        gfx::Rgba borderCol = 0xFFD300F0u;
        if (z.flashFrames > 0u)
        {
            if (z.lastOutcome == kWdZoneHit)
            {
                fillTop = 0x4CE68888u;
                fillBot = 0x1E8048D0u;
                borderCol = 0xFFE866FFu;
            }
            else if (z.lastOutcome == kWdZoneShort || z.lastOutcome == kWdZoneOvershoot)
            {
                fillTop = 0xFF665878u;
                fillBot = 0x902820C0u;
                borderCol = 0xFF8878F0u;
            }
        }

        const float y0 = z.y + 0.15f;
        const float y1 = y0 + 2.2f;
        const float midX = 0.5f * (z.minX + z.maxX);
        const float box[5][2] = {
            {z.minX, y0}, {z.maxX, y0}, {z.maxX, y1}, {z.minX, y1}, {z.minX, y0}
        };
        const float posts[6][2] = {
            {z.minX, y1}, {z.minX, y1 + 2.4f},
            {z.maxX, y1}, {z.maxX, y1 + 2.4f},
            {midX, y0},   {midX, y1 + 1.2f}
        };

        gfx::prepare();
        GXBegin(GX_QUADS, GX_VTXFMT1, 4);
        gfx::vertex(z.minX, y1, fillTop);
        gfx::vertex(z.maxX, y1, fillTop);
        gfx::vertex(z.maxX, y0, fillBot);
        gfx::vertex(z.minX, y0, fillBot);
        GXEnd();

        gfx::lineBegin(GX_LINESTRIP, 5, 18);
        emitVerts(box, 5, borderCol);
        GXEnd();

        gfx::lineBegin(GX_LINES, 6, 16);
        emitVerts(posts, 6, borderCol);
        GXEnd();

        gfx::forgetState();
        s_net->debug.drawCalls++;
    }
}
}
