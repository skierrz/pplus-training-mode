#include "main/experiments/ray_probe.h"

#include <revolution/OS/OSTime.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/so_module_accesser.h>
#include <st/st_utility.h>

#include "main/game/fighters.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace experiments {
    // Only the DI Display (in the extra module) uses these and rayProbeRun:
    // kept from the linker's stripping.
#pragma force_active on
    RayProbe g_rayProbe;
    u32 g_rayCount;
#pragma force_active reset
    static bool s_logged = false;

    static void logEcb()
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
            if (m == NULL || !game::isRam(m->m_postureModule) || !game::isRam(m->m_groundModule))
            {
                continue;
            }
            const float* p = reinterpret_cast<const float*>(reinterpret_cast<u32>(m->m_postureModule) + 0xC);
            soGroundModule* g = m->m_groundModule;
            Vec2f d = g->getDownPos(0), u = g->getUpPos(0), l = g->getLeftPos(0), r = g->getRightPos(0),
                  c = g->getCenterPos(0);
            TM_LOG("ray: ecb P%d pos %.3f %.3f down %.3f %.3f up %.3f %.3f left %.3f %.3f right %.3f %.3f centre "
                   "%.3f %.3f\n",
                   refs[i].playerNo + 1, p[0], p[1], d.m_x - p[0], d.m_y - p[1], u.m_x - p[0], u.m_y - p[1],
                   l.m_x - p[0], l.m_y - p[1], r.m_x - p[0], r.m_y - p[1], c.m_x - p[0], c.m_y - p[1]);
        }
    }

#pragma force_active on
    void rayProbeRun()
    {
        if (!s_logged)
        {
            s_logged = true;
            TM_LOG("ray: probe at %08x\n", &g_rayProbe);
        }
        if (g_rayProbe.ecb & 1)
        {
            logEcb();
        }
        u32 n = g_rayProbe.n;
        if (n == 0 || n > 16)
        {
            return;
        }
        u32 start = OSGetTick();
        for (u32 i = 0; i < n; i++)
        {
            RayProbe::Ray& r = g_rayProbe.rays[i];
            Vec3f pos(r.pos[0], r.pos[1], r.pos[2]);
            Vec3f dir(r.dir[0], r.dir[1], r.dir[2]);
            Vec3f hit(0.0f, 0.0f, 0.0f);
            Vec3f normal(0.0f, 0.0f, 0.0f);
            int line = -1;
            if (r.kind == 1)
            {
                r.ret = stRayCheck(&pos, &dir, &line, &hit, &normal, r.b1 != 0, r.b2 != 0, r.mask);
            }
            else if (r.kind == 2)
            {
                r.ret = stRayCheck(&pos, &dir, r.b1 != 0, NULL, r.b2 != 0, r.mask);
            }
            else
            {
                r.ret = stRayCheck(&pos, &dir, &hit, &normal, r.b1 != 0, NULL, r.b2 != 0, r.mask);
            }
            r.line = line;
            r.hit[0] = hit.m_x;
            r.hit[1] = hit.m_y;
            r.hit[2] = hit.m_z;
            r.normal[0] = normal.m_x;
            r.normal[1] = normal.m_y;
            r.normal[2] = normal.m_z;
        }
        g_rayProbe.ticks = OSGetTick() - start;
        g_rayProbe.done++;
        g_rayProbe.n = 0;
    }
#pragma force_active reset
}
}
