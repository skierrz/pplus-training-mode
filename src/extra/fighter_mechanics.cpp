// Per-fighter mechanics backend (fighter_mechanics.h): applies P+ 3.2.1
// charge, resource, float, and Dedede Waddle Toss overrides across all
// matching active fighters in the match. Pure mapping logic lives in
// fighter_mechanics_logic.h so host tests can verify every fighter directly.
#include "extra/fighter_mechanics.h"

#include <ft/fighter.h>
#include <ft/ft_common_data_accesser.h>
#ifdef __MWERKS__
#include <so/effect/so_effect_module_impl.h>
#include <so/stageobject.h>
#include <so/visibility/so_visibility_module_impl.h>
#endif
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <so/work/so_work_manage_module_impl.h>

#include "extra/fighter_mechanics_logic.h"
#include "extra/net_alloc.h"
#include "extra/olimar_lab.h"
#include "main/game/fighters.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            // FitDedede.pac data+0x80 -> Waddle Toss float table (+0x18..+0x24:
            // Dee = 35.0, Doo = 10.0, Gordo = 4.0, Capsule = 0.0).
            kDededeDataTable = 0x80,
            kDededeWeightsOffset = 0x18,
            kDededeWeightCount = 4,
            kDededeParamDee = 4015,
            kDededeParamDoo = 4016,
            kDededeParamGordo = 4017
        };

        struct WeightCache {
            const void* data;
            float* weights;
            float orig[5];
            bool tried;
        };

#ifdef __MWERKS__
        struct VisualSlot {
            const Fighter* fighter;
            u8 ftKind;
            u8 kind; // CustomVisualKind
            u8 tier;
            s32 h0;
        };
#endif

        struct FighterMechNet {
            WeightCache dedede;
#ifdef __MWERKS__
            VisualSlot vis[game::kMaxFighters];
#endif
        };

#ifdef __MWERKS__
        FighterMechNet* s_net = NULL;
#else
        FighterMechNet s_netHost;
        FighterMechNet* s_net = &s_netHost;
#endif

#ifdef __MWERKS__
        void resetVisualSlot(VisualSlot& v)
        {
            v.fighter = NULL;
            v.ftKind = 0xFF;
            v.kind = kVisNone;
            v.tier = 0;
            v.h0 = -1;
        }

        void clearVisualSlots()
        {
            if (s_net == NULL)
            {
                return;
            }
            for (int i = 0; i < game::kMaxFighters; i++)
            {
                resetVisualSlot(s_net->vis[i]);
            }
        }

        // BrawlHeaders' soEffectModule::killKind(bool, bool, int) has swapped parameter
        // order; sora_melee 0x807a48c8 (vtable + 0x64) takes (this, u32 effectId, bool, bool).
        typedef void (*EffectKillKindFn)(soEffectModule*, u32, bool, bool);
        void effectKillKind(soEffectModule* eff, u32 effectId, bool b1, bool b2)
        {
            u32* vtbl = *reinterpret_cast<u32**>(eff);
            if (!game::isRam(vtbl))
            {
                return;
            }
            EffectKillKindFn fn = reinterpret_cast<EffectKillKindFn>(vtbl[0x64 / 4]);
            if (game::isRam(reinterpret_cast<const void*>(fn)))
            {
                fn(eff, effectId, b1, b2);
            }
        }

        struct CustomVisSpec {
            u32 effectId;
            u8 bone0, bone1;
            u8 isExternal;
            u8 killB1, killB2;
            u8 killAfterImage;
            float yOff;
            float scale1, scale2;
        };

        void killCustomEffect(soEffectModule* eff, const CustomVisSpec& sp)
        {
            if (sp.killAfterImage != 0)
            {
                eff->removeAllAfterImage(0, 1);
            }
            effectKillKind(eff, sp.effectId, sp.killB1 != 0, sp.killB2 != 0);
        }

        // Verified against P+ 3.2.1 Fit*.pac PSA scripts:
        //  - Lucas (FitLucas.pac ActExit[6] sub@0x28544): 0x1B0001 on bones 28 & 51, scale 0.5, external
        //  - Ivysaur (FitPokeFushigisou.pac Wait1 0x206cc): 0x84 on bone 33, y=1.5, scale 0.06 / 0.16, common
        //  - Lucario (FitLucario.pac 0x29b40): 0x220001 on bones 31 & 53, scale 0.8 / 1.0, external
        static const CustomVisSpec kCustomSpecs[3] = {
            {0x1B0001u, 28, 51, 1, 0, 1, 0, 0.0f, 0.5f, 0.5f},
            {0x000084u, 33, 0,  0, 0, 0, 0, 1.5f, 0.06f, 0.16f},
            {0x220001u, 31, 53, 1, 1, 1, 1, 0.0f, 0.8f, 1.0f}
        };

        void syncFighterVisuals(const Fighter* fighter, const soModuleEnumeration* m, int ftKind, int status,
                                int playerNo, const FighterPlan& plan, bool hadPreVisual)
        {
            if (s_net == NULL || playerNo < 0 || playerNo >= game::kMaxFighters)
            {
                return;
            }
            VisualSlot& slot = s_net->vis[playerNo];
            if (slot.fighter != fighter || slot.ftKind != static_cast<u8>(ftKind))
            {
                resetVisualSlot(slot);
                slot.fighter = fighter;
                slot.ftKind = static_cast<u8>(ftKind);
            }
            if (game::outOfPlay(status, true))
            {
                slot.kind = kVisNone;
                slot.tier = 0;
                slot.h0 = -1;
                return;
            }
            soEffectModule* eff = m->m_effectModule;
            if (!game::isRam(eff) || eff->isNull())
            {
                return;
            }
            if (plan.commonEffectId != 0)
            {
                int cid = static_cast<int>(plan.commonEffectId);
                bool exists = eff->isExistCommon(cid);
                if (plan.commonEffectOn != 0)
                {
                    if (!exists)
                    {
                        eff->reqCommon(0.0f, cid);
                    }
                }
                else if (exists)
                {
                    eff->removeCommon(cid);
                }
            }
            if (plan.customVisual == kVisNone || plan.customVisual > kVisLucarioAura)
            {
                return;
            }
            const CustomVisSpec& sp = kCustomSpecs[plan.customVisual - 1];
            bool alive = slot.kind == plan.customVisual && slot.h0 >= 0 && eff->isExistEffect(slot.h0);
            if (plan.customTier == 0)
            {
                if ((slot.kind == plan.customVisual && slot.tier != 0) || hadPreVisual || alive)
                {
                    killCustomEffect(eff, sp);
                }
                slot.kind = plan.customVisual;
                slot.tier = 0;
                slot.h0 = -1;
                return;
            }
            if (slot.kind == plan.customVisual && slot.tier == plan.customTier && alive)
            {
                return;
            }
            killCustomEffect(eff, sp);
            Vec3f pos(0.0f, sp.yOff, 0.0f);
            Vec3f rot(0.0f, 0.0f, 0.0f);
            float scale = plan.customTier == 1 ? sp.scale1 : sp.scale2;
            EfID eid = static_cast<EfID>(sp.effectId);
            slot.h0 = static_cast<s32>(eff->reqFollow(eid, sp.bone0, &pos, &rot, scale, sp.isExternal != 0, 0, 0, 0));
            if (sp.bone1 != 0)
            {
                eff->reqFollow(eid, sp.bone1, &pos, &rot, scale, sp.isExternal != 0, 0, 0, 0);
            }
            slot.kind = plan.customVisual;
            slot.tier = plan.customTier;
        }
#endif

        void clearCache(WeightCache& c)
        {
            c.data = NULL;
            c.weights = NULL;
            for (int i = 0; i < 5; i++)
            {
                c.orig[i] = 0.0f;
            }
            c.tried = false;
        }

        bool cacheResident(const WeightCache& c, int kind, u32 tableOff, u32 weightsOff)
        {
            if (c.weights == NULL || c.data == NULL)
            {
                return false;
            }
            const void* data = extraFtCommonData(kind);
            if (data != c.data || !game::isRam(data))
            {
                return false;
            }
            u32 tableAddr = game::wordAt(data, tableOff);
            float* weights = reinterpret_cast<float*>(tableAddr + weightsOff);
            return game::isRam(reinterpret_cast<const void*>(tableAddr)) && weights == c.weights
                   && game::isRam(c.weights);
        }

        void syncCache(WeightCache& c, int kind)
        {
            const void* data = extraFtCommonData(kind);
            if (data == NULL || data != c.data)
            {
                clearCache(c);
            }
        }

        void writeWeights(float* dst, const float* want, int count)
        {
            for (int i = 0; i < count; i++)
            {
                if (dst[i] != want[i])
                {
                    dst[i] = want[i];
                }
            }
        }

        void findDededeWeights(WeightCache& c, Fighter* fighter)
        {
            c.tried = true;
            const void* data = extraFtCommonData(kFtDedede);
            if (!game::isRam(data))
            {
                c.data = NULL;
                TM_LOG("fighter mech: Dedede fighter data not found: Waddle Toss left alone\n");
                return;
            }
            c.data = data;
            u32 tableAddr = game::wordAt(data, kDededeDataTable);
            if (!game::isRam(reinterpret_cast<const void*>(tableAddr)))
            {
                TM_LOG("fighter mech: Dedede Waddle table pointer invalid: left alone\n");
                return;
            }
            float* weights = reinterpret_cast<float*>(tableAddr + kDededeWeightsOffset);
            if (!game::isRam(weights))
            {
                return;
            }
            // Validate against both FitDedede.pac's known P+ 3.2.1 weights
            // (35, 10, 4, 0) and soValueAccesser's live IC-Float[4015..4017].
            float cDee = extraConstFloat(fighter->m_moduleAccesser, kDededeParamDee);
            float cDoo = extraConstFloat(fighter->m_moduleAccesser, kDededeParamDoo);
            float cGordo = extraConstFloat(fighter->m_moduleAccesser, kDededeParamGordo);
            bool ok = weights[0] == 35.0f && weights[1] == 10.0f && weights[2] == 4.0f && weights[3] == 0.0f
                      && cDee == weights[0] && cDoo == weights[1] && cGordo == weights[2];
            TM_LOG("fighter mech: Dedede Waddle weights at %08x (%.1f/%.1f/%.1f/%.1f, const %.1f/%.1f/%.1f): %s\n",
                   weights, weights[0], weights[1], weights[2], weights[3], cDee, cDoo, cGordo,
                   ok ? "ok" : "not as expected: left alone");
            if (ok)
            {
                c.weights = weights;
                writeWeights(c.orig, weights, kDededeWeightCount);
            }
        }

        void applyDededeWeights(Fighter* fighter, u8 choice)
        {
            if (s_net == NULL)
            {
                return;
            }
            WeightCache& c = s_net->dedede;
            syncCache(c, kFtDedede);
            if (!c.tried)
            {
                findDededeWeights(c, fighter);
            }
            if (!cacheResident(c, kFtDedede, kDededeDataTable, kDededeWeightsOffset))
            {
                return;
            }
            float want[kDededeWeightCount];
            bool clearCd = false;
            dededeWaddleWeights(choice, c.orig, want, clearCd);
            writeWeights(c.weights, want, kDededeWeightCount);
        }

        void executePlan(soWorkManageModule* work, const FighterPlan& plan)
        {
            for (int i = 0; i < plan.count; i++)
            {
                const WorkOp& op = plan.ops[i];
                if (op.kind == kOpInt)
                {
                    if (work->getInt(op.id) != op.intValue)
                    {
                        work->setInt(op.intValue, op.id);
                    }
                }
                else if (op.kind == kOpFloat)
                {
                    if (work->getFloat(op.id) != op.floatValue)
                    {
                        work->setFloat(op.floatValue, op.id);
                    }
                }
                else if (op.kind == kOpFlag)
                {
                    if (op.flagValue != 0)
                    {
                        work->onFlag(op.id);
                    }
                    else
                    {
                        work->offFlag(op.id);
                    }
                }
            }
        }
    }

    void fighterMechanicsAttach()
    {
#ifdef __MWERKS__
        freeNetPtr(s_net);
        s_net = allocNetArray<FighterMechNet>(1);
#else
        s_net = &s_netHost;
#endif
        if (s_net == NULL)
        {
            return;
        }
        clearCache(s_net->dedede);
        olimarLabAttach();
#ifdef __MWERKS__
        clearVisualSlots();
#endif
    }

    void fighterMechanicsSync()
    {
        if (s_net == NULL)
        {
            return;
        }
        syncCache(s_net->dedede, kFtDedede);
        olimarLabSync();
    }

    void fighterMechanicsApplyFighter(Fighter* fighter, const soModuleEnumeration* m, int ftKind, int status,
                                      int playerNo, bool isCpu)
    {
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return;
        }
        soWorkManageModule* work = m->m_workManageModule;
        const main::FighterMechanicsSettings& s = main::settings().fighterMechanics;
        bool mewtwoHoverActive = ftKind == kFtMewtwo && work->isFlag(kVarMewtwoHoverActive);
        bool lucasPreFirstAttack = ftKind == kFtLucas && work->getInt(kVarLucasAttackSerial) == 0;
#ifdef __MWERKS__
        // Read pre-plan state before executePlan() clears flags/floats so savestate loads
        // into Off/Empty/0 cleanly kill any restored custom follow effects.
        bool hadPreVisual = (ftKind == kFtLucas && work->isFlag(kVarLucasOffenseFlag))
                            || (ftKind == kFtIvysaur && work->getFloat(kVarIvysaurSolarbeam) > 0.0f)
                            || (ftKind == kFtLucario && work->getFloat(kVarLucarioAura) > 0.0f);
#endif
        FighterPlan plan;
        buildFighterPlan(ftKind, status, mewtwoHoverActive, s, plan, lucasPreFirstAttack);
        executePlan(work, plan);
#ifdef __MWERKS__
        syncFighterVisuals(fighter, m, ftKind, status, playerNo, plan, hadPreVisual);
#endif
        if (ftKind == kFtDedede && fighter != NULL)
        {
            applyDededeWeights(fighter, s.dededeWaddle);
        }
        else if (ftKind == kFtOlimar)
        {
            olimarLabApplyFighter(fighter, m, status, playerNo, isCpu);
        }
    }

    void fighterMechanicsPrepareSwap()
    {
        olimarLabPrepareSwap();
        if (s_net == NULL)
        {
            return;
        }
        WeightCache& ddd = s_net->dedede;
        if (cacheResident(ddd, kFtDedede, kDededeDataTable, kDededeWeightsOffset))
        {
            const void* liveData = extraFtCommonData(kFtDedede);
            TM_LOG("fighter mech: detach Dedede Waddle weights %08x (put back %.1f/%.1f/%.1f/%.1f, live data %08x)\n",
                   ddd.weights, ddd.orig[0], ddd.orig[1], ddd.orig[2], ddd.orig[3], liveData);
            writeWeights(ddd.weights, ddd.orig, kDededeWeightCount);
        }
        clearCache(ddd);
#ifdef __MWERKS__
        clearVisualSlots();
#endif
    }

    void fighterMechanicsDetach()
    {
        fighterMechanicsPrepareSwap();
        olimarLabDetach();
#ifdef __MWERKS__
        freeNetPtr(s_net);
#endif
    }
}
}
