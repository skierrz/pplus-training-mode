#include "extra/combo_break_rush.h"

#include <GX/GXGeometry.h>
#include <GX/GXHardware.h>
#include <GX/GXPixel.h>
#include <GX/GXTev.h>
#include <GX/GXTexture.h>
#include <GX/GXTransform.h>
#include <GX/GXTypes.h>
#include <OS/OSCache.h>
#include <cstdio>
#include <ec/ec_mgr.h>
#include <ef/ef_id.h>
#include <ft/fighter.h>
#include <gf/gf_camera.h>
#include <gf/gf_draw.h>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <mt/mt_vector.h>
#include <so/posture/so_posture_module_impl.h>
#include <so/so_module_accesser.h>
#include <string.h>

#include "extra/combo_break_rush_core.h"
#include "extra/drill_practice_driver.h"
#include "extra/hud_draw.h"
#include "extra/net_alloc.h"
#include "extra/pplus_collision.h"
#include "extra/stage_geom.h"
#include "main/ui/style.h"
#include "main/game/fighters.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/lab.h"
#include "main/services.h"
#include "main/ui/hud_layout.h"
#include "main/ui/menu.h"
#include "shared/drill_session.h"
#include "shared/game/frame.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/game/menu_sound.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kPartGroup = 0x88,
            kPosturePos = 0x0C,
            // RSBE01 rev 2 verified addresses (doldecomp/brawl config/RSBE01_02/symbols.txt):
            kGxInitTexObjAddr = 0x801F28F0,
            kDvdConvertPathAddr = 0x801F5F08,
            kDvdFastOpenAddr = 0x801F6210,
            kDvdCloseAddr = 0x801F6524,
            kDvdReadPrioAddr = 0x801F67F0,
            kDvdChunkBytes = 4096,       // 4 KiB read chunk (~16 sync DVD calls vs ~61)
            kDvdScratchBytes = kDvdChunkBytes + kRushLzRingSize, // 8 KiB temporary pool scratch
            kDvdMaxCompRead = 80 * 1024, // STGTARGETLv5 mip1 needs ~61 KiB, Lv1 needs ~55 KiB
            kCostumeReserve = 0x110000,
            // Brawl resident SFX IDs (snd/snd_id.h):
            kSfxCapsuleBreak = 0x1F7A,   // snd_se_item_Capsule_break
            kSfxPunchHitL = 0x003D,      // snd_se_common_punch_hit_l
            kSfxJustImpact = 0x000D,     // snd_se_common_JustImpact
            kSfxGuardBreak = 0x0062      // snd_se_common_GuardBreak
        };

        struct DvdFileInfoRaw {
            u8 raw[0x3C]; // sizeof(DVDFileInfo) == 0x3C (OpenRVL DVD/dvd.h)
        };

        struct RushNet {
            ComboBreakState st;
            StageGeometry geom;
            drills::Session* sess;
            ComboBreakDebugBlock debug;
            u8* targetTex; // 8 KiB GX_TF_CMPR 128x128 mip 1 from Brawl STGTARGETLv5.PAC (active only)
            u8 geomReady;
            u8 shapeOk;
            u8 texTried;
            u8 lastSfxOk;
            u8 lastVfxOk;
            u8 pad[3];
        };

        static RushNet* s_net = NULL;

        // Direct 32-byte-aligned Heaps::Network pool alloc/free (bypasses extra's s_arena
        // bump slab so repeated mid-match OFF->ON toggles never burn arena headroom, while
        // still enforcing P+'s 0x110000 costume decompression reserve).
        u8* allocNetPool32(u32 bytes)
        {
            u32 need = (bytes + 31u) & ~31u;
            if (need == 0u) return NULL;
            game::HeapUsage u;
            u.largest = 0u;
            if (!game::heapUsage(Heaps::Network, u) || u.largest < kCostumeReserve
                || u.largest - kCostumeReserve < need)
            {
                return NULL;
            }
            void* p = gfMemoryPool::alloc(gfHeapManager::getHeap(Heaps::Network), need, 32);
            return game::isRam(p) ? static_cast<u8*>(p) : NULL;
        }

        void freeNetPool32(u8*& ptr)
        {
            if (game::isRam(ptr))
            {
                free(ptr);
            }
            ptr = NULL;
        }

        // Smooth periodic approximation for gentle 3D target yaw/shatter spin (zero libm relocs).
        void unitWave16(float phase, float& outSin, float& outCos)
        {
            float scaled = phase * 16.0f;
            int idx = f2i(scaled);
            if (scaled < 0.0f) idx--;
            float frac = scaled - i2f(idx);
            float x0, y0, x1, y1;
            unitPoint16(idx, x0, y0);
            unitPoint16(idx + 1, x1, y1);
            outCos = x0 + (x1 - x0) * frac;
            outSin = y0 + (y1 - y0) * frac;
        }

        // Streams only the first ~61 KiB of Brawl's STGTARGETLv5.PAC (fallback LV1)
        // from disc via DVDReadPrio using a temporary 8 KiB 32-byte-aligned pool scratch
        // (4 KiB chunk + 4 KiB LZ77 ring, freed before return) and extracts its 128x128
        // GX_TF_CMPR bullseye mip 1 (8 KiB) into Heaps::Network.
        bool tryLoadDiscTargetTex(const char* path, u8* outTex, u8* chunk, u8* ring, u32& outFail)
        {
            typedef s32 (*ConvertFn)(const char*);
            typedef int (*FastOpenFn)(s32, DvdFileInfoRaw*);
            typedef int (*CloseFn)(DvdFileInfoRaw*);
            typedef s32 (*ReadPrioFn)(DvdFileInfoRaw*, void*, s32, s32, s32);

            s32 entry = reinterpret_cast<ConvertFn>(kDvdConvertPathAddr)(path);
            if (entry < 0) { outFail = 3u; return false; }
            DvdFileInfoRaw fi;
            memset(&fi, 0, sizeof(fi));
            if (!reinterpret_cast<FastOpenFn>(kDvdFastOpenAddr)(entry, &fi)) { outFail = 4u; return false; }

            // OpenRVL DVDFileInfo::length sits at +0x34; DVDReadPrio panics if
            // offset > fileLen or offset + length >= fileLen + 32.
            u32 fileLen = *reinterpret_cast<const u32*>(fi.raw + 0x34);
            bool ok = false;
            if (fileLen < 256u)
            {
                outFail = 10u;
            }
            else
            {
                DCFlushRange(chunk, kDvdChunkBytes);
                s32 hdrRead = reinterpret_cast<ReadPrioFn>(kDvdReadPrioAddr)(&fi, chunk, 256, 0, 2);
                u32 streamOff = 0u, compSize = 0u;
                if (hdrRead < 160)
                {
                    outFail = 5u;
                }
                else if (!rushParseOuterArcEntry1(chunk, 256u, streamOff, compSize))
                {
                    outFail = 6u;
                }
                else if (streamOff >= fileLen || (streamOff & 3u) != 0u)
                {
                    outFail = 11u;
                }
                else
                {
                    RushTexStream st;
                    rushTexStreamInit(st, ring, outTex);
                    u32 maxAvail = fileLen - streamOff;
                    u32 limit = compSize < maxAvail ? compSize : maxAvail;
                    if (limit > static_cast<u32>(kDvdMaxCompRead)) limit = static_cast<u32>(kDvdMaxCompRead);
                    u32 pos = 0u;
                    while (pos < limit && !st.done && !st.failed)
                    {
                        u32 want = limit - pos;
                        if (want > static_cast<u32>(kDvdChunkBytes)) want = static_cast<u32>(kDvdChunkBytes);
                        u32 rdLen = (want + 31u) & ~31u;
                        if (rdLen > static_cast<u32>(kDvdChunkBytes)) rdLen = static_cast<u32>(kDvdChunkBytes);
                        u32 curOff = streamOff + pos;
                        if (curOff > fileLen || (curOff & 3u) != 0u || rdLen == 0u
                            || rdLen >= (fileLen - curOff) + 32u)
                        {
                            outFail = 12u;
                            break;
                        }
                        s32 got = reinterpret_cast<ReadPrioFn>(kDvdReadPrioAddr)(
                            &fi, chunk, static_cast<s32>(rdLen), static_cast<s32>(curOff), 2);
                        if (got <= 0) { outFail = 7u; break; }
                        u32 ugot = static_cast<u32>(got);
                        u32 feed = ugot < want ? ugot : want;
                        rushTexStreamFeed(st, chunk, feed);
                        pos += feed;
                        // A short read may leave pos unaligned for DVDReadPrio's (offset >> 2) shift.
                        if (ugot < want)
                        {
                            if (!st.done) outFail = 13u;
                            break;
                        }
                    }
                    ok = (st.done != 0u);
                    if (!ok && outFail == 0u) outFail = st.failed ? 8u : 9u;
                }
            }
            reinterpret_cast<CloseFn>(kDvdCloseAddr)(&fi);
            return ok;
        }

        void releaseTargetTex(RushNet& net)
        {
            freeNetPool32(net.targetTex);
            net.texTried = 0u;
        }

        void ensureTargetTex(RushNet& net)
        {
            if (net.targetTex != NULL || net.texTried != 0u) return;
            net.texTried = 1u;
            u8* tex = allocNetPool32(kRushTexMip1Bytes);
            if (tex == NULL)
            {
                TM_LOG("combo_break_rush: tex alloc failed (8 KiB)\n");
                return;
            }
            u8* scratch = allocNetPool32(kDvdScratchBytes);
            if (scratch == NULL)
            {
                TM_LOG("combo_break_rush: scratch alloc failed (8 KiB)\n");
                freeNetPool32(tex);
                return;
            }
            u8* chunk = scratch;
            u8* ring = scratch + kDvdChunkBytes;
            u32 fail1 = 0u, fail2 = 0u;
            bool ok = tryLoadDiscTargetTex("/stage/melee/STGTARGETLv5.PAC", tex, chunk, ring, fail1)
                      || tryLoadDiscTargetTex("/stage/melee/STGTARGETLV1.PAC", tex, chunk, ring, fail2);
            freeNetPool32(scratch);
            if (!ok)
            {
                TM_LOG("combo_break_rush: disc target tex fallback (f1=%u f2=%u)\n", fail1, fail2);
                freeNetPool32(tex);
                return;
            }
            DCFlushRange(tex, kRushTexMip1Bytes);
            GXInvalidateTexAll();
            net.targetTex = tex;
        }

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
            d.texLoaded = net.targetTex != NULL ? 1u : 0u;
            d.cpuHidden = drillPracticeCpuInStandby() ? 1u : 0u;
            d.lastSfxOk = net.lastSfxOk;
            d.lastVfxOk = net.lastVfxOk;
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

        void spawnBreakVfx(RushNet& net, float x, float y, bool bonus)
        {
            if (!game::isRam(g_ecMgr)) return;
            Vec3f pos(x, y, 0.0f);
            Vec3f rot(0.0f, 0.0f, 0.0f);
            float sc = bonus ? 0.72f : 0.62f;
            Vec3f scl(sc, sc, sc);
            // Non-looping resident ef_common hit & flash bursts (roll back atomically on savestate load).
            u32 h1 = g_ecMgr->setEffect(ef_ptc_common_hit_normal_a, &pos, &rot, &scl);
            u32 h2 = g_ecMgr->setEffect(ef_ptc_common_smash_flash, &pos, &rot, &scl);
            if (h1 != 0u || h2 != 0u) net.lastVfxOk = 1u;
        }

        void playBreakSfx(RushNet& net, u8 ev)
        {
            int h = -1;
            if (ev == 3u)
            {
                h = game::playSound(kSfxGuardBreak, false);
                if (h < 0) ui::menuSound(game::kSoundBack);
            }
            else
            {
                h = game::playSound(kSfxCapsuleBreak, false);
                if (h < 0) h = game::playSound(kSfxPunchHitL, false);
                if (ev == 2u)
                {
                    int m = game::playSound(kSfxJustImpact, false);
                    if (m < 0) ui::menuSound(game::kSoundOpen);
                }
                else if (h < 0)
                {
                    ui::menuSound(game::kSoundConfirm);
                }
            }
            if (h >= 0) net.lastSfxOk = 1u;
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
                        float tx = net.st.targets[k].x;
                        float ty = net.st.targets[k].y;
                        bool bonus = (net.st.targets[k].tier == 2u);
                        rushBreakTarget(net.st, k);
                        spawnBreakVfx(net, tx, ty, bonus);
                    }
                }
            }
        }

        inline void vtx3d(float x, float y, float z, gfx::Rgba col)
        {
            WGPIPE.f = x;
            WGPIPE.f = y;
            WGPIPE.f = z;
            WGPIPE.ui = col;
        }

        inline void vtx3dTex(float x, float y, float z, gfx::Rgba col, float u, float v)
        {
            WGPIPE.f = x;
            WGPIPE.f = y;
            WGPIPE.f = z;
            WGPIPE.ui = col;
            WGPIPE.f = u;
            WGPIPE.f = v;
        }

        void drawRing3d(float cx, float cy, float cz, float rx, float ry, float sinY, gfx::Rgba col, u8 width)
        {
            gfx::lineBegin(GX_LINESTRIP, 17, width);
            for (int i = 0; i <= 16; i++)
            {
                float ux, uy;
                unitPoint16(i, ux, uy);
                vtx3d(cx + ux * rx, cy + uy * ry, cz - ux * ry * sinY, col);
            }
            GXEnd();
        }

        void drawFilledDisc3d(float cx, float cy, float zOff, float r, float cosY, float sinY, gfx::Rgba col)
        {
            GXBegin(GX_TRIANGLEFAN, GX_VTXFMT1, 18);
            vtx3d(cx, cy, zOff * cosY, col);
            for (int i = 0; i <= 16; i++)
            {
                float ux, uy;
                unitPoint16(i, ux, uy);
                float px = cx + ux * r * cosY + zOff * sinY;
                float py = cy + uy * r;
                float pz = zOff * cosY - ux * r * sinY;
                vtx3d(px, py, pz, col);
            }
            GXEnd();
        }

        void drawBevelRim3d(float cx, float cy, float r, float halfD, float cosY, float sinY, bool gold)
        {
            const gfx::Rgba topCol = gold ? 0xFFE658FFu : 0x465260FFu;
            const gfx::Rgba botCol = gold ? 0x9C6800FFu : 0x121820FFu;
            GXBegin(GX_QUADS, GX_VTXFMT1, 64);
            for (int i = 0; i < 16; i++)
            {
                float u0x, u0y, u1x, u1y;
                unitPoint16(i, u0x, u0y);
                unitPoint16(i + 1, u1x, u1y);
                gfx::Rgba c = (u0y + u1y >= 0.0f) ? topCol : botCol;
                float fx0 = cx + u0x * r * cosY + halfD * sinY;
                float fy0 = cy + u0y * r;
                float fz0 = halfD * cosY - u0x * r * sinY;
                float bx0 = cx + u0x * r * cosY - halfD * sinY;
                float bz0 = -halfD * cosY - u0x * r * sinY;
                float fx1 = cx + u1x * r * cosY + halfD * sinY;
                float fy1 = cy + u1y * r;
                float fz1 = halfD * cosY - u1x * r * sinY;
                float bx1 = cx + u1x * r * cosY - halfD * sinY;
                float bz1 = -halfD * cosY - u1x * r * sinY;
                vtx3d(fx0, fy0, fz0, c);
                vtx3d(bx0, fy0, bz0, botCol);
                vtx3d(bx1, fy1, bz1, botCol);
                vtx3d(fx1, fy1, fz1, c);
            }
            GXEnd();
        }

        void drawProceduralBullseye3d(float cx, float cy, float halfD, float r, float cosY, float sinY, bool gold)
        {
            // Fallback concentric Smash bullseye if disc texture could not be read.
            static const float kFrac[5] = {1.00f, 0.80f, 0.58f, 0.38f, 0.20f};
            const gfx::Rgba red = gold ? 0xE83420FFu : 0xDC1C1CFFu;
            const gfx::Rgba wht = gold ? 0xFFF4D0FFu : 0xFFFFFFFFu;
            for (int k = 0; k < 5; k++)
            {
                drawFilledDisc3d(cx, cy, halfD + 0.02f * i2f(k), r * kFrac[k], cosY, sinY, (k & 1) ? wht : red);
            }
        }

        void drawShatterShards3d(const RushTarget& t)
        {
            float prog = 1.0f - i2f(t.animFrames) / i2f(kRushBurstFrames);
            u32 alpha = static_cast<u32>(f2i((1.0f - prog) * 245.0f)) & 0xFFu;
            float spread = t.radius * (0.45f + prog * 1.85f);
            float gravY = -3.8f * prog * prog;
            float shardR = t.radius * (0.48f - 0.16f * prog);

            // Expanding bright break shockwave ring.
            drawRing3d(t.x, t.y, 0.2f, t.radius * (0.95f + prog * 1.45f), t.radius * (0.95f + prog * 1.45f), 0.0f,
                       (t.tier == 2u ? 0xFFE86600u : 0xFFF4E000u) | alpha, 16);

            // 6 flying tumbling Smash bullseye shards (alternating crimson/white wedges).
            GXBegin(GX_TRIANGLES, GX_VTXFMT1, 18);
            for (int s = 0; s < 6; s++)
            {
                float dirX, dirY;
                unitPoint16((s * 3 + 1) & 15, dirX, dirY);
                float spinS, spinC;
                unitWave16(prog * 1.4f + i2f(s) * 0.17f, spinS, spinC);
                float sx = t.x + dirX * spread;
                float sy = t.y + dirY * spread + gravY;
                float sz = (s & 1 ? 1.4f : -1.4f) * prog;
                gfx::Rgba col = ((s & 1) == 0 ? 0xE0202000u : (t.tier == 2u ? 0xFFE86600u : 0xFFFFFF00u)) | alpha;
                vtx3d(sx + spinC * shardR, sy + spinS * shardR, sz, col);
                vtx3d(sx - spinS * shardR * 0.85f, sy + spinC * shardR * 0.85f, sz + 0.35f, col);
                vtx3d(sx - spinC * shardR * 0.5f, sy - spinS * shardR * 0.65f, sz - 0.35f, 0x1A202800u | alpha);
            }
            GXEnd();
        }
    }

    void comboBreakRushAttach()
    {
        TM_STATIC_ASSERT(kRushBestSlot == drills::kDrillComboBreakRush, tm_rush_best_slot_matches_drill_id);
        if (s_net == NULL) s_net = allocNetArray<RushNet>(1);
        if (s_net == NULL) return;
        s_net->sess = drills::sessionIn(main::services().settings());
        s_net->shapeOk = collShapeSupported() ? 1 : 0;
        s_net->debug.cmdMode = kRushCmdIdle;
        u32 best = s_net->sess != NULL ? static_cast<u32>(s_net->sess->best[kRushBestSlot]) : 0u;
        u8 startMode = (s_net->sess != NULL && s_net->sess->drill == drills::kDrillComboBreakRush)
                           ? static_cast<u8>(kRushFree)
                           : static_cast<u8>(kRushOff);
        rushInit(s_net->st, startMode, best, 0x544D4252u);
        if (startMode != kRushOff)
        {
            if (s_net->sess != NULL) s_net->sess->drillRan = 1;
            ensureTargetTex(*s_net);
            drillPracticeSetExternalStandby(true);
        }
        syncDebug(*s_net);
        TM_LOG("combo_break_rush: debug block at %08x\n", reinterpret_cast<u32>(&s_net->debug));
    }

    void comboBreakRushDetach()
    {
        if (s_net != NULL)
        {
            saveBest(*s_net);
            drillPracticeSetExternalStandby(false);
            releaseTargetTex(*s_net);
        }
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
            ensureTargetTex(*s_net);
            drillPracticeSetExternalStandby(true);
            if (queryStageGeometry(s_net->geom, false, true)) s_net->geomReady = 1;
            float fx, fy, flr;
            primaryPos(fx, fy, flr);
            for (int i = 0; i < kRushActiveSlots; i++)
            {
                rushSpawnOne(s_net->st, i, s_net->geom, fx, fy);
            }
        }
        else
        {
            drillPracticeSetExternalStandby(false);
            releaseTargetTex(*s_net);
        }
        syncDebug(*s_net);
    }

    void comboBreakRushCycleMode()
    {
        if (s_net == NULL || s_net->st.mode == kRushOff) return;
        comboBreakRushSetActive(rushNextMode(s_net->st.mode));
        ui::menuSound(game::kSoundMove);
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
            playBreakSfx(net, net.st.hitEvent);
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

        float rads[kRushActiveSlots];
        float cosY[kRushActiveSlots];
        float sinY[kRushActiveSlots];
        const float halfD = 0.65f;

        camMgr->m_cameras[0].setGX();
        gfx::forgetState();
        gfx::prepare();

        // Pass 1: 3D beveled cylindrical rims, bonus halos, procedural fallback faces, and shatter shards.
        for (int i = 0; i < kRushActiveSlots; i++)
        {
            const RushTarget& t = net.st.targets[i];
            rads[i] = t.radius * (t.animFrames < 8 ? (0.4f + 0.075f * i2f(t.animFrames)) : 1.0f);
            float wSin, wCos;
            unitWave16(i2f(net.st.tickCount) * 0.0085f + i2f(i) * 0.31f, wSin, wCos);
            // Gentle +-20 deg Y-axis sway shows 3D rim depth while keeping bullseye readable.
            float yawSin = wSin * 0.34f;
            float yawCos = 1.0f - 0.5f * yawSin * yawSin;
            sinY[i] = yawSin;
            cosY[i] = yawCos;
            if (t.active == 1)
            {
                bool gold = (t.tier == 2u);
                if (gold)
                {
                    drawRing3d(t.x, t.y, -0.25f, rads[i] * 1.18f * yawCos, rads[i] * 1.18f, yawSin, 0xFFD300A8u, 14);
                }
                drawFilledDisc3d(t.x, t.y, -halfD, rads[i] * 1.03f, yawCos, yawSin, gold ? 0x6E4800FFu : 0x161D26FFu);
                drawBevelRim3d(t.x, t.y, rads[i] * 1.03f, halfD, yawCos, yawSin, gold);
                if (net.targetTex == NULL)
                {
                    drawProceduralBullseye3d(t.x, t.y, halfD, rads[i], yawCos, yawSin, gold);
                }
            }
            else if (t.active == 2)
            {
                drawShatterShards3d(t);
            }
        }

        // Pass 2: Real Brawl STGTARGET CMPR 128x128 bullseye mapped onto each live 3D front face.
        if (net.targetTex != NULL)
        {
            typedef void (*GxInitTexObjFn)(GXTexObj*, void*, u16, u16, GXTexFmt, GXTexWrapMode, GXTexWrapMode, GXBool);
            gfDrawSetVtxPosColorTexPrimEnvironment();
            GXSetCullMode(GX_CULL_NONE);
            GXLoadPosMtxImm(*reinterpret_cast<const Mtx*>(&camMgr->m_cameras[0]), GX_PNMTX0);
            GXSetZMode(FALSE, GX_ALWAYS, FALSE);
            GXSetZCompLoc(FALSE);
            GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
            GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
            GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);

            GXTexObj texObj;
            reinterpret_cast<GxInitTexObjFn>(kGxInitTexObjAddr)(&texObj, net.targetTex, kRushTexWidth, kRushTexHeight,
                                                                GX_TF_CMPR, GX_CLAMP, GX_CLAMP, FALSE);
            GXInitTexObjLOD(&texObj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, FALSE, FALSE, GX_ANISO_1);
            GXLoadTexObj(&texObj, GX_TEXMAP0);

            for (int i = 0; i < kRushActiveSlots; i++)
            {
                const RushTarget& t = net.st.targets[i];
                if (t.active != 1) continue;
                gfx::Rgba tint = (t.tier == 2u) ? 0xFFF6D2FFu : 0xFFFFFFFFu;
                float r = rads[i];
                float cY = cosY[i];
                float sY = sinY[i];
                GXBegin(GX_TRIANGLEFAN, GX_VTXFMT1, 18);
                vtx3dTex(t.x + halfD * sY, t.y, halfD * cY, tint, 0.5f, 0.5f);
                for (int k = 0; k <= 16; k++)
                {
                    float ux, uy;
                    unitPoint16(k, ux, uy);
                    float px = t.x + ux * r * cY + halfD * sY;
                    float py = t.y + uy * r;
                    float pz = halfD * cY - ux * r * sY;
                    // Radius 0.455 samples the outer crimson ring cleanly inside the circular disc.
                    vtx3dTex(px, py, pz, tint, 0.5f + ux * 0.455f, 0.5f - uy * 0.455f);
                }
                GXEnd();
            }
            gfx::forgetState();
            gfx::prepare();
        }

        // Pass 3: Crisp front bevel rim outline for 480i CRT readability against bright skies.
        for (int i = 0; i < kRushActiveSlots; i++)
        {
            const RushTarget& t = net.st.targets[i];
            if (t.active != 1) continue;
            gfx::Rgba rimCol = (t.tier == 2u) ? 0xFFE866FFu : 0x0B121AF0u;
            drawRing3d(t.x + halfD * sinY[i], t.y, halfD * cosY[i] + 0.05f, rads[i] * 1.02f * cosY[i],
                       rads[i] * 1.02f, sinY[i], rimCol, 12);
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
        // Synced pixel-for-pixel to layout::comboBreakHudBox(wide) (316*xs x 40 at y = 24..64).
        const float ox = b.x0;
        const float oy = b.y0;
        const float bw = b.x1 - b.x0;
        const float bh = b.y1 - b.y0;

        // Panel B quiet borderless smoked plate (zero wireframe border, zero neon accent bar).
        gfx::fillRect(ox, oy, bw, bh, 0x0E161EA6u);

        const float padX = ui::style::kFibGap * xs;
        const float lx = ox + padX;
        const float rx = ox + bw - padX;
        const float txtSc = ui::style::scaleForCap(ui::style::kTypeMeta);

        // Top row: calm white Rodin title + mode/timer readout.
        const float topY = ui::style::textYForCap(oy + 4.0f, txtSc);
        drawShadowText(lx, topY, "Break the Targets!", 0xF2F6FAFFu, txtSc);

        char buf[32];
        if (st.mode == kRushFree)
        {
            float tw = gfx::textWidth("Free", txtSc);
            drawShadowText(rx - tw, topY, "Free", 0xA8E8C8FFu, txtSc);
        }
        else
        {
            unsigned secs = (static_cast<unsigned>(st.timerFrames) + 59u) / 60u;
            if (st.finished) strcpy(buf, "Time Up");
            else snprintf(buf, sizeof(buf), "%u:%02u", secs / 60u, secs % 60u);
            gfx::Rgba tCol = (st.finished || secs <= 5u) ? 0xFF7868FFu : 0xFFD300FFu;
            drawShadowText(rx - gfx::textWidth(buf, txtSc), topY, buf, tCol, txtSc);
        }

        // 2px integer-Y inset divider matching Panel B grounding (480i interlace-safe).
        gfx::fillRect(lx, oy + 18.0f, bw - 2.0f * padX, ui::style::kFibHair, 0xFFFFFF24u);

        // Second row: calm white labels + semantic gold/green values.
        const float rowY = ui::style::textYForCap(oy + 23.0f, txtSc);
        float curX = lx;
        drawShadowText(curX, rowY, "Score ", 0xD8E2ECFFu, txtSc);
        curX += gfx::textWidth("Score ", txtSc);

        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(st.score));
        drawShadowText(curX, rowY, buf, 0xFFD300FFu, txtSc);
        curX += gfx::textWidth(buf, txtSc) + ui::style::kFibGap * xs;

        snprintf(buf, sizeof(buf), "x%u", static_cast<unsigned>(st.multiplier));
        gfx::Rgba multCol = st.multiplier >= 7 ? 0xFF7868FFu
                                               : (st.multiplier >= 4 ? 0xFFD300FFu
                                                                     : (st.multiplier >= 2 ? 0x4CE688FFu : 0xC8D6E0FFu));
        drawShadowText(curX, rowY, buf, multCol, txtSc);

        if (st.bestScore > 0u)
        {
            snprintf(buf, sizeof(buf), "Best %u", static_cast<unsigned>(st.bestScore));
        }
        else
        {
            strcpy(buf, "Best --");
        }
        float bestW = gfx::textWidth(buf, txtSc);
        drawShadowText(rx - bestW, rowY, buf, 0xD8E2ECFFu, txtSc);

        snprintf(buf, sizeof(buf), "Chain %u", static_cast<unsigned>(st.streak));
        float stkW = gfx::textWidth(buf, txtSc);
        float stkX = rx - bestW - ui::style::kFibGap * xs - stkW;
        if (stkX > curX + ui::style::kFibPad * xs)
        {
            drawShadowText(stkX, rowY, buf, st.streak > 0 ? 0x4CE688FFu : 0xB8C6D2FFu, txtSc);
        }

        // 2px integer-Y chain timer bar along the bottom inset of the smoked plate.
        const float barW = bw - 2.0f * padX;
        const float barY = oy + bh - 4.0f;
        gfx::fillRect(lx, barY, barW, 2.0f, 0x060C1280u);
        if (st.chainFrames > 0)
        {
            gfx::fillRect(lx, barY,
                          barW * i2f(st.chainFrames) / i2f(kRushChainMaxFrames), 2.0f,
                          st.multiplier >= 4 ? 0xFFD300F0u : 0x4CE688E8u);
        }
        s_net->debug.draw2dCalls++;
    }
}
}
