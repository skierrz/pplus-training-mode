// Olimar / Pikmin Lab Kit runtime backend (extra/olimar_lab.h).
// Verified against P+ 3.2.1 ft_pikmin.rel, FitPikmin.pac, and sora_melee.
#include "extra/olimar_lab.h"

#include <ft/fighter.h>
#include <ft/ft_common_data_accesser.h>
#include <ft/ft_entry_manager.h>
#include <so/so_module_accesser.h>
#include <so/work/so_work_manage_module_impl.h>
#ifdef __MWERKS__
#include <so/stageobject.h>
#include <so/stop/so_stop_module_impl.h>
#include <so/visibility/so_visibility_module_impl.h>
#endif

#include "extra/fighter_mechanics_logic.h"
#include "extra/fighter_swap.h"
#include "extra/net_alloc.h"
#include "extra/olimar_pikmin_logic.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            // FitPikmin.pac data+0x7C -> pullOutParam table (count 30 at +0x80;
            // row 0: s32 gid=0, float speed=1.0, float weights[5]={1,1,1,1,1}
            // for Red, Yellow, Blue, White, Purple; 0x80a6b864 & RSBE01.TXT:2963).
            kOlimarDataTable = 0x7C,
            kOlimarDataCount = 0x80,
            kOlimarTableEntries = 30,
            kOlimarWeightsOffset = 0x08,
            kOlimarWeightCount = 5
        };

        struct OlimarWeightCache {
            const void* data;
            float* weights;
            float orig[5];
            bool tried;
        };

        struct OlimarPlayerState {
            const Fighter* fighter;
            s32 prevStatus;
            bool startSwapPending;
            bool rebirthPending;
            u8 rebirthFrames;
            u8 initFrames;
            OlimarPendingLatch pendingPreset;
        };

        struct OlimarNet {
            OlimarWeightCache cache;
            OlimarPlayerState players[game::kMaxFighters];
        };

#ifdef __MWERKS__
        OlimarNet* s_net = NULL;
#else
        OlimarNet s_netHost;
        OlimarNet* s_net = &s_netHost;
#endif

        void clearCache(OlimarWeightCache& c)
        {
            c.data = NULL;
            c.weights = NULL;
            for (int i = 0; i < kOlimarWeightCount; i++)
            {
                c.orig[i] = 0.0f;
            }
            c.tried = false;
        }

        void resetPlayerState(OlimarPlayerState& ps)
        {
            ps.fighter = NULL;
            ps.prevStatus = -1;
            ps.startSwapPending = false;
            ps.rebirthPending = false;
            ps.rebirthFrames = 0;
            ps.initFrames = 0;
            ps.pendingPreset.clear();
        }

        void resetAllPlayers()
        {
            if (s_net == NULL)
            {
                return;
            }
            for (int i = 0; i < game::kMaxFighters; i++)
            {
                resetPlayerState(s_net->players[i]);
            }
        }

        void writeWeights(float* dst, const float* want)
        {
            for (int i = 0; i < kOlimarWeightCount; i++)
            {
                if (dst[i] != want[i])
                {
                    dst[i] = want[i];
                }
            }
        }

        bool cacheResident(const OlimarWeightCache& c)
        {
            if (c.weights == NULL || c.data == NULL)
            {
                return false;
            }
            const void* data = extraFtCommonData(kFtOlimar);
            if (data != c.data || !game::isRam(data))
            {
                return false;
            }
            u32 tableAddr = game::wordAt(data, kOlimarDataTable);
            float* weights = reinterpret_cast<float*>(tableAddr + kOlimarWeightsOffset);
            return game::isRam(reinterpret_cast<const void*>(tableAddr)) && weights == c.weights
                   && game::isRam(c.weights);
        }

        void syncCache(OlimarWeightCache& c)
        {
            const void* data = extraFtCommonData(kFtOlimar);
            if (data == NULL || data != c.data)
            {
                clearCache(c);
            }
        }

        void findOlimarWeights(OlimarWeightCache& c)
        {
            c.tried = true;
            const void* data = extraFtCommonData(kFtOlimar);
            if (!game::isRam(data))
            {
                c.data = NULL;
                TM_LOG("fighter mech: Olimar fighter data not found: Pikmin pluck left alone\n");
                return;
            }
            c.data = data;
            u32 tableAddr = game::wordAt(data, kOlimarDataTable);
            u32 count = game::wordAt(data, kOlimarDataCount);
            if (!game::isRam(reinterpret_cast<const void*>(tableAddr)) || count != kOlimarTableEntries)
            {
                TM_LOG("fighter mech: Olimar pullOutParam invalid (%08x, %u): left alone\n", tableAddr, count);
                return;
            }
            s32 gid = static_cast<s32>(game::wordAt(reinterpret_cast<const void*>(tableAddr), 0));
            float speed = floatAt(reinterpret_cast<const void*>(tableAddr), 4);
            float* weights = reinterpret_cast<float*>(tableAddr + kOlimarWeightsOffset);
            if (!game::isRam(weights))
            {
                return;
            }
            bool ok = gid == 0 && speed == 1.0f && weights[0] == 1.0f && weights[1] == 1.0f && weights[2] == 1.0f
                      && weights[3] == 1.0f && weights[4] == 1.0f;
            TM_LOG("fighter mech: Olimar Pikmin weights at %08x (gid=%d spd=%.1f %.1f/%.1f/%.1f/%.1f/%.1f): %s\n",
                   weights, gid, speed, weights[0], weights[1], weights[2], weights[3], weights[4],
                   ok ? "ok" : "not as expected: left alone");
            if (ok)
            {
                c.weights = weights;
                writeWeights(c.orig, weights);
            }
        }

        bool ensureWeights()
        {
            if (s_net == NULL)
            {
                return false;
            }
            OlimarWeightCache& c = s_net->cache;
            syncCache(c);
            if (!c.tried)
            {
                findOlimarWeights(c);
            }
            return cacheResident(c);
        }

        void applyBasePullPoolWeights(const main::FighterMechanicsSettings& s)
        {
            if (!ensureWeights())
            {
                return;
            }
            OlimarWeightCache& c = s_net->cache;
            float want[kOlimarWeightCount];
            olimarPullPoolWeights(s, c.orig, want);
            writeWeights(c.weights, want);
        }

#ifdef __MWERKS__
        typedef s32 (*ListSizeFn)(const void*);
        typedef void** (*ListAtFn)(void*, s32);
        typedef bool (*ListIsEmptyFn)(const void*);
        typedef void (*ListClearFn)(void*);
        typedef void (*ListElemFn)(void*, void* const*);
        typedef void (*ListEraseFn)(void*, s32);

        typedef int (*ArtGetIdFn)(void*);
        typedef bool (*ArtIsActiveFn)(void*);

        typedef void* (*GenArtCreateFn)(void*, s32, void*, s32);
        typedef void (*GenArtAddFn)(void*, void*);
        typedef void (*GenArtRemoveAllFn)(void*, s32, s32);
        typedef bool (*GenArtIsExistFn)(void*, s32);

        typedef void (*JostleSetPrioFn)(void*, s32);

        // Triple-validates ftPikmin's embedded PikminManager at fighter+0x1FD08:
        //  1. fighter->m_moduleAccesser->m_enumerationStart->m_glowModule + 0x1CF4 == mgr
        //  2. List 0/1/2 vptrs all equal soArrayList<wnPikminPikmin*, 10> (0x80B19960)
        u8* pikminManager(Fighter* fighter, const soModuleEnumeration* m)
        {
            if (!game::isRam(fighter) || !game::isRam(m) || !game::isRam(m->m_glowModule))
            {
                return NULL;
            }
            u8* mgr = reinterpret_cast<u8*>(fighter) + kOliMgrOffset;
            u8* fromGlow = reinterpret_cast<u8*>(m->m_glowModule) + kOliMgrFromGlowOffset;
            if (fromGlow != mgr || !game::isRam(mgr))
            {
                return NULL;
            }
            if (*reinterpret_cast<const u32*>(mgr) != kOliArrayListVptr
                || *reinterpret_cast<const u32*>(mgr + kOliMgrList1Offset) != kOliArrayListVptr
                || *reinterpret_cast<const u32*>(mgr + kOliMgrList2Offset) != kOliArrayListVptr)
            {
                return NULL;
            }
            return mgr;
        }

        s32 listSize(const void* list)
        {
            if (!game::isRam(list))
            {
                return -1;
            }
            u32* vt = *reinterpret_cast<u32* const*>(list);
            if (!game::isRam(vt))
            {
                return -1;
            }
            ListSizeFn fn = reinterpret_cast<ListSizeFn>(vt[0x14 / 4]);
            return game::isRam(reinterpret_cast<const void*>(fn)) ? fn(list) : -1;
        }

        void* listElemAt(void* list, s32 idx)
        {
            if (!game::isRam(list))
            {
                return NULL;
            }
            u32* vt = *reinterpret_cast<u32**>(list);
            if (!game::isRam(vt))
            {
                return NULL;
            }
            ListAtFn fn = reinterpret_cast<ListAtFn>(vt[0x0C / 4]);
            if (!game::isRam(reinterpret_cast<const void*>(fn)))
            {
                return NULL;
            }
            void** slot = fn(list, idx);
            return (game::isRam(slot) && game::isRam(*slot)) ? *slot : NULL;
        }

        bool listIsEmpty(const void* list)
        {
            if (!game::isRam(list))
            {
                return true;
            }
            u32* vt = *reinterpret_cast<u32* const*>(list);
            if (!game::isRam(vt))
            {
                return true;
            }
            ListIsEmptyFn fn = reinterpret_cast<ListIsEmptyFn>(vt[0x18 / 4]);
            return !game::isRam(reinterpret_cast<const void*>(fn)) || fn(list);
        }

        void listClear(void* list)
        {
            if (!game::isRam(list))
            {
                return;
            }
            u32* vt = *reinterpret_cast<u32**>(list);
            if (!game::isRam(vt))
            {
                return;
            }
            ListClearFn fn = reinterpret_cast<ListClearFn>(vt[0x28 / 4]);
            if (game::isRam(reinterpret_cast<const void*>(fn)))
            {
                fn(list);
            }
        }

        void listUnshift(void* list, void* elem)
        {
            if (!game::isRam(list))
            {
                return;
            }
            u32* vt = *reinterpret_cast<u32**>(list);
            if (!game::isRam(vt))
            {
                return;
            }
            ListElemFn fn = reinterpret_cast<ListElemFn>(vt[0x2C / 4]);
            if (game::isRam(reinterpret_cast<const void*>(fn)))
            {
                fn(list, &elem);
            }
        }

        void listPush(void* list, void* elem)
        {
            if (!game::isRam(list))
            {
                return;
            }
            u32* vt = *reinterpret_cast<u32**>(list);
            if (!game::isRam(vt))
            {
                return;
            }
            ListElemFn fn = reinterpret_cast<ListElemFn>(vt[0x30 / 4]);
            if (game::isRam(reinterpret_cast<const void*>(fn)))
            {
                fn(list, &elem);
            }
        }

        void listErase(void* list, s32 idx)
        {
            if (!game::isRam(list))
            {
                return;
            }
            u32* vt = *reinterpret_cast<u32**>(list);
            if (!game::isRam(vt))
            {
                return;
            }
            ListEraseFn fn = reinterpret_cast<ListEraseFn>(vt[0x38 / 4]);
            if (game::isRam(reinterpret_cast<const void*>(fn)))
            {
                fn(list, idx);
            }
        }

        // Reads wnPikminPikmin's color via soWorkManageModule::getInt(LA-Basic[5]).
        int pikminColor(void* pikPtr)
        {
            StageObject* obj = reinterpret_cast<StageObject*>(pikPtr);
            if (!game::isRam(obj) || !game::isRam(obj->m_moduleAccesser))
            {
                return -1;
            }
            const soModuleEnumeration* am = obj->m_moduleAccesser->m_enumerationStart;
            if (!game::isRam(am) || !game::isRam(am->m_workManageModule))
            {
                return -1;
            }
            s32 c = am->m_workManageModule->getInt(kOliVarPikminColor);
            return static_cast<u32>(c) < kOliColorCount ? static_cast<int>(c) : -1;
        }

        // Re-syncs push-box jostle priority for List 0 (exact mirror of
        // ft_pikmin.rel:0x25614 / 0x1b2ac: secondary base jostle+8, vt+0x44, 6 - i).
        void syncList0Priorities(void* list0)
        {
            s32 n = listSize(list0);
            if (n <= 0 || n > 6)
            {
                return;
            }
            for (s32 i = 0; i < n; i++)
            {
                StageObject* obj = reinterpret_cast<StageObject*>(listElemAt(list0, i));
                if (!game::isRam(obj) || !game::isRam(obj->m_moduleAccesser))
                {
                    continue;
                }
                const soModuleEnumeration* am = obj->m_moduleAccesser->m_enumerationStart;
                if (!game::isRam(am) || !game::isRam(am->m_jostleModule))
                {
                    continue;
                }
                u8* j2 = reinterpret_cast<u8*>(am->m_jostleModule) + 8;
                u32* jvt = *reinterpret_cast<u32**>(j2);
                if (!game::isRam(jvt))
                {
                    continue;
                }
                JostleSetPrioFn fn = reinterpret_cast<JostleSetPrioFn>(jvt[0x44 / 4]);
                if (game::isRam(reinterpret_cast<const void*>(fn)))
                {
                    fn(j2, 6 - i);
                }
            }
        }

        bool genArtIsExist(void* genArt, s32 articleId)
        {
            if (!game::isRam(genArt))
            {
                return false;
            }
            u32* vt = *reinterpret_cast<u32**>(genArt);
            if (!game::isRam(vt))
            {
                return false;
            }
            GenArtIsExistFn fn = reinterpret_cast<GenArtIsExistFn>(vt[0x50 / 4]);
            return game::isRam(reinterpret_cast<const void*>(fn)) && fn(genArt, articleId);
        }

        void genArtRemoveAll(void* genArt, s32 articleId)
        {
            if (!game::isRam(genArt))
            {
                return;
            }
            u32* vt = *reinterpret_cast<u32**>(genArt);
            if (!game::isRam(vt))
            {
                return;
            }
            GenArtRemoveAllFn fn = reinterpret_cast<GenArtRemoveAllFn>(vt[0x48 / 4]);
            if (game::isRam(reinterpret_cast<const void*>(fn)))
            {
                fn(genArt, articleId, 0);
            }
        }

        void genArtCreate(void* genArt, s32 articleId)
        {
            if (!game::isRam(genArt))
            {
                return;
            }
            u32* vt = *reinterpret_cast<u32**>(genArt);
            if (!game::isRam(vt))
            {
                return;
            }
            GenArtCreateFn fn = reinterpret_cast<GenArtCreateFn>(vt[0x14 / 4]);
            if (!game::isRam(reinterpret_cast<const void*>(fn)))
            {
                return;
            }
            void* art = fn(genArt, articleId, NULL, 0);
            // Register the created soArticle* in m_articles (+0x2C) and its
            // soArticleEventObserver via addArticle (vtable +0x3C = 0x8079f814),
            // which is idempotent and guards isNull().
            if (game::isRam(art))
            {
                GenArtAddFn addFn = reinterpret_cast<GenArtAddFn>(vt[0x3C / 4]);
                if (game::isRam(reinterpret_cast<const void*>(addFn)))
                {
                    addFn(genArt, art);
                }
            }
        }

        // Locks each active wnPikminPikmin's maturity by color (LA-Basic[5]),
        // zeroes growth timer LA-Basic[9], and syncs visibility group 0 via
        // setStatusDefault + setDefaultAll (ft_pikmin.rel:0x1b61c..0x1b828).
        void applyOlimarMaturity(const soModuleEnumeration* m, const main::FighterMechanicsSettings& s)
        {
            if (!olimarHasAnyMaturityLock(s) || !game::isRam(m->m_generateArticleManageModule))
            {
                return;
            }
            void* arr = reinterpret_cast<void*>(game::wordAt(m->m_generateArticleManageModule, 0x2C));
            if (!game::isRam(arr))
            {
                return;
            }
            s32 count = listSize(arr);
            if (count <= 0 || count > 16)
            {
                return;
            }
            for (s32 i = 0; i < count; i++)
            {
                void* art = listElemAt(arr, i);
                if (!game::isRam(art))
                {
                    continue;
                }
                u32* artVt = *reinterpret_cast<u32**>(art);
                if (!game::isRam(artVt))
                {
                    continue;
                }
                ArtGetIdFn idFn = reinterpret_cast<ArtGetIdFn>(artVt[0x20 / 4]);
                if (!game::isRam(reinterpret_cast<const void*>(idFn)) || idFn(art) != kPikminArticleId)
                {
                    continue;
                }
                // Guard active article via soArticle::isActiveArticle() (vtable + 0x58).
                ArtIsActiveFn activeFn = reinterpret_cast<ArtIsActiveFn>(artVt[0x58 / 4]);
                if (!game::isRam(reinterpret_cast<const void*>(activeFn)) || !activeFn(art))
                {
                    continue;
                }
                // MWERKS secondary-base top-offset at vtable+4 (-0x88 inside Weapon).
                s32 topOff = static_cast<s32>(artVt[1]);
                if (topOff > 0 || topOff < -0x200)
                {
                    continue;
                }
                StageObject* obj = reinterpret_cast<StageObject*>(reinterpret_cast<u8*>(art) + topOff);
                if (!game::isRam(obj) || !game::isRam(obj->m_moduleAccesser))
                {
                    continue;
                }
                const soModuleEnumeration* am = obj->m_moduleAccesser->m_enumerationStart;
                if (!game::isRam(am) || !game::isRam(am->m_workManageModule) || !game::isRam(am->m_visibilityModule))
                {
                    continue;
                }
                soWorkManageModule* aWork = am->m_workManageModule;
                soVisibilityModule* aVis = am->m_visibilityModule;
                s32 color = aWork->getInt(kOliVarPikminColor);
                int target = olimarResolvedMaturityTarget(s, static_cast<int>(color));
                if (target < 0)
                {
                    continue;
                }
                if (game::isRam(am->m_statusModule))
                {
                    int pStatus = game::fighterStatus(am);
                    if (olimarPikminStatusDying(pStatus))
                    {
                        continue;
                    }
                }
                if (aWork->getInt(kVarPikminGrowTimer) != 0)
                {
                    aWork->setInt(0, kVarPikminGrowTimer);
                }
                if (aWork->getInt(kVarPikminMaturity) != target || aVis->get(0) != target)
                {
                    aWork->setInt(target, kVarPikminMaturity);
                    aVis->setStatusDefault(0, static_cast<u8>(target), true);
                    aVis->setDefaultAll();
                }
            }
        }

        bool isFighterInHitlag(const soModuleEnumeration* m)
        {
            if (!game::isRam(m) || !game::isRam(m->m_stopModule))
            {
                return false;
            }
            soStopModule* stop = m->m_stopModule;
            return stop->isHit() || stop->isDamage() || stop->isStop();
        }

        bool checkSafeToSwap(const soModuleEnumeration* m, int status, u8* mgr)
        {
            if (mgr == NULL || !game::isRam(m->m_generateArticleManageModule) || !ensureWeights())
            {
                return false;
            }
            game::MatchState ms = game::matchState();
            if (!ms.running || !ms.training || ms.paused)
            {
                return false;
            }
            void* genArt = m->m_generateArticleManageModule;
            bool exist1 = genArtIsExist(genArt, 1);
            bool exist2 = genArtIsExist(genArt, 2);
            s32 list1Sz = listSize(mgr + kOliMgrList1Offset);
            bool hitlag = isFighterInHitlag(m);
            return olimarCanSwapLineup(status, hitlag, exist1, exist2, static_cast<int>(list1Sz), fighterSwapBusy());
        }

        bool lineupMatchesList0(u8* mgr, const OlimarLineupSpec& spec)
        {
            if (mgr == NULL)
            {
                return false;
            }
            s32 n = listSize(mgr);
            if (n != static_cast<s32>(spec.count))
            {
                return false;
            }
            for (s32 i = 0; i < n; i++)
            {
                if (spec.colors[i] >= 0 && pikminColor(listElemAt(mgr, i)) != static_cast<int>(spec.colors[i]))
                {
                    return false;
                }
            }
            return true;
        }

        // Native despawn + ordered respawn (0..4 Pikmin), restoring base pull
        // pool weights and immediately applying per-color maturity locks.
        void applyOlimarLineup(Fighter* fighter, const soModuleEnumeration* m,
                               const main::FighterMechanicsSettings& s, const OlimarLineupSpec& spec)
        {
            u8* mgr = pikminManager(fighter, m);
            if (mgr == NULL || !game::isRam(m->m_generateArticleManageModule) || !game::isRam(m->m_workManageModule)
                || !ensureWeights())
            {
                return;
            }
            void* genArt = m->m_generateArticleManageModule;
            soWorkManageModule* work = m->m_workManageModule;
            OlimarWeightCache& c = s_net->cache;

            genArtRemoveAll(genArt, kPikminArticleId);
            // Belt-and-suspenders: guarantee zero stale pointers in Lists 0/1/2.
            if (!listIsEmpty(mgr))
            {
                listClear(mgr);
            }
            if (!listIsEmpty(mgr + kOliMgrList1Offset))
            {
                listClear(mgr + kOliMgrList1Offset);
            }
            if (!listIsEmpty(mgr + kOliMgrList2Offset))
            {
                listClear(mgr + kOliMgrList2Offset);
            }

            float want[kOlimarWeightCount];
            for (u8 i = 0; i < spec.count && i < 4u; i++)
            {
                if (spec.colors[i] >= 0 && spec.colors[i] < kOliColorCount)
                {
                    olimarSingleColorWeights(spec.colors[i], want);
                    writeWeights(c.weights, want);
                    work->setInt(-1, kOliVarLastPull);
                    work->setInt(-1, kOliVarPrevPull);
                }
                else
                {
                    olimarPullPoolWeights(s, c.orig, want);
                    writeWeights(c.weights, want);
                }
                genArtCreate(genArt, kPikminArticleId);
            }

            // Restore base Pull Pool weights and re-apply maturity + whistle seed.
            olimarPullPoolWeights(s, c.orig, want);
            writeWeights(c.weights, want);
            applyOlimarMaturity(m, s);
            u8 whistle = olimarWhistleColor(s);
            if (whistle > 0u)
            {
                *reinterpret_cast<s32*>(mgr + kOliMgrWhistleColorOffset) = olimarWhistleSeedForColor(whistle - 1);
            }
        }

        void moveLeadNowOnFighter(Fighter* fighter, const soModuleEnumeration* m,
                                  const main::FighterMechanicsSettings& s)
        {
            u8* mgr = pikminManager(fighter, m);
            if (mgr == NULL || fighterSwapBusy())
            {
                return;
            }
            int status = game::fighterStatus(m);
            if (olimarStatusBlockedForSwap(status))
            {
                return;
            }
            s32 n = listSize(mgr);
            if (n <= 0 || n > 6)
            {
                return;
            }
            u8 whistle = olimarWhistleColor(s);
            if (whistle > 0u)
            {
                int wantColor = static_cast<int>(whistle) - 1;
                if (pikminColor(listElemAt(mgr, 0)) == wantColor)
                {
                    *reinterpret_cast<s32*>(mgr + kOliMgrWhistleColorOffset) = wantColor;
                    return;
                }
                for (s32 k = 1; k < n; k++)
                {
                    void* pik = listElemAt(mgr, k);
                    if (pik != NULL && pikminColor(pik) == wantColor)
                    {
                        listErase(mgr, k);
                        listUnshift(mgr, pik);
                        syncList0Priorities(mgr);
                        *reinterpret_cast<s32*>(mgr + kOliMgrWhistleColorOffset) = wantColor;
                        TM_LOG("olimar: moved lead color %d to front (List0 size %d)\n", wantColor, n);
                        return;
                    }
                }
                return;
            }
            // Default whistle color: rotate lead Pikmin to back so the next one leads.
            if (n >= 2)
            {
                void* front = listElemAt(mgr, 0);
                if (front != NULL)
                {
                    listErase(mgr, 0);
                    listPush(mgr, front);
                    syncList0Priorities(mgr);
                    int newLead = pikminColor(listElemAt(mgr, 0));
                    if (newLead >= 0)
                    {
                        *reinterpret_cast<s32*>(mgr + kOliMgrWhistleColorOffset) = newLead;
                    }
                    TM_LOG("olimar: rotated lead to color %d (List0 size %d)\n", newLead, n);
                }
            }
        }

        int activeKind(const game::FighterRef& ref)
        {
            ftEntry* entry = extraEntry(ref.entryId);
            if (!game::isRam(entry) || entry->m_entryId != ref.entryId)
            {
                return -1;
            }
            int active = entry->m_activeInstanceIndex;
            return (active >= 0 && active < 4) ? static_cast<int>(entry->m_instances[active].m_kind) : -1;
        }
#endif
    }

    void olimarLabAttach()
    {
#ifdef __MWERKS__
        freeNetPtr(s_net);
        s_net = allocNetArray<OlimarNet>(1);
#else
        s_net = &s_netHost;
#endif
        if (s_net == NULL)
        {
            return;
        }
        clearCache(s_net->cache);
        resetAllPlayers();
    }

    void olimarLabSync()
    {
        if (s_net == NULL)
        {
            return;
        }
        syncCache(s_net->cache);
    }

    void olimarLabNoOlimarFrame()
    {
        if (s_net == NULL)
        {
            return;
        }
        OlimarWeightCache& c = s_net->cache;
        if (cacheResident(c))
        {
            writeWeights(c.weights, c.orig);
        }
        resetAllPlayers();
    }

    void olimarLabApplyFighter(Fighter* fighter, const soModuleEnumeration* m, int status, int playerNo, bool isCpu)
    {
        if (s_net == NULL || m == NULL || !game::isRam(m->m_workManageModule))
        {
            return;
        }
        const main::FighterMechanicsSettings& s = main::settings().fighterMechanics;
        applyBasePullPoolWeights(s);
#ifndef __MWERKS__
        (void)fighter;
        (void)status;
        (void)playerNo;
        (void)isCpu;
#else
        u8* mgr = pikminManager(fighter, m);
        // Feature 5: Down-B Whistle lead color seed (+0x1FE88).
        u8 whistle = olimarWhistleColor(s);
        if (whistle > 0u && mgr != NULL)
        {
            *reinterpret_cast<s32*>(mgr + kOliMgrWhistleColorOffset) = olimarWhistleSeedForColor(whistle - 1);
        }

        if (playerNo >= 0 && playerNo < game::kMaxFighters)
        {
            OlimarPlayerState& ps = s_net->players[playerNo];
            if (ps.fighter != fighter)
            {
                resetPlayerState(ps);
                ps.fighter = fighter;
                // Training Mode spawns Olimar straight into Wait (skipping Entry),
                // so arm Starting Lineup on every newly-seen Olimar instance too.
                ps.startSwapPending = olimarStartLineupEnabled(s);
            }
            if (status == kOliStatusEntry)
            {
                ps.startSwapPending = true;
                ps.rebirthPending = false;
                ps.rebirthFrames = 0;
                ps.initFrames = 0;
                // Feature 3 Option A: zero-flicker Entry interception.
                if (olimarStartLineupEnabled(s) && mgr != NULL && ensureWeights())
                {
                    OlimarLineupSpec spec;
                    olimarResolveStartLineup(s, spec);
                    s32 curN = listSize(mgr);
                    if (curN >= 0 && curN < static_cast<s32>(spec.count) && spec.colors[curN] >= 0)
                    {
                        float want[kOlimarWeightCount];
                        olimarSingleColorWeights(spec.colors[curN], want);
                        writeWeights(s_net->cache.weights, want);
                        m->m_workManageModule->setInt(-1, kOliVarLastPull);
                        m->m_workManageModule->setInt(-1, kOliVarPrevPull);
                    }
                }
            }
            else if (status == kOliStatusRebirth)
            {
                ps.rebirthPending = true;
                ps.rebirthFrames = 0;
            }
            else if (!game::outOfPlay(status, true))
            {
                bool safeNow = checkSafeToSwap(m, status, mgr);
                // Feature 6: Instant D-Pad Lineup Presets (P1 / human Olimar).
                int pendingId = !isCpu ? ps.pendingPreset.step(safeNow) : -1;
                if (pendingId >= 0)
                {
                    OlimarLineupSpec spec;
                    olimarResolvePreset(static_cast<u8>(pendingId), s, spec);
                    applyOlimarLineup(fighter, m, s, spec);
                    ps.startSwapPending = false;
                    ps.rebirthPending = false;
                }
                else if (ps.startSwapPending)
                {
                    if (!olimarStartLineupEnabled(s))
                    {
                        ps.startSwapPending = false;
                    }
                    else
                    {
                        s32 curN = mgr != NULL ? listSize(mgr) : 0;
                        if (curN <= 0 && ps.initFrames < 6)
                        {
                            ps.initFrames++;
                        }
                        else if (safeNow)
                        {
                            OlimarLineupSpec spec;
                            olimarResolveStartLineup(s, spec);
                            if (!lineupMatchesList0(mgr, spec))
                            {
                                applyOlimarLineup(fighter, m, s, spec);
                            }
                            ps.startSwapPending = false;
                        }
                    }
                }
                else if (ps.rebirthPending)
                {
                    if (!olimarStartLineupEnabled(s))
                    {
                        ps.rebirthPending = false;
                    }
                    else
                    {
                        s32 curN = mgr != NULL ? listSize(mgr) : 0;
                        // FitPikmin.pac Wait1 after Rebirth spawns 2 Pikmin at Async frame 45
                        // when active < 2; wait until that spawn completes or Olimar moves.
                        if (status == game::kStatusWait && curN < 2 && ps.rebirthFrames < 48)
                        {
                            ps.rebirthFrames++;
                        }
                        else if (safeNow)
                        {
                            OlimarLineupSpec spec;
                            olimarResolveStartLineup(s, spec);
                            if (!lineupMatchesList0(mgr, spec))
                            {
                                applyOlimarLineup(fighter, m, s, spec);
                            }
                            ps.rebirthPending = false;
                        }
                    }
                }
            }
            ps.prevStatus = status;
        }

        // Feature 2: Per-color maturity locks + global Set All.
        applyOlimarMaturity(m, s);
#endif
    }

    void olimarLabAction(u32 action, u32 dpadHeld, u32 dpadPressed)
    {
        if (s_net == NULL || fighterSwapBusy())
        {
            return;
        }
        if (action == main::kOlimarSavestateLoaded)
        {
            // Keep ps.fighter intact so loading a savestate never re-triggers
            // Starting Lineup on the existing match's Fighter instance.
            for (int i = 0; i < game::kMaxFighters; i++)
            {
                OlimarPlayerState& ps = s_net->players[i];
                ps.startSwapPending = false;
                ps.rebirthPending = false;
                ps.rebirthFrames = 0;
                ps.initFrames = 0;
                ps.pendingPreset.clear();
            }
            return;
        }
#ifndef __MWERKS__
        (void)dpadHeld;
        (void)dpadPressed;
#else
        const main::FighterMechanicsSettings& s = main::settings().fighterMechanics;
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);

        if (action == main::kOlimarMoveLead)
        {
            for (int i = 0; i < n; i++)
            {
                if (activeKind(refs[i]) != kFtOlimar)
                {
                    continue;
                }
                const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
                if (m != NULL)
                {
                    moveLeadNowOnFighter(refs[i].fighter, m, s);
                }
            }
            return;
        }

        if (action != main::kOlimarPreStep)
        {
            return;
        }
        u8 mode = olimarDpadMode(s);
        if (mode == main::kOlimarDpadOff)
        {
            return;
        }
        for (int i = 0; i < n; i++)
        {
            if (refs[i].cpu || activeKind(refs[i]) != kFtOlimar)
            {
                continue;
            }
            if (mode == main::kOlimarDpadPluck)
            {
                // Feature 4: Frame-0 pre-step weight override for Neutral-B + D-Pad Pluck.
                if (ensureWeights())
                {
                    int color = olimarPluckColorForDpad(s, dpadHeld);
                    float want[kOlimarWeightCount];
                    if (color >= 0)
                    {
                        olimarSingleColorWeights(color, want);
                    }
                    else
                    {
                        olimarPullPoolWeights(s, s_net->cache.orig, want);
                    }
                    writeWeights(s_net->cache.weights, want);
                }
            }
            else if (mode == main::kOlimarDpadPresets && dpadPressed != 0u)
            {
                int dir = olimarPresetDirFromDpad(dpadPressed);
                if (dir >= 0 && refs[i].playerNo >= 0 && refs[i].playerNo < game::kMaxFighters)
                {
                    u8 presetId = olimarDpadPreset(s, dir);
                    s_net->players[refs[i].playerNo].pendingPreset.latch(static_cast<s8>(presetId));
                }
            }
            break;
        }
#endif
    }

    void olimarLabPrepareSwap()
    {
        if (s_net == NULL)
        {
            return;
        }
        OlimarWeightCache& oli = s_net->cache;
        if (cacheResident(oli))
        {
            const void* liveData = extraFtCommonData(kFtOlimar);
            TM_LOG("fighter mech: detach Olimar Pikmin weights %08x (put back %.1f/%.1f/%.1f/%.1f/%.1f, live data %08x)\n",
                   oli.weights, oli.orig[0], oli.orig[1], oli.orig[2], oli.orig[3], oli.orig[4], liveData);
            writeWeights(oli.weights, oli.orig);
        }
        clearCache(oli);
        resetAllPlayers();
    }

    void olimarLabDetach()
    {
        olimarLabPrepareSwap();
#ifdef __MWERKS__
        freeNetPtr(s_net);
#endif
    }
}
}
