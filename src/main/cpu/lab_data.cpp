#include "main/cpu/lab_data.h"

#include <string.h>

#include "main/cpu/actions.h"
#include "main/cpu/options.h"
#include "main/settings.h"

namespace tm {
namespace cpu {
    // ---- Advanced Counter Options -------------------------------------------

    static CpuTables& tables()
    {
        return main::settings().cpuTables;
    }

    static void defaultHit(AdvancedHit& h)
    {
        // Default per-hit counter config: Default logic, Jab (ground),
        // Airdodge (air), Grab (shield), no delay.
        h.logic = kLogicDefault;
        h.action[0] = 13;
        h.action[1] = 1;
        h.action[2] = 1;
        h.delay[0] = h.delay[1] = h.delay[2] = 0;
    }

    static int clampHit(int hit)
    {
        return hit < 1 ? 1 : (hit > kAdvancedHits ? kAdvancedHits : hit);
    }

    const AdvancedHit& advancedHit(int hit)
    {
        return tables().hits[clampHit(hit) - 1];
    }

    void poolTablesDefaults(CpuTables& t)
    {
        for (int list = 0; list < kListCount; list++)
        {
            poolDefaults(list, t.poolBits[list]);
        }
        t.customDiX = 100;
        t.customDiY = 0;
        t.poolReady = 1;
    }

    void advancedHitsDefaults(CpuTables& t)
    {
        for (int i = 0; i < kAdvancedHits; i++)
        {
            defaultHit(t.hits[i]);
        }
        t.ready = 1;
    }

    void tablesDefaults(CpuTables& t)
    {
        advancedHitsDefaults(t);
        poolTablesDefaults(t);
    }

    void advancedAttach()
    {
        CpuTables& t = tables();
        if (!t.ready)
        {
            tablesDefaults(t);
        }
        else if (!t.poolReady)
        {
            poolTablesDefaults(t);
        }
        CpuOptions& o = main::settings().cpu.options;
        o.advHit = 1;
        advancedHitChanged();
    }

    void advancedHitChanged()
    {
        CpuOptions& o = main::settings().cpu.options;
        const AdvancedHit& h = advancedHit(o.advHit);
        o.advLogic = h.logic;
        o.advGround = h.action[0];
        o.advAir = h.action[1];
        o.advShield = h.action[2];
        o.advDelayGround = h.delay[0];
        o.advDelayAir = h.delay[1];
        o.advDelayShield = h.delay[2];
    }

    void advancedValueChanged()
    {
        const CpuOptions& o = main::settings().cpu.options;
        AdvancedHit& h = tables().hits[clampHit(o.advHit) - 1];
        h.logic = o.advLogic;
        h.action[0] = o.advGround;
        h.action[1] = o.advAir;
        h.action[2] = o.advShield;
        h.delay[0] = o.advDelayGround;
        h.delay[1] = o.advDelayAir;
        h.delay[2] = o.advDelayShield;
    }
}
}
