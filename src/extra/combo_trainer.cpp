#include "extra/combo_trainer.h"

#include <GX/GXTypes.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/stop/so_stop_module_impl.h>
#include <string.h>

#include "extra/frame_data.h"
#include "extra/pplus_recovery.h"
#include "extra/stage_geom.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/status_ids.h"
#include "main/gfx/draw.h"
#include "main/settings.h"
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

    static ComboSlotState s_slots[game::kMaxFighters];
    static ComboMeterValues s_meters[game::kMaxFighters];
    static bool s_meterValid[game::kMaxFighters];
    static u32 s_rng = 0x51A7F3C9u;
    static ComboDebugBlock s_debug;

    // 16-point unit circle for neutral world-space DI indicator ring.
    static const float kRing[17][2] = {
        {1.0000f, 0.0000f},  {0.9239f, 0.3827f},  {0.7071f, 0.7071f},  {0.3827f, 0.9239f},
        {0.0000f, 1.0000f},  {-0.3827f, 0.9239f}, {-0.7071f, 0.7071f}, {-0.9239f, 0.3827f},
        {-1.0000f, 0.0000f}, {-0.9239f, -0.3827f},{-0.7071f, -0.7071f},{-0.3827f, -0.9239f},
        {0.0000f, -1.0000f}, {0.3827f, -0.9239f}, {0.7071f, -0.7071f}, {0.9239f, -0.3827f},
        {1.0000f, 0.0000f}
    };

    void comboTrainerReset()
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p >= 0 && p < game::kMaxFighters && s_slots[p].restoreCorrect)
            {
                setFighterCorrect(refs[i], s_slots[p].restoreCorrectKind);
            }
        }
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            comboSlotReset(s_slots[i]);
            memset(&s_meters[i], 0, sizeof(s_meters[i]));
            s_meterValid[i] = false;
        }
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kComboDebugMagic;
    }

    void comboTrainerAttach()
    {
        comboTrainerReset();
        s_rng = game::frameCounter() | 1u;
        TM_LOG("combo: debug block at %08x\n", &s_debug);
    }

    const ComboMeterValues* comboTrainerMeters(int playerNo)
    {
        if (playerNo < 0 || playerNo >= game::kMaxFighters || !s_meterValid[playerNo])
        {
            return NULL;
        }
        return &s_meters[playerNo];
    }

    static void copyDebugSlot(int p, Fighter* f)
    {
        ComboDebugSlot& d = s_debug.slots[p];
        const ComboSlotState& st = s_slots[p];
        const ComboMeterValues& mv = s_meters[p];
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
        const main::ComboTrainerSettings& cfg = main::settings().comboTrainer;
        s_debug.frame = game::frameCounter();
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            s_meterValid[i] = false;
        }

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
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
            computeComboMeters(cfg.enabledMeters, in, s_slots[p], s_meters[p]);
            s_meterValid[p] = true;

            // Reset / noclip / recovery-trainer actions apply only to CPU fighters.
            if (ref.cpu)
            {
                ComboStepAction act;
                stepComboTrainer(cfg, in, s_slots[p], act);
                if (act.restoreCorrect)
                {
                    setFighterCorrect(ref, act.restoreCorrectKind);
                }
                if (act.doWarp)
                {
                    s_rng ^= game::frameCounter() * 0x9E3779B9u;
                    float wx = s_slots[p].anchorX;
                    float wy = s_slots[p].anchorY;
                    if (act.pickOffstagePos && haveGeom)
                    {
                        sampleRecoveryCandidate(geom, s_rng, wx, wy);
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
                        dmg = static_cast<float>(cfg.fixedDamage);
                    }
                    if (act.warpFalling)
                    {
                        placeFalling(ref, wx, wy, act.setCorrectNone, dmg);
                    }
                    else
                    {
                        placeStanding(ref, wx, wy, act.setCorrectNone, dmg);
                    }
                    pplusRecoveryReset(p);
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
            gfx::vertex(cx + kRing[i][0] * r, cy + kRing[i][1] * r, 0xD8D8D8CC);
        }
        gfx::lineBegin(GX_LINES, 2, 16);
        gfx::vertex(cx, cy, 0xFFFFFFFF);
        gfx::vertex(cx + dx * r, cy + dy * r, 0xFF9830FF);
    }

    void comboTrainerDraw(const game::MatchState& match)
    {
        const main::ComboTrainerSettings& cfg = main::settings().comboTrainer;
        if (!match.running || match.paused || !cfg.enabled || cfg.enabledMeters == 0)
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p < 0 || p >= game::kMaxFighters || !s_meterValid[p])
            {
                continue;
            }
            const ComboMeterValues& mv = s_meters[p];
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
