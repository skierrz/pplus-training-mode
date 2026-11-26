#include "extra/combo_break_rush.h"

#include <GX/GXGeometry.h>
#include <GX/GXTypes.h>
#include <cstdio>
#include <ft/fighter.h>
#include <gf/gf_camera.h>
#include <so/posture/so_posture_module_impl.h>
#include <so/so_module_accesser.h>
#include <string.h>

#include "extra/combo_break_rush_core.h"
#include "extra/hud_draw.h"
#include "extra/net_alloc.h"
#include "extra/pplus_collision.h"
#include "extra/stage_geom.h"
#include "main/game/fighters.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/lab.h"
#include "main/services.h"
#include "main/ui/hud_layout.h"
#include "main/ui/menu.h"
#include "shared/drill_session.h"
#include "shared/game/frame.h"
#include "shared/game/mem.h"
#include "shared/game/menu_sound.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kPartGroup = 0x88,
            kPosturePos = 0x0C
        };

        struct RushNet {
            ComboBreakState st;
            StageGeometry geom;
            drills::Session* sess;
            ComboBreakDebugBlock debug;
            u8 geomReady;
            u8 shapeOk;
            u8 bubbleOk;
            u8 pad;
        };

        static RushNet* s_net = NULL;

#ifdef __MWERKS__
#pragma dont_inline on
#endif
        void saveBest(RushNet& net)
        {
            if (net.sess == NULL || net.st.bestScore == 0u) return;
            u16 cap = net.st.bestScore > 65535u ? 65535u : static_cast<u16>(net.st.bestScore);
            u16 m = drills::betterBest(kRushBestSlot, net.sess->best[kRushBestSlot], cap);
            net.sess->best[kRushBestSlot] = m;
            if (static_cast<u32>(m) > net.st.bestScore) net.st.bestScore = static_cast<u32>(m);
        }

        void syncDebug(RushNet& net)
        {
            ComboBreakDebugBlock& d = net.debug;
            const ComboBreakState& st = net.st;
            d.magic = kRushDebugMagic;
            d.version = kRushDebugVersion;
            d.mode = st.mode;
            d.active = st.mode != kRushOff ? 1 : 0;
            d.finished = st.finished;
            d.multiplier = st.multiplier;
            d.activeTargets = rushCountLive(st);
            d.stagePlatforms = net.geomReady ? static_cast<u8>(net.geom.platformCount) : 0;
            d.shapeOk = net.shapeOk;
            d.score = st.score;
            d.bestScore = st.bestScore;
            d.streak = st.streak;
            d.breaks = st.breaks;
            d.chainFrames = st.chainFrames;
            d.timerFrames = st.timerFrames;
            for (int i = 0; i < kRushMaxTargets; i++)
            {
                const RushTarget& t = st.targets[i];
                bool live = (t.active == 1);
                d.targetX[i] = live ? t.x : 0.0f;
                d.targetY[i] = live ? t.y : 0.0f;
                d.targetR[i] = live ? t.radius : 0.0f;
            }
        }

        void primaryPos(float& ox, float& oy, float& olr)
        {
            ox = oy = 0.0f;
            olr = 1.0f;
            game::FighterRef refs[game::kMaxFighters];
            int n = game::listFighters(refs, game::kMaxFighters);
            for (int i = 0; i < n; i++)
            {
                if (!refs[i].cpu)
                {
                    const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
                    if (m != NULL && game::isRam(m->m_postureModule))
                    {
                        const float* p = reinterpret_cast<const float*>(reinterpret_cast<u32>(m->m_postureModule) + kPosturePos);
                        ox = p[0];
                        oy = p[1];
                        olr = m->m_postureModule->getLr() < 0.0f ? -1.0f : 1.0f;
                        return;
                    }
                }
            }
        }

        void checkAttackHits(RushNet& net, const soModuleEnumeration* m)
        {
            if (m == NULL || !game::isRam(m->m_collisionAttackModule)) return;
            const void* atk = m->m_collisionAttackModule;
            const void* vt = reinterpret_cast<const void*>(game::wordAt(atk, 0));
            if (!game::isRam(vt) || game::wordAt(vt, kCollAttackSlotRenderDebug) != kCollAttackRenderDebug
                || (game::wordAt(atk, kCollAttackFlags) & kCollAttackAsleep) != 0)
            {
                return;
            }
            const void* parts = NULL;
            const void* calls = NULL;
            const void* coll = NULL;
            int gCount = 0;
            int limit = collOpenParts(atk, kCollAttackParts, kCollAttackCollision, parts, calls, coll, gCount);
            for (int id = 0; id < limit; id++)
            {
                const void* part = collArrayAt(parts, calls, id);
                if (!game::isRam(part) || game::wordAt(part, 0) != kCollPartActive) continue;
                const void* shape = collShapeOf(coll, gCount, id, game::wordAt(part, kPartGroup));
                if (!game::isRam(shape) || game::byteAt(shape, kCollShapeActive) != 1) continue;
                const float* sf = static_cast<const float*>(shape);
                float r = sf[3]; // kCollShapeRadius = 0x0C
                if (!(r > 0.0f && r < 500.0f)) continue;
                if (game::wordAt(shape, kCollShapeVtable) == kCollCapsuleVtable)
                {
                    float sc = sf[28]; // kCollCapsuleScale = 0x70
                    if (sc > 0.0f && sc < 10.0f) r *= sc;
                }
                for (int k = 0; k < kRushActiveSlots; k++)
                {
                    if (rushHitboxHitsTarget(sf, sf + 13, r, net.st.targets[k]))
                    {
                        rushBreakTarget(net.st, k);
                    }
                }
            }
        }

        void drawRing3d(float cx, float cy, float r, gfx::Rgba col, u8 width)
        {
            gfx::lineBegin(GX_LINESTRIP, 17, width);
            for (int i = 0; i <= 16; i++)
            {
                float ux, uy;
                unitPoint16(i, ux, uy);
                gfx::vertex(cx + ux * r, cy + uy * r, col);
            }
            GXEnd();
        }

        void drawBadge(float x, float y, float w, float h, float xs, u32 top, u32 bot, const char* txt, float ty, float sc)
        {
            gfx::fillRect(x, y, w, h, 0x050C12E8u);
            gfx::fillGradV(x + xs, y + 1.0f, w - 2.0f * xs, h - 2.0f, top, bot);
            gfx::text(x + 4.0f * xs, ty, txt, 0xFFF8DCFFu, sc);
        }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif
    }

    void comboBreakRushAttach()
    {
        if (s_net == NULL) s_net = allocNetArray<RushNet>(1);
        if (s_net == NULL) return;
        s_net->sess = drills::sessionIn(main::services().settings());
        s_net->shapeOk = collShapeSupported() ? 1 : 0;
        s_net->bubbleOk = collBubbleSupported() ? 1 : 0;
        s_net->debug.cmdMode = kRushCmdIdle;
        u32 best = s_net->sess != NULL ? static_cast<u32>(s_net->sess->best[kRushBestSlot]) : 0u;
        rushInit(s_net->st, kRushOff, best, 0x544D4252u);
        syncDebug(*s_net);
        TM_LOG("combo_break_rush: debug block at %08x\n", reinterpret_cast<u32>(&s_net->debug));
    }

    void comboBreakRushDetach()
    {
        if (s_net != NULL) saveBest(*s_net);
        freeNetPtr(s_net);
    }

    void comboBreakRushReset()
    {
        if (s_net != NULL && s_net->st.mode != kRushOff) comboBreakRushSetActive(s_net->st.mode);
    }

    void comboBreakRushSetActive(u8 mode)
    {
        if (s_net == NULL) return;
        saveBest(*s_net);
        u32 best = s_net->st.bestScore;
        if (s_net->sess != NULL && static_cast<u32>(s_net->sess->best[kRushBestSlot]) > best)
        {
            best = static_cast<u32>(s_net->sess->best[kRushBestSlot]);
        }
        u8 clean = mode < kRushModeCount ? mode : static_cast<u8>(kRushOff);
        rushInit(s_net->st, clean, best, game::frameCounter() ^ 0x9E3779B9u);
        if (clean != kRushOff)
        {
            if (queryStageGeometry(s_net->geom, false, true)) s_net->geomReady = 1;
            float fx, fy, flr;
            primaryPos(fx, fy, flr);
            for (int i = 0; i < kRushActiveSlots; i++)
            {
                rushSpawnOne(s_net->st, i, s_net->geom, fx, fy);
            }
        }
        syncDebug(*s_net);
    }

    bool comboBreakRushActive()
    {
        return s_net != NULL && s_net->st.mode != kRushOff;
    }

    void comboBreakRushFrame()
    {
        if (s_net == NULL) return;
        RushNet& net = *s_net;
        u8 cmd = net.debug.cmdMode;
        if (cmd != kRushCmdIdle)
        {
            net.debug.cmdMode = kRushCmdIdle;
            if (cmd < kRushModeCount)
            {
                comboBreakRushSetActive(cmd);
            }
            else if (cmd == kRushCmdSnapTarget && net.st.mode != kRushOff)
            {
                float fx, fy, flr;
                primaryPos(fx, fy, flr);
                RushTarget& t = net.st.targets[0];
                t.x = fx + 10.0f * flr;
                t.y = fy + 8.5f;
                t.radius = 5.2f;
                t.active = 1;
                t.tier = 2;
                t.animFrames = 12;
                t.cooldown = 0;
            }
        }
        if (net.st.mode == kRushOff)
        {
            return;
        }

        if ((!net.geomReady || (net.st.tickCount & 63u) == 0u) && queryStageGeometry(net.geom, false, true))
        {
            net.geomReady = 1;
        }
        float fx, fy, flr;
        primaryPos(fx, fy, flr);
        rushStepFrame(net.st, net.geom, fx, fy);

        if (!net.st.finished && net.shapeOk)
        {
            game::FighterRef refs[game::kMaxFighters];
            int n = game::listFighters(refs, game::kMaxFighters);
            for (int i = 0; i < n; i++)
            {
                if (!refs[i].cpu)
                {
                    checkAttackHits(net, game::fighterModules(refs[i].fighter));
                    if (refs[i].partner != NULL && refs[i].partner != refs[i].fighter)
                    {
                        checkAttackHits(net, game::fighterModules(refs[i].partner));
                    }
                }
            }
        }

        if (net.st.hitEvent != 0)
        {
            static const u8 kSfx[3] = {game::kSoundConfirm, game::kSoundOpen, game::kSoundBack};
            ui::menuSound(kSfx[net.st.hitEvent <= 3 ? (net.st.hitEvent - 1) : 0]);
            saveBest(net);
        }
        syncDebug(net);
    }

    void comboBreakRushDraw3d(const game::MatchState& match)
    {
        if (s_net == NULL || s_net->st.mode == kRushOff || !match.running || match.paused || lab::menuOpen()) return;
        gfCameraManager* camMgr = gfCameraManager::getManager();
        if (!game::isRam(camMgr)) return;
        RushNet& net = *s_net;
        float viewMtx[3][4];
        float idMtx[3][4];
        memcpy(viewMtx, &camMgr->m_cameras[0], sizeof(viewMtx));
        collIdentityMatrix34(idMtx);

        static const u32 kOuter[3] = {0x2CE0F4C8u, 0x4CE688C8u, 0xFFD300D8u};
        static const u32 kShade[3] = {0x0E6C80C8u, 0x167844C8u, 0xB86E00D8u};
        static const u32 kRing[3]  = {0x9CF4FFFFu, 0xA8FFCCFFu, 0xFFF29AFFu};

        float rads[kRushActiveSlots];
        for (int i = 0; i < kRushActiveSlots; i++)
        {
            const RushTarget& t = net.st.targets[i];
            rads[i] = t.radius * (t.animFrames < 8 ? (0.4f + 0.075f * static_cast<float>(t.animFrames)) : 1.0f);
            if (!net.bubbleOk || t.active == 0) continue;
            float p[3] = {t.x, t.y, 0.0f};
            if (t.active == 1)
            {
                u8 tier = t.tier < 3 ? t.tier : 0;
                collDrawBubble(static_cast<double>(rads[i]), idMtx, p, p, kOuter[tier], kShade[tier], viewMtx);
                collDrawBubble(static_cast<double>(rads[i] * 0.46f), idMtx, p, p, 0xFFFFFFE8u, kOuter[tier], viewMtx);
            }
            else if (t.animFrames > kRushBurstFrames - 5)
            {
                float r = t.radius * (0.6f + 0.14f * static_cast<float>(t.animFrames - (kRushBurstFrames - 5)));
                collDrawBubble(static_cast<double>(r), idMtx, p, p, 0xFFF8C0F0u, 0xFFD300D0u, viewMtx);
            }
        }

        camMgr->m_cameras[0].setGX();
        gfx::forgetState();
        gfx::prepare();
        for (int i = 0; i < kRushActiveSlots; i++)
        {
            const RushTarget& t = net.st.targets[i];
            if (t.active == 1)
            {
                drawRing3d(t.x, t.y, rads[i] * 1.18f, kRing[t.tier < 3 ? t.tier : 0], 12);
            }
            else if (t.active == 2)
            {
                float prog = 1.0f - static_cast<float>(t.animFrames) / static_cast<float>(kRushBurstFrames);
                drawRing3d(t.x, t.y, t.radius * (1.0f + prog * 1.6f),
                           0xFFE87000u | (static_cast<u32>((1.0f - prog) * 230.0f) & 0xFFu), 18);
            }
        }
        GXSetLineWidth(6, 0);
        camMgr->m_cameras[0].setGX();
        gfx::forgetState();
        net.debug.draw3dCalls++;
    }

    void comboBreakRushDraw(const game::MatchState& match)
    {
        if (s_net == NULL || s_net->st.mode == kRushOff || !match.running || match.paused || lab::menuOpen()
            || gfx::lineHeight(0.5f) <= 0.0f)
        {
            return;
        }
        const ComboBreakState& st = s_net->st;
        const bool wide = gfx::widescreen();
        const float xs = ui::layout::xsOf(wide);
        const ui::layout::Box b = ui::layout::comboBreakHudBox(wide);
        const float ox = b.x0, oy = b.y0, bw = b.x1 - b.x0, bh = b.y1 - b.y0;
        const float ix = ox + 4.0f * xs, iw = bw - 8.0f * xs;

        // Brawl/P+ double-framed translucent top HUD (zero left-accent stripe).
        gfx::fillRect(ox, oy, bw, bh, 0x060D12F2u);
        gfx::fillRect(ox + 2.0f * xs, oy + 2.0f, bw - 4.0f * xs, bh - 4.0f,
                      (st.finished || st.multiplier >= 4) ? 0xFFD300FFu : 0x35B8C8FFu);
        gfx::fillGradV(ix, oy + 4.0f, iw, bh - 8.0f, 0x0D1E26ECu, 0x071117ECu);
        gfx::fillGradV(ix, oy + 4.0f, iw, 15.0f, 0x226874F0u, 0x123C46F0u);
        gfx::fillRect(ix, oy + 19.0f, iw, 2.0f, 0x2A5866E0u);

        const float lx = ox + 10.0f * xs;
        const float rx = ox + bw - 10.0f * xs;
        const float dispSc = 8.5f / 24.0f;
        const float txtSc = 8.0f / 15.0f;

        gfx::displayText(lx, oy + 6.1f, "COMBO-BREAK RUSH", 0xFFFFFFFFu, 0xB8E0EEFFu, dispSc, dispSc, 1.2f, 0x050C12FFu);

        char buf[28];
        if (st.mode == kRushFree)
        {
            float pw = gfx::textWidth("FREE", txtSc) + 8.0f * xs;
            drawBadge(rx - pw, oy + 5.5f, pw, 12.0f, xs, 0x1B6B76FFu, 0x0E3D45FFu, "FREE", oy + 4.8f, txtSc);
        }
        else
        {
            unsigned secs = (static_cast<unsigned>(st.timerFrames) + 59u) / 60u;
            if (st.finished) strcpy(buf, "TIME UP");
            else snprintf(buf, sizeof(buf), "%u:%02u", secs / 60u, secs % 60u);
            gfx::text(rx - gfx::textWidth(buf, txtSc), oy + 4.8f, buf,
                      (st.finished || secs <= 5u) ? 0xFF7868FFu : 0xFFD300FFu, txtSc);
        }

        const float rowY = oy + 19.3f;
        snprintf(buf, sizeof(buf), "SCORE %u", static_cast<unsigned>(st.score));
        gfx::text(lx, rowY, buf, 0xFFFFFFFFu, txtSc);
        float multX = lx + gfx::textWidth(buf, txtSc) + 6.0f * xs;

        snprintf(buf, sizeof(buf), "x%u", static_cast<unsigned>(st.multiplier));
        static const u32 kMTop[4] = {0x1D5C6CFFu, 0x1C8A50FFu, 0xC88A00FFu, 0xC83C28FFu};
        static const u32 kMBot[4] = {0x103640FFu, 0x0F502EFFu, 0x7E5200FFu, 0x7A1E14FFu};
        int mt = st.multiplier >= 7 ? 3 : (st.multiplier >= 4 ? 2 : (st.multiplier >= 2 ? 1 : 0));
        drawBadge(multX, oy + 21.0f, gfx::textWidth(buf, txtSc) + 8.0f * xs, 11.0f, xs, kMTop[mt], kMBot[mt], buf, rowY,
                  txtSc);

        if (st.bestScore > 0u)
        {
            snprintf(buf, sizeof(buf), "STK %u  BEST %u", static_cast<unsigned>(st.streak),
                     static_cast<unsigned>(st.bestScore));
        }
        else
        {
            snprintf(buf, sizeof(buf), "STK %u  BEST --", static_cast<unsigned>(st.streak));
        }
        gfx::text(rx - gfx::textWidth(buf, txtSc), rowY, buf, 0xA0BCC4FFu, txtSc);

        const float barW = bw - 20.0f * xs;
        gfx::fillRect(lx, oy + 33.0f, barW, 3.0f, 0x162A34FFu);
        if (st.chainFrames > 0)
        {
            gfx::fillRect(lx, oy + 33.0f,
                          barW * static_cast<float>(st.chainFrames) / static_cast<float>(kRushChainMaxFrames), 3.0f,
                          st.multiplier >= 4 ? 0xFFD300FFu : 0x3AD0E0FFu);
        }
        s_net->debug.draw2dCalls++;
    }
}
}
