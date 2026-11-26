// Per-fighter mechanics backend (fighter_mechanics.h): applies P+ 3.2.1
// charge, resource, float, and Dedede Waddle Toss overrides across all
// matching active fighters in the match. Pure mapping logic lives in
// fighter_mechanics_logic.h so host tests can verify every fighter directly.
#include "extra/fighter_mechanics.h"

#include <ft/fighter.h>
#include <ft/ft_common_data_accesser.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <so/work/so_work_manage_module_impl.h>

#include "extra/fighter_mechanics_logic.h"
#include "extra/net_alloc.h"
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
            kDededeParamGordo = 4017,
            // FitPikmin.pac data+0x7C -> pullOutParam table (count 30 at +0x80;
            // row 0: s32 gid=0, float speed=1.0, float weights[5]={1,1,1,1,1}
            // for Red, Yellow, Blue, White, Purple; 0x80a6b864 & RSBE01.TXT:2963).
            kOlimarDataTable = 0x7C,
            kOlimarDataCount = 0x80,
            kOlimarTableEntries = 30,
            kOlimarWeightsOffset = 0x08,
            kOlimarWeightCount = 5
        };

        struct WeightCache {
            const void* data;
            float* weights;
            float orig[5];
            bool tried;
        };

        WeightCache s_dedede = {NULL, NULL, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, false};
        WeightCache s_olimar = {NULL, NULL, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, false};

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

        void findOlimarWeights()
        {
            s_olimar.tried = true;
            const void* data = extraFtCommonData(kFtOlimar);
            if (!game::isRam(data))
            {
                s_olimar.data = NULL;
                TM_LOG("fighter mech: Olimar fighter data not found: Pikmin pluck left alone\n");
                return;
            }
            s_olimar.data = data;
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
                s_olimar.weights = weights;
                writeWeights(s_olimar.orig, weights, kOlimarWeightCount);
            }
        }

        void applyOlimarWeights(u8 choice)
        {
            syncCache(s_olimar, kFtOlimar);
            if (!s_olimar.tried)
            {
                findOlimarWeights();
            }
            if (!cacheResident(s_olimar, kFtOlimar, kOlimarDataTable, kOlimarWeightsOffset))
            {
                return;
            }
            float want[kOlimarWeightCount];
            olimarPikminWeights(choice, s_olimar.orig, want);
            writeWeights(s_olimar.weights, want, kOlimarWeightCount);
        }

        void findDededeWeights(Fighter* fighter)
        {
            s_dedede.tried = true;
            const void* data = extraFtCommonData(kFtDedede);
            if (!game::isRam(data))
            {
                s_dedede.data = NULL;
                TM_LOG("fighter mech: Dedede fighter data not found: Waddle Toss left alone\n");
                return;
            }
            s_dedede.data = data;
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
                s_dedede.weights = weights;
                writeWeights(s_dedede.orig, weights, kDededeWeightCount);
            }
        }

        void applyDededeWeights(Fighter* fighter, u8 choice)
        {
            syncCache(s_dedede, kFtDedede);
            if (!s_dedede.tried)
            {
                findDededeWeights(fighter);
            }
            if (!cacheResident(s_dedede, kFtDedede, kDededeDataTable, kDededeWeightsOffset))
            {
                return;
            }
            float want[kDededeWeightCount];
            bool clearCd = false;
            dededeWaddleWeights(choice, s_dedede.orig, want, clearCd);
            writeWeights(s_dedede.weights, want, kDededeWeightCount);
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
        clearCache(s_dedede);
        clearCache(s_olimar);
    }

    void fighterMechanicsSync()
    {
        syncCache(s_dedede, kFtDedede);
        syncCache(s_olimar, kFtOlimar);
    }

    void fighterMechanicsApplyFighter(Fighter* fighter, const soModuleEnumeration* m, int ftKind, int status)
    {
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return;
        }
        soWorkManageModule* work = m->m_workManageModule;
        const main::FighterMechanicsSettings& s = main::settings().fighterMechanics;
        bool mewtwoHoverActive = ftKind == kFtMewtwo && work->isFlag(kVarMewtwoHoverActive);
        bool lucasPreFirstAttack = ftKind == kFtLucas && work->getInt(kVarLucasAttackSerial) == 0;
        FighterPlan plan;
        buildFighterPlan(ftKind, status, mewtwoHoverActive, s, plan, lucasPreFirstAttack);
        executePlan(work, plan);
        if (ftKind == kFtDedede && fighter != NULL)
        {
            applyDededeWeights(fighter, s.dededeWaddle);
        }
        else if (ftKind == kFtOlimar)
        {
            applyOlimarWeights(s.olimarPikmin);
        }
    }

    void fighterMechanicsPrepareSwap()
    {
        fighterMechanicsDetach();
    }

    void fighterMechanicsDetach()
    {
        if (cacheResident(s_dedede, kFtDedede, kDededeDataTable, kDededeWeightsOffset))
        {
            const void* liveData = extraFtCommonData(kFtDedede);
            TM_LOG("fighter mech: detach Dedede Waddle weights %08x (put back %.1f/%.1f/%.1f/%.1f, live data %08x)\n",
                   s_dedede.weights, s_dedede.orig[0], s_dedede.orig[1], s_dedede.orig[2], s_dedede.orig[3], liveData);
            writeWeights(s_dedede.weights, s_dedede.orig, kDededeWeightCount);
        }
        if (cacheResident(s_olimar, kFtOlimar, kOlimarDataTable, kOlimarWeightsOffset))
        {
            const void* liveData = extraFtCommonData(kFtOlimar);
            TM_LOG("fighter mech: detach Olimar Pikmin weights %08x (put back %.1f/%.1f/%.1f/%.1f/%.1f, live data %08x)\n",
                   s_olimar.weights, s_olimar.orig[0], s_olimar.orig[1], s_olimar.orig[2], s_olimar.orig[3],
                   s_olimar.orig[4], liveData);
            writeWeights(s_olimar.weights, s_olimar.orig, kOlimarWeightCount);
        }
        clearCache(s_dedede);
        clearCache(s_olimar);
    }
}
}
