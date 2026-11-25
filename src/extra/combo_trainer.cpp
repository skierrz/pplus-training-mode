#include "extra/combo_trainer.h"

#include <GX/GXTypes.h>
#include <ft/fighter.h>
#include <so/collision/so_collision_hit_module_impl.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/posture/so_posture_module_impl.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/status/so_status_module_impl.h>
#include <so/stop/so_stop_module_impl.h>
#include <string.h>

#include "extra/frame_data.h"
#include "extra/hud_draw.h"
#include "extra/net_alloc.h"
#include "extra/pplus_recovery.h"
#include "extra/scenario_presets.h"
#include "extra/stage_geom.h"
#include "main/cpu/options.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/status_ids.h"
#include "main/gfx/draw.h"
#include "main/settings.h"
#include "main/storage/notice.h"
#include "shared/game/frame.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    enum {
        kComboDebugMagic = 0x544D4354, // "TMCT"
        kCliffFirst = 0x73,
        kCliffWait = 0x75,
        kCliffLast = 0x7B
    };

    // Per-player snapshot exposed for live RAM verification in Dolphin tests.
    struct ComboDebugSlot {
        u32 resetCount;
        u32 recoveryResets;
        u8 lastCorrectWritten;
        u8 observedCorrect;
        u8 activeMeterMask;
        u8 restorePending;
        float anchorX;
        float anchorY;
        float hitlagRatio;
        float hitstunRatio;
        float shieldStunRatio;
        float diX;
        float diY;
        float iasaRatio;
        float ledgeIntangRatio;
        float intangRatio;
        s16 hitlagFrames;
        s16 hitstunFrames;
        s16 shieldStunFrames;
        s16 iasaFrames;
        s16 ledgeIntangFrames;
        s16 intangFrames;
    };

    struct ComboDebugBlock {
        u32 magic; // kComboDebugMagic
        u32 frame;
        ComboDebugSlot slots[game::kMaxFighters];
    };

    struct ComboNet {
        ComboSlotState slots[game::kMaxFighters];
        ComboMeterValues meters[game::kMaxFighters];
        bool meterValid[game::kMaxFighters];
        float hmnAnchorX;
        float hmnAnchorY;
        float hmnAnchorLr;
        bool hmnAnchorValid;
        bool hmnRestoreCorrect;
        u8 hmnRestoreCorrectKind;
        s8 hmnPlacedPlayer;
        ComboDebugBlock debug;
    };

    static ComboNet* s_net = NULL;
    static u32 s_rng = 0x51A7F3C9u;

    static void clearComboWarpIntang(const game::FighterRef& ref)
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

    static void finishComboStanding(const game::FighterRef& ref, bool setNoclip)
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
                m->m_statusModule->changeStatusForce(game::kStatusWait, pair[i]->m_moduleAccesser);
                m->m_groundModule->setCorrect(
                    setNoclip ? soGroundShapeImpl::Correct_None : soGroundShapeImpl::Correct_Ground_Cliff_Stop, 0);
            }
            else
            {
                m->m_statusModule->changeStatusForce(game::kStatusFall, pair[i]->m_moduleAccesser);
                m->m_groundModule->setCorrect(
                    setNoclip ? soGroundShapeImpl::Correct_None : soGroundShapeImpl::Correct_Air, 0);
            }
        }
    }

    void comboTrainerSetAnchors(float hmnX, float hmnY, float hmnLr, float cpuX, float cpuY)
    {
        if (s_net == NULL)
        {
            return;
        }
        s_net->hmnAnchorX = hmnX;
        s_net->hmnAnchorY = hmnY;
        s_net->hmnAnchorLr = hmnLr != 0.0f ? hmnLr : 1.0f;
        s_net->hmnAnchorValid = true;
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (refs[i].cpu && p >= 0 && p < game::kMaxFighters)
            {
                s_net->slots[p].anchorX = cpuX;
                s_net->slots[p].anchorY = cpuY;
                s_net->slots[p].anchorValid = true;
            }
        }
    }

    void comboTrainerRepositionCpu()
    {
        if (s_net == NULL)
        {
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
            return;
        }
        game::FighterState hmnFs;
        if (!game::readState(hmnRef, hmnFs))
        {
            return;
        }
        const soModuleEnumeration* hm = game::fighterModules(hmnRef.fighter);
        if (hm == NULL || (game::isRam(hm->m_situationModule) && hm->m_situationModule->getKind() == Situation_Air))
        {
            return;
        }
        float lr = 1.0f;
        if (game::isRam(hm->m_postureModule))
        {
            float rawLr = hm->m_postureModule->getLr();
            if (rawLr < 0.0f)
            {
                lr = -1.0f;
            }
        }
        float targetX = hmnFs.x + lr * 10.0f;
        float targetY = hmnFs.y;
        StageGeometry geom;
        if (queryStageGeometry(geom, false, true) && geom.floor.valid)
        {
            bool onPlat = false;
            if (hmnFs.y > geom.floor.centerY + 2.0f)
            {
                for (int pi = 0; pi < geom.platformCount; pi++)
                {
                    const StagePlatform& plat = geom.platforms[pi];
                    if (!plat.valid)
                    {
                        continue;
                    }
                    float dy = hmnFs.y - plat.y;
                    if (dy < 0.0f) dy = -dy;
                    if (dy <= 4.0f && hmnFs.x >= plat.leftX - 2.0f && hmnFs.x <= plat.rightX + 2.0f)
                    {
                        float lo = plat.leftX + 2.0f;
                        float hi = plat.rightX - 2.0f;
                        targetX = lo <= hi ? geomClamp(targetX, lo, hi) : (plat.leftX + plat.rightX) * 0.5f;
                        targetY = plat.y;
                        onPlat = true;
                        break;
                    }
                }
                if (!onPlat)
                {
                    targetY = geom.floor.centerY;
                }
            }
            if (!onPlat)
            {
                targetX = geomClamp(targetX, geom.floor.leftX + 6.0f, geom.floor.rightX - 6.0f);
            }
        }
        int p = cpuRef.playerNo;
        if (p >= 0 && p < game::kMaxFighters)
        {
            ComboSlotState& st = s_net->slots[p];
            st.anchorX = targetX;
            st.anchorY = targetY;
            st.anchorValid = true;
            st.wasInHitstun = false;
            st.postResetArmed = false;
            st.comboTimer = 0;
            st.postResetTimer = 0;
            st.restoreCorrect = true;
            st.restoreCorrectKind = kCorrectGroundCliffStop;
            st.lastCorrectWritten = kCorrectNone;
        }
        s_net->hmnAnchorX = hmnFs.x;
        s_net->hmnAnchorY = hmnFs.y;
        s_net->hmnAnchorLr = lr;
        s_net->hmnAnchorValid = true;
        float cpuDmg = static_cast<float>(main::settings().cpu.percent);
        placeStanding(cpuRef, targetX, targetY, true, cpuDmg, -lr);
        finishComboStanding(cpuRef, true);
        clearComboWarpIntang(cpuRef);
        cpu::optionsResetSlot(p);
        pplusRecoveryReset(p);
        storage::notice(storage::kNoticeOk, "CPU anchor updated");
    }

    void comboTrainerReset()
    {
        if (s_net == NULL)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p >= 0 && p < game::kMaxFighters && s_net->slots[p].restoreCorrect)
            {
                setFighterCorrect(refs[i], s_net->slots[p].restoreCorrectKind);
            }
            if (s_net->hmnRestoreCorrect && p == s_net->hmnPlacedPlayer)
            {
                setFighterCorrect(refs[i], s_net->hmnRestoreCorrectKind);
            }
        }
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            comboSlotReset(s_net->slots[i]);
            memset(&s_net->meters[i], 0, sizeof(s_net->meters[i]));
            s_net->meterValid[i] = false;
        }
        s_net->hmnAnchorX = 0.0f;
        s_net->hmnAnchorY = 0.0f;
        s_net->hmnAnchorLr = 1.0f;
        s_net->hmnAnchorValid = false;
        s_net->hmnRestoreCorrect = false;
        s_net->hmnRestoreCorrectKind = kCorrectGroundCliffStop;
        s_net->hmnPlacedPlayer = -1;
        memset(&s_net->debug, 0, sizeof(s_net->debug));
        s_net->debug.magic = kComboDebugMagic;
    }

    void comboTrainerAttach()
    {
        freeNetPtr(s_net);
        s_net = allocNetArray<ComboNet>(1);
        s_rng = game::frameCounter() | 1u;
        if (s_net == NULL)
        {
            return;
        }
        comboTrainerReset();
        TM_LOG("combo: debug block at %08x\n", &s_net->debug);
    }

    void comboTrainerDetach()
    {
        comboTrainerReset();
        freeNetPtr(s_net);
    }

    const ComboMeterValues* comboTrainerMeters(int playerNo)
    {
        if (s_net == NULL || playerNo < 0 || playerNo >= game::kMaxFighters || !s_net->meterValid[playerNo])
        {
            return NULL;
        }
        return &s_net->meters[playerNo];
    }

    static void copyDebugSlot(int p, Fighter* f)
    {
        ComboDebugSlot& d = s_net->debug.slots[p];
        const ComboSlotState& st = s_net->slots[p];
        const ComboMeterValues& mv = s_net->meters[p];
        d.resetCount = st.resetCount;
        d.recoveryResets = st.recoveryResets;
        d.lastCorrectWritten = st.lastCorrectWritten;
        int curCorr = getFighterCorrect(f);
        d.observedCorrect = curCorr >= 0 ? static_cast<u8>(curCorr) : 0xFF;
        d.activeMeterMask = mv.activeMask;
        d.restorePending = st.restoreCorrect ? 1 : 0;
        d.anchorX = st.anchorX;
        d.anchorY = st.anchorY;
        d.hitlagRatio = mv.hitlagRatio;
        d.hitstunRatio = mv.hitstunRatio;
        d.shieldStunRatio = mv.shieldStunRatio;
        d.diX = mv.diX;
        d.diY = mv.diY;
        d.iasaRatio = mv.iasaRatio;
        d.ledgeIntangRatio = mv.ledgeIntangRatio;
        d.intangRatio = mv.intangRatio;
        d.hitlagFrames = mv.hitlagFrames;
        d.hitstunFrames = mv.hitstunFrames;
        d.shieldStunFrames = mv.shieldStunFrames;
        d.iasaFrames = mv.iasaFrames;
        d.ledgeIntangFrames = mv.ledgeIntangFrames;
        d.intangFrames = mv.intangFrames;
    }

    void comboTrainerFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        const main::ComboTrainerSettings& cfg = main::settings().comboTrainer;
        s_net->debug.frame = game::frameCounter();
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            s_net->meterValid[i] = false;
        }

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        game::FighterRef hmnRef;
        bool haveHmn = false;
        for (int i = 0; i < n; i++)
        {
            if (!refs[i].cpu && !haveHmn)
            {
                hmnRef = refs[i];
                haveHmn = true;
            }
            if (s_net->hmnRestoreCorrect && refs[i].playerNo == s_net->hmnPlacedPlayer)
            {
                setFighterCorrect(refs[i], s_net->hmnRestoreCorrectKind);
                s_net->hmnRestoreCorrect = false;
            }
        }
        if (!cfg.enabled)
        {
            s_net->hmnAnchorValid = false;
        }
        else if (haveHmn && !s_net->hmnAnchorValid)
        {
            game::FighterState hfs;
            const soModuleEnumeration* hm = game::fighterModules(hmnRef.fighter);
            bool hmnGrounded = hm != NULL && game::isRam(hm->m_situationModule)
                               && hm->m_situationModule->getKind() != Situation_Air;
            if (hmnGrounded && game::readState(hmnRef, hfs) && !game::outOfPlay(hfs.status, true))
            {
                s_net->hmnAnchorX = hfs.x;
                s_net->hmnAnchorY = hfs.y;
                s_net->hmnAnchorLr = (game::isRam(hm->m_postureModule) && hm->m_postureModule->getLr() < 0.0f)
                                         ? -1.0f
                                         : 1.0f;
                s_net->hmnAnchorValid = true;
            }
        }
        StageGeometry geom;
        bool haveGeom = cfg.enabled ? queryStageGeometry(geom) : false;

        for (int i = 0; i < n; i++)
        {
            const game::FighterRef& ref = refs[i];
            int p = ref.playerNo;
            if (p < 0 || p >= game::kMaxFighters)
            {
                continue;
            }
            const soModuleEnumeration* m = game::fighterModules(ref.fighter);
            game::FighterState fs;
            if (m == NULL || !game::readState(ref, fs))
            {
                continue;
            }
            game::Body body;
            bool haveBody = game::readBody(ref.fighter, body);

            ComboStepInput in;
            memset(&in, 0, sizeof(in));
            in.status = fs.status;
            in.hitlag = game::isRam(m->m_stopModule) ? m->m_stopModule->getHitStopRealFrame() : 0;
            if (in.hitlag < 0)
            {
                in.hitlag = 0;
            }
            in.hitstun = fs.hitstun;
            in.shieldStun = fs.shieldStun;
            ActIn actIn = frameDataActIn(ref.fighter);
            in.framesUntilIasa = actIn.frames > 0 ? actIn.frames : 0;
            in.xluFrames = haveBody ? body.xluFrames : 0;
            in.invFrames = haveBody ? static_cast<int>(body.invFrames) : 0;
            in.bodyIntangible = haveBody && body.intangible != 0;
            in.onLedge = fs.status >= kCliffFirst && fs.status <= kCliffLast;
            in.air = game::isRam(m->m_situationModule) && m->m_situationModule->getKind() == Situation_Air;
            in.outOfPlay = game::outOfPlay(fs.status, true);
            in.x = fs.x;
            in.y = fs.y;
            in.stickX = haveBody ? body.stickX : 0.0f;
            in.stickY = haveBody ? body.stickY : 0.0f;

            if (haveGeom && geom.floor.valid)
            {
                float lowY = geomMax(geom.blast.bottom + 24.0f, geom.floor.centerY - 95.0f);
                in.belowRecoveryReset = fs.y < lowY || fs.status == kCliffWait;
                bool onMainFloor = !in.air && fs.x >= geom.floor.leftX && fs.x <= geom.floor.rightX;
                in.landedSafeOnStage = onMainFloor && fs.status <= game::kStatusLandingFallSpecial
                                       && !(fs.status >= game::kStatusJump && fs.status <= 0x10);
            }

            // Compute all 7 meters for every fighter (human and CPU).
            computeComboMeters(cfg.enabledMeters, in, s_net->slots[p], s_net->meters[p]);
            s_net->meterValid[p] = true;

            // Reset / noclip / recovery-trainer actions apply only to CPU fighters.
            if (ref.cpu)
            {
                ComboStepAction act;
                stepComboTrainer(cfg, in, s_net->slots[p], act);
                if (act.restoreCorrect)
                {
                    setFighterCorrect(ref, act.restoreCorrectKind);
                }
                if (act.doWarp)
                {
                    s_rng ^= game::frameCounter() * 0x9E3779B9u;
                    float wx = s_net->slots[p].anchorX;
                    float wy = s_net->slots[p].anchorY;
                    if (act.pickOffstagePos && haveGeom)
                    {
                        sampleRecoveryCandidate(geom, s_rng, wx, wy, scenarioPresetsEdgeguardSide());
                    }
                    else if (act.pickRandomPos && haveGeom)
                    {
                        findSafeRandomPoint(geom, s_rng, cfg.randOnGround != 0, wx, wy);
                    }
                    float dmg = -1.0f;
                    if (act.pickRandomDamage)
                    {
                        dmg = static_cast<float>(geomNextRng(s_rng) % 100u);
                    }
                    else if (act.applyFixedDamage)
                    {
                        dmg = static_cast<float>(cfg.fixedDamageMode != main::kComboDmgCpu ? cfg.fixedDamage
                                                                                           : main::settings().cpu.percent);
                    }
                    if (act.warpFalling)
                    {
                        placeFalling(ref, wx, wy, act.setCorrectNone, dmg);
                        refreshAirResources(ref, false);
                    }
                    else
                    {
                        placeStanding(ref, wx, wy, act.setCorrectNone, dmg);
                        finishComboStanding(ref, act.setCorrectNone);
                    }
                    clearComboWarpIntang(ref);
                    cpu::optionsResetSlot(p);
                    pplusRecoveryReset(p);

                    // Reset P1 position + damage back to starting anchor when Reset P1 is On.
                    if (act.resetP1 && haveHmn && s_net->hmnAnchorValid)
                    {
                        float hmnDmg = static_cast<float>(main::settings().general.playerPercent);
                        float hlr = wx >= s_net->hmnAnchorX ? 1.0f : -1.0f;
                        placeStanding(hmnRef, s_net->hmnAnchorX, s_net->hmnAnchorY, act.setCorrectNone, hmnDmg, hlr);
                        finishComboStanding(hmnRef, act.setCorrectNone);
                        clearComboWarpIntang(hmnRef);
                        if (act.setCorrectNone)
                        {
                            s_net->hmnRestoreCorrect = true;
                            s_net->hmnRestoreCorrectKind = kCorrectGroundCliffStop;
                            s_net->hmnPlacedPlayer = static_cast<s8>(hmnRef.playerNo);
                        }
                    }
                }
                else if (act.setCorrectNone)
                {
                    setFighterCorrect(ref, kCorrectNone);
                }
            }

            copyDebugSlot(p, ref.fighter);
        }
    }

    // Draws a minimal neutral world-space horizontal bar centred on (cx, cy).
    static void drawBar(float cx, float cy, float w, float h, float ratio, gfx::Rgba fillColor)
    {
        float x0 = cx - w * 0.5f;
        float y0 = cy - h * 0.5f;
        // Neutral dark frame + muted backing.
        gfx::fillRect(x0 - 0.35f, y0 - 0.35f, w + 0.7f, h + 0.7f, 0x101010C8);
        gfx::fillRect(x0, y0, w, h, 0x262626B0);
        if (ratio > 0.0f)
        {
            float fw = w * (ratio > 1.0f ? 1.0f : ratio);
            gfx::fillRect(x0, y0, fw, h, fillColor);
        }
    }

    // Draws a minimal neutral world-space DI stick ring centred on (cx, cy).
    static void drawDiRing(float cx, float cy, float r, float dx, float dy)
    {
        gfx::lineBegin(GX_LINESTRIP, 17, 10);
        for (int i = 0; i < 17; i++)
        {
            float ux, uy;
            unitPoint16(i, ux, uy);
            gfx::vertex(cx + ux * r, cy + uy * r, 0xD8D8D8CC);
        }
        gfx::lineBegin(GX_LINES, 2, 16);
        gfx::vertex(cx, cy, 0xFFFFFFFF);
        gfx::vertex(cx + dx * r, cy + dy * r, 0xFF9830FF);
    }

    void comboTrainerDraw(const game::MatchState& match)
    {
        const main::ComboTrainerSettings& cfg = main::settings().comboTrainer;
        if (s_net == NULL || !match.running || match.paused || cfg.enabledMeters == 0)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p < 0 || p >= game::kMaxFighters || !s_net->meterValid[p])
            {
                continue;
            }
            const ComboMeterValues& mv = s_net->meters[p];
            if (mv.activeMask == 0)
            {
                continue;
            }
            const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
            if (m == NULL || !game::isRam(m->m_groundModule))
            {
                continue;
            }
            Vec2f top = m->m_groundModule->getUpPos(0);
            float cx = top.m_x;
            float cy = top.m_y + 10.5f;

            // Main state bar (priority: shieldstun > hitlag > hitstun > timeUntilIASA).
            const float kBarW = 14.0f;
            const float kBarH = 2.4f;
            if ((mv.activeMask & (main::kMeterShieldstun | main::kMeterHitlag | main::kMeterHitstun | main::kMeterIasa))
                != 0)
            {
                float ratio = 0.0f;
                gfx::Rgba col = 0xCCCCCCFF;
                if ((mv.activeMask & main::kMeterShieldstun) != 0)
                {
                    ratio = mv.shieldStunRatio;
                    col = 0x3898FFFF;
                }
                else if ((mv.activeMask & main::kMeterHitlag) != 0)
                {
                    ratio = mv.hitlagRatio;
                    col = 0x40D060FF;
                }
                else if ((mv.activeMask & main::kMeterHitstun) != 0)
                {
                    ratio = mv.hitstunRatio;
                    col = 0xFF9830FF;
                }
                else
                {
                    ratio = mv.iasaRatio;
                    col = 0xD0D840FF;
                }
                drawBar(cx, cy, kBarW, kBarH, ratio, col);
                cy += kBarH + 1.4f;
            }

            // Intangibility / Ledge Intangibility bar.
            if ((mv.activeMask & (main::kMeterLedgeIntang | main::kMeterIntang)) != 0)
            {
                bool ledge = (mv.activeMask & main::kMeterLedgeIntang) != 0;
                float ratio = ledge ? mv.ledgeIntangRatio : mv.intangRatio;
                gfx::Rgba col = ledge ? 0x40D090FF : 0x58B8F0FF;
                drawBar(cx, cy, 11.0f, 1.8f, ratio, col);
                cy += 3.0f;
            }

            // DI stick indicator ring.
            if ((mv.activeMask & main::kMeterDi) != 0)
            {
                drawDiRing(cx, cy + 2.2f, 2.8f, mv.diX, mv.diY);
            }
        }
    }
}
}
