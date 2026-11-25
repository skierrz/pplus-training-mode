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

        // Cached FitDedede.pac Waddle Toss weight pointer, tied to live
        // g_ftCommonDataAccesser data so fighter swaps drop stale pointers and
        // detach restores original bytes only while still resident.
        const void* s_dededeData = NULL;
        float* s_dededeWeights = NULL;
        float s_dededeOrig[kDededeWeightCount] = {0.0f, 0.0f, 0.0f, 0.0f};
        bool s_dededeTried = false;

        void clearDededeCache()
        {
            s_dededeData = NULL;
            s_dededeWeights = NULL;
            for (int i = 0; i < kDededeWeightCount; i++)
            {
                s_dededeOrig[i] = 0.0f;
            }
            s_dededeTried = false;
        }

        bool dededeResident()
        {
            if (s_dededeWeights == NULL || s_dededeData == NULL)
            {
                return false;
            }
            const void* data = g_ftCommonDataAccesser.getData(static_cast<ftKind>(kFtDedede));
            if (data != s_dededeData || !game::isRam(data))
            {
                return false;
            }
            u32 tableAddr = game::wordAt(data, kDededeDataTable);
            float* weights = reinterpret_cast<float*>(tableAddr + kDededeWeightsOffset);
            return game::isRam(reinterpret_cast<const void*>(tableAddr)) && weights == s_dededeWeights
                   && game::isRam(s_dededeWeights);
        }

        void syncDededeCache()
        {
            const void* data = g_ftCommonDataAccesser.getData(static_cast<ftKind>(kFtDedede));
            if (data == NULL || data != s_dededeData)
            {
                clearDededeCache();
            }
        }

        void findDededeWeights(Fighter* fighter)
        {
            s_dededeTried = true;
            const void* data = g_ftCommonDataAccesser.getData(static_cast<ftKind>(kFtDedede));
            if (!game::isRam(data))
            {
                s_dededeData = NULL;
                TM_LOG("fighter mech: Dedede fighter data not found: Waddle Toss left alone\n");
                return;
            }
            s_dededeData = data;
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
            float cDee = soValueAccesser::getConstantFloat(fighter->m_moduleAccesser, kDededeParamDee, 0);
            float cDoo = soValueAccesser::getConstantFloat(fighter->m_moduleAccesser, kDededeParamDoo, 0);
            float cGordo = soValueAccesser::getConstantFloat(fighter->m_moduleAccesser, kDededeParamGordo, 0);
            bool ok = weights[0] == 35.0f && weights[1] == 10.0f && weights[2] == 4.0f && weights[3] == 0.0f
                      && cDee == weights[0] && cDoo == weights[1] && cGordo == weights[2];
            TM_LOG("fighter mech: Dedede Waddle weights at %08x (%.1f/%.1f/%.1f/%.1f, const %.1f/%.1f/%.1f): %s\n",
                   weights, weights[0], weights[1], weights[2], weights[3], cDee, cDoo, cGordo,
                   ok ? "ok" : "not as expected: left alone");
            if (ok)
            {
                s_dededeWeights = weights;
                for (int i = 0; i < kDededeWeightCount; i++)
                {
                    s_dededeOrig[i] = weights[i];
                }
            }
        }

        void applyDededeWeights(Fighter* fighter, u8 choice)
        {
            syncDededeCache();
            if (!s_dededeTried)
            {
                findDededeWeights(fighter);
            }
            if (!dededeResident())
            {
                return;
            }
            float want[kDededeWeightCount];
            bool clearCd = false;
            dededeWaddleWeights(choice, s_dededeOrig, want, clearCd);
            for (int i = 0; i < kDededeWeightCount; i++)
            {
                if (s_dededeWeights[i] != want[i])
                {
                    s_dededeWeights[i] = want[i];
                }
            }
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
        clearDededeCache();
    }

    void fighterMechanicsSync()
    {
        syncDededeCache();
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
    }

    void fighterMechanicsPrepareSwap()
    {
        fighterMechanicsDetach();
    }

    void fighterMechanicsDetach()
    {
        if (dededeResident())
        {
            const void* liveData = g_ftCommonDataAccesser.getData(static_cast<ftKind>(kFtDedede));
            TM_LOG("fighter mech: detach Dedede Waddle weights %08x (put back %.1f/%.1f/%.1f/%.1f, live data %08x)\n",
                   s_dededeWeights, s_dededeOrig[0], s_dededeOrig[1], s_dededeOrig[2], s_dededeOrig[3], liveData);
            for (int i = 0; i < kDededeWeightCount; i++)
            {
                s_dededeWeights[i] = s_dededeOrig[i];
            }
        }
        clearDededeCache();
    }
}
}
