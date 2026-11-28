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
#include "main/cpu/view.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/status_ids.h"
#include "main/gfx/draw.h"
#include "main/settings.h"
#include "main/storage/notice.h"
#include "main/ui/state_colors.h"
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

    void comboTrainerSetAnchors(float hmnX, float hmnY, float hmnLr, float cpuX, float cpuY)
    {
        ComboNet* net = s_net;
        if (net == NULL)
        {
            return;
        }
        net->hmnAnchorX = hmnX;
        net->hmnAnchorY = hmnY;
        net->hmnAnchorLr = hmnLr != 0.0f ? hmnLr : 1.0f;
        net->hmnAnchorValid = true;
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (refs[i].cpu && p >= 0 && p < game::kMaxFighters)
            {
                net->slots[p].anchorX = cpuX;
                net->slots[p].anchorY = cpuY;
                net->slots[p].anchorValid = true;
            }
        }
    }

    void comboTrainerRepositionCpu()
    {
        ComboNet* net = s_net;
        if (net == NULL)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        game::FighterRef hmnRef;
        game::FighterRef cpuRef;
        bool haveHmn = false;
        bool haveCpu = false;
        if (!findHmnAndCpu(refs, n, hmnRef, cpuRef, haveHmn, haveCpu))
        {
            return;
        }
        game::FighterState hmnFs;
        if (!game::readState(hmnRef, hmnFs))
        {
            return;
        }
        const soModuleEnumeration* hm = game::fighterModules(hmnRef.fighter);
        if (hm == NULL || isFighterAir(hm))
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
            ComboSlotState& st = net->slots[p];
            u32 prevResets = st.resetCount;
            u32 prevRecResets = st.recoveryResets;
            comboSlotReset(st);
            st.resetCount = prevResets;
            st.recoveryResets = prevRecResets;
            st.anchorX = targetX;
            st.anchorY = targetY;
            st.anchorValid = true;
            st.restoreCorrect = true;
            st.restoreCorrectKind = kCorrectGroundCliffStop;
            st.lastCorrectWritten = kCorrectNone;
        }
        net->hmnAnchorX = hmnFs.x;
        net->hmnAnchorY = hmnFs.y;
        net->hmnAnchorLr = lr;
        net->hmnAnchorValid = true;
        float cpuDmg = i2f(main::settings().cpu.percent);
        warpGroundClean(cpuRef, targetX, targetY, true, cpuDmg, -lr, true);
        cpu::optionsResetSlot(p);
        pplusRecoveryReset(p);
        storage::notice(storage::kNoticeOk, "CPU anchor updated");
    }

    void comboTrainerReset()
    {
        ComboNet* net = s_net;
        if (net == NULL)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p >= 0 && p < game::kMaxFighters && net->slots[p].restoreCorrect)
            {
                setFighterCorrect(refs[i], net->slots[p].restoreCorrectKind);
            }
            if (net->hmnRestoreCorrect && p == net->hmnPlacedPlayer)
            {
                setFighterCorrect(refs[i], net->hmnRestoreCorrectKind);
            }
        }
        memset(net, 0, sizeof(*net));
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            net->slots[i].restoreCorrectKind = kCorrectAir;
            net->slots[i].lastCorrectWritten = 0xFF;
        }
        net->hmnAnchorLr = 1.0f;
        net->hmnRestoreCorrectKind = kCorrectGroundCliffStop;
        net->hmnPlacedPlayer = -1;
        net->debug.magic = kComboDebugMagic;
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
        ComboNet* net = s_net;
        ComboDebugSlot& d = net->debug.slots[p];
        const ComboSlotState& st = net->slots[p];
        const ComboMeterValues& mv = net->meters[p];
        d.resetCount = st.resetCount;
        d.recoveryResets = st.recoveryResets;
        d.lastCorrectWritten = st.lastCorrectWritten;
        int curCorr = getFighterCorrect(f);
        d.observedCorrect = curCorr >= 0 ? static_cast<u8>(curCorr) : 0xFF;
        d.activeMeterMask = mv.activeMask;
        d.restorePending = st.restoreCorrect ? 1 : 0;
        d.anchorX = st.anchorX;
        d.anchorY = st.anchorY;
        const size_t kMeterTailBytes =
            static_cast<size_t>(reinterpret_cast<const char*>(&mv + 1) - reinterpret_cast<const char*>(&mv.hitlagRatio));
        memcpy(&d.hitlagRatio, &mv.hitlagRatio, kMeterTailBytes);
    }

    void comboTrainerFrame()
    {
        ComboNet* net = s_net;
        if (net == NULL)
        {
            return;
        }
        const main::ComboTrainerSettings& cfg = main::settings().comboTrainer;
        net->debug.frame = game::frameCounter();
        memset(net->meterValid, 0, sizeof(net->meterValid));

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
            if (net->hmnRestoreCorrect && refs[i].playerNo == net->hmnPlacedPlayer)
            {
                setFighterCorrect(refs[i], net->hmnRestoreCorrectKind);
                net->hmnRestoreCorrect = false;
            }
        }
        if (!cfg.enabled)
        {
            net->hmnAnchorValid = false;
        }
        else if (haveHmn && !net->hmnAnchorValid)
        {
            game::FighterState hfs;
            const soModuleEnumeration* hm = game::fighterModules(hmnRef.fighter);
            if (hm != NULL && !isFighterAir(hm) && game::readState(hmnRef, hfs) && !game::outOfPlay(hfs.status, true))
            {
                net->hmnAnchorX = hfs.x;
                net->hmnAnchorY = hfs.y;
                net->hmnAnchorLr = (game::isRam(hm->m_postureModule) && hm->m_postureModule->getLr() < 0.0f)
                                       ? -1.0f
                                       : 1.0f;
                net->hmnAnchorValid = true;
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
            in.hitlag = readFighterHitlag(m);
            in.hitstun = fs.hitstun;
            in.shieldStun = fs.shieldStun;
            ActIn actIn = frameDataActIn(ref.fighter);
            in.framesUntilIasa = actIn.frames > 0 ? actIn.frames : 0;
            in.xluFrames = haveBody ? body.xluFrames : 0;
            in.invFrames = haveBody ? static_cast<int>(body.invFrames) : 0;
            in.bodyIntangible = haveBody && body.intangible != 0;
            in.onLedge = fs.status >= kCliffFirst && fs.status <= kCliffLast;
            in.ignoreLedgeReset = !scenarioPresetsEdgeguardResetOnLedge();
            in.air = isFighterAir(m);
            in.outOfPlay = game::outOfPlay(fs.status, true);
            in.x = fs.x;
            in.y = fs.y;
            in.stickX = haveBody ? body.stickX : 0.0f;
            in.stickY = haveBody ? body.stickY : 0.0f;

            if (haveGeom && geom.floor.valid)
            {
                float lowY = geomMax(geom.blast.bottom + 24.0f, geom.floor.centerY - 95.0f);
                in.belowRecoveryReset = fs.y < lowY;
                bool actionable = false;
                if (ref.cpu && cfg.recoveryTrainer && !in.air && in.hitstun == 0 && in.hitlag == 0)
                {
                    cpu::View cv;
                    actionable = cpu::readView(ref.fighter, cv) && cpu::canAct(cv);
                }
                in.landedSafeOnStage = isLandedSafeOnStage(fs.status, in.air, actionable, in.hitstun, in.hitlag, fs.x,
                                                           geom.floor.leftX, geom.floor.rightX);
            }

            // Compute all 7 meters for every fighter (human and CPU).
            ComboSlotState& st = net->slots[p];
            computeComboMeters(cfg.enabledMeters, in, st, net->meters[p]);
            net->meterValid[p] = true;

            // Reset / noclip / recovery-trainer actions apply only to CPU fighters.
            if (ref.cpu)
            {
                ComboStepAction act;
                stepComboTrainer(cfg, in, st, act);
                if (act.restoreCorrect)
                {
                    setFighterCorrect(ref, act.restoreCorrectKind);
                }
                if (act.doWarp)
                {
                    s_rng ^= game::frameCounter() * 0x9E3779B9u;
                    float wx = st.anchorX;
                    float wy = st.anchorY;
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
                        dmg = i2f(static_cast<int>(geomNextRng(s_rng) % 100u));
                    }
                    else if (act.applyFixedDamage)
                    {
                        dmg = i2f(cfg.fixedDamageMode != main::kComboDmgCpu ? cfg.fixedDamage
                                                                            : main::settings().cpu.percent);
                    }
                    bool customEdgeguard = act.pickOffstagePos && haveGeom
                                           && scenarioPresetsEdgeguardRespawn(ref, geom, s_rng, act.setCorrectNone,
                                                                              dmg);
                    if (!customEdgeguard)
                    {
                        if (act.warpFalling)
                        {
                            warpAirClean(ref, wx, wy, act.setCorrectNone, dmg, 0.0f, true, false);
                        }
                        else
                        {
                            warpGroundClean(ref, wx, wy, act.setCorrectNone, dmg, 0.0f, true);
                        }
                        cpu::optionsResetSlot(p);
                    }
                    pplusRecoveryReset(p);

                    // Reset P1 position + damage back to starting anchor when Reset P1 is On.
                    if (act.resetP1 && haveHmn && net->hmnAnchorValid)
                    {
                        float hmnDmg = i2f(main::settings().general.playerPercent);
                        float hlr = wx >= net->hmnAnchorX ? 1.0f : -1.0f;
                        warpGroundClean(hmnRef, net->hmnAnchorX, net->hmnAnchorY, act.setCorrectNone, hmnDmg, hlr,
                                        true);
                        if (act.setCorrectNone)
                        {
                            net->hmnRestoreCorrect = true;
                            net->hmnRestoreCorrectKind = kCorrectGroundCliffStop;
                            net->hmnPlacedPlayer = static_cast<s8>(hmnRef.playerNo);
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
        ComboNet* net = s_net;
        const main::ComboTrainerSettings& cfg = main::settings().comboTrainer;
        if (net == NULL || !match.running || match.paused || cfg.enabledMeters == 0)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p < 0 || p >= game::kMaxFighters || !net->meterValid[p])
            {
                continue;
            }
            const ComboMeterValues& mv = net->meters[p];
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
            // Unified Layer 2 valence palette: Hot Amber-Orange for Stun/Hitlag, Emerald for IASA.
            u8 cbMode = ui::activeColorblindMode();
            static const u8 kMainBit[4] = {
                main::kMeterShieldstun, main::kMeterHitlag, main::kMeterHitstun, main::kMeterIasa
            };
            static const u8 kMainState[4] = {
                ui::kStateColorStun, ui::kStateColorStun, ui::kStateColorStun, ui::kStateColorActionable
            };
            const float ratios[4] = {mv.shieldStunRatio, mv.hitlagRatio, mv.hitstunRatio, mv.iasaRatio};
            for (int mi = 0; mi < 4; mi++)
            {
                if ((mv.activeMask & kMainBit[mi]) != 0)
                {
                    drawBar(cx, cy, 14.0f, 2.4f, ratios[mi], ui::stateColor(kMainState[mi], cbMode));
                    cy += 3.8f;
                    break;
                }
            }

            // Invincibility / Ledge Invincibility bar (Bright Ice-Aqua).
            if ((mv.activeMask & (main::kMeterLedgeIntang | main::kMeterIntang)) != 0)
            {
                bool ledge = (mv.activeMask & main::kMeterLedgeIntang) != 0;
                float ratio = ledge ? mv.ledgeIntangRatio : mv.intangRatio;
                drawBar(cx, cy, 11.0f, 1.8f, ratio, ui::stateColor(ui::kStateColorGalint, cbMode));
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
