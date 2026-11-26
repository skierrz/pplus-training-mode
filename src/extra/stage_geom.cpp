#include "extra/stage_geom.h"

#include <ft/fighter.h>
#include <so/controller/so_controller_module_impl.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/ground/so_ground_shape_impl.h>
#include <so/kinetic/so_kinetic_module_impl.h>
#include <so/posture/so_posture_module_impl.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <so/status/so_status_module_impl.h>
#include <so/stop/so_stop_module_impl.h>
#include <so/work/so_work_manage_module_impl.h>
#include <st/st_utility.h>
#include <st/stage.h>
#include <string.h>

#include "extra/net_alloc.h"
#include "main/cpu/view.h"
#include "main/display/display.h"
#include "main/game/status_ids.h"
#include "main/game/tracker.h"
#include "shared/game/mem.h"

namespace tm {
namespace extra {
    enum {
        kRayFighterMask = 1,
        kCacheFrames = 15,
        kHitstunLaBasic = 0x10000038 // LA-Basic[0x38] (fighters.cpp)
    };

    static StageGeometry* s_cache = NULL;
    static s32 s_cacheFrame = -1000;
    static bool s_cacheValid = false;
    static bool s_cacheHasPlatforms = false;

    void stageGeomAttach()
    {
        freeNetPtr(s_cache);
        s_cache = allocNetArray<StageGeometry>(1);
        stageGeomReset();
    }

    void stageGeomDetach()
    {
        stageGeomReset();
        freeNetPtr(s_cache);
    }

    void stageGeomReset()
    {
        s_cacheValid = false;
        s_cacheHasPlatforms = false;
        s_cacheFrame = -1000;
    }

    bool readBlastZones(BlastZones& out)
    {
        static const BlastZones kDef = {-220.0f, 220.0f, 180.0f, -140.0f, false};
        memcpy(&out, &kDef, sizeof(out));

        // Primary: CameraController + 0x158 (verified in cpu/options.cpp:790
        // and extra/di_display.cpp:730).
        void* camera = display::cameraController();
        if (camera != NULL)
        {
            const float* z = reinterpret_cast<const float*>(reinterpret_cast<u32>(camera) + 0x158);
            if (z[0] < z[1] && z[3] < z[2] && z[0] > -2000.0f && z[1] < 2000.0f && z[3] > -2000.0f && z[2] < 2000.0f)
            {
                out.left = z[0];
                out.right = z[1];
                out.top = z[2];
                out.bottom = z[3];
                out.valid = true;
                return true;
            }
        }
        // Fallback: Stage::m_deadRange (BrawlHeaders st/stage.h:123).
        Stage* stage = g_Stage;
        if (game::isRam(stage))
        {
            const Rect2D& r = stage->m_deadRange;
            if (r.m_left < r.m_right && r.m_down < r.m_up)
            {
                out.left = r.m_left;
                out.right = r.m_right;
                out.top = r.m_up;
                out.bottom = r.m_down;
                out.valid = true;
                return true;
            }
        }
        return false;
    }

#pragma dont_inline on
    static bool rayCast(float x, float y, float dx, float dy, bool passThrough, Vec3f& hit, Vec3f& normal)
    {
        Vec3f start(x, y, 0.0f);
        Vec3f dir(dx, dy, 0.0f);
        hit.m_x = hit.m_y = hit.m_z = 0.0f;
        normal.m_x = normal.m_y = normal.m_z = 0.0f;
        return stRayCheck(&start, &dir, &hit, &normal, passThrough, NULL, false, kRayFighterMask);
    }
#pragma dont_inline reset

    bool rayCheckGround(float x, float y, float maxDist, float& hitY, bool passThrough)
    {
        Vec3f hit, normal;
        if (maxDist <= 0.0f || !rayCast(x, y, 0.0f, -maxDist, passThrough, hit, normal) || normal.m_y < 0.5f)
        {
            return false;
        }
        hitY = hit.m_y;
        return true;
    }

    bool rayCheckCeiling(float x, float y, float dist)
    {
        Vec3f hit, normal;
        return dist > 0.0f && rayCast(x, y, 0.0f, dist, false, hit, normal) && normal.m_y < 0.5f;
    }

    bool rayCheckWall(float x, float y, float dirX, float dist, float* hitDist)
    {
        Vec3f hit, normal;
        if (dist <= 0.0f || dirX == 0.0f
            || !rayCast(x, y, (dirX > 0.0f ? dist : -dist), 0.0f, false, hit, normal))
        {
            return false;
        }
        float absNx = normal.m_x < 0.0f ? -normal.m_x : normal.m_x;
        if (absNx < 0.5f)
        {
            return false;
        }
        if (hitDist != NULL)
        {
            float d = hit.m_x - x;
            *hitDist = d < 0.0f ? -d : d;
        }
        return true;
    }

    static bool rayCheckPassThroughOnly(float x, float y, float maxDist, float& hitY)
    {
        float py = 0.0f;
        if (!rayCheckGround(x, y, maxDist, py, true))
        {
            return false;
        }
        float sy = 0.0f;
        if (rayCheckGround(x, py + 1.5f, 3.0f, sy, false) && (sy - py) > -2.5f && (sy - py) < 2.5f)
        {
            return false;
        }
        hitY = py;
        return true;
    }

#pragma dont_inline on
    static bool probeEdge(bool isPlat, float x, float refY, float& outY)
    {
        float hy = 0.0f;
        if (isPlat)
        {
            return rayCheckPassThroughOnly(x, refY + 3.0f, 6.0f, hy) && (hy - refY) > -2.0f && (hy - refY) < 2.0f;
        }
        if (!rayCheckGround(x, refY + 20.0f, 60.0f, hy, false))
        {
            return false;
        }
        outY = hy;
        return true;
    }

    static void findEdge(bool isPlat, float inX, float inY, float stepSign, float limitX, float& edgeX, float& edgeY)
    {
        float coarse = isPlat ? 4.0f : 8.0f;
        int maxSteps = isPlat ? 32 : 40;
        int refineIters = isPlat ? 5 : 6;
        float lastX = inX;
        float lastY = inY;
        float curX = inX + stepSign * coarse;
        bool left = false;
        for (int i = 0; i < maxSteps; i++)
        {
            if ((stepSign < 0.0f && curX < limitX) || (stepSign > 0.0f && curX > limitX))
            {
                break;
            }
            if (probeEdge(isPlat, curX, lastY, lastY))
            {
                lastX = curX;
                curX += stepSign * coarse;
            }
            else
            {
                left = true;
                break;
            }
        }
        if (left)
        {
            float lo = lastX;
            float hi = curX;
            for (int iter = 0; iter < refineIters; iter++)
            {
                float mid = 0.5f * (lo + hi);
                if (probeEdge(isPlat, mid, lastY, lastY))
                {
                    lo = mid;
                }
                else
                {
                    hi = mid;
                }
            }
            lastX = lo;
        }
        edgeX = lastX;
        edgeY = lastY;
    }
#pragma dont_inline reset

    static float findPlatformEdge(float startX, float platY, float stepSign, float limitX)
    {
        float ex = startX, ey = platY;
        findEdge(true, startX, platY, stepSign, limitX, ex, ey);
        return ex;
    }

#pragma dont_inline on
    static bool platformOverlaps(const StageGeometry& g, float y, float xLo, float xHi, float pad)
    {
        for (int k = 0; k < g.platformCount; k++)
        {
            const StagePlatform& e = g.platforms[k];
            float dy = e.y - y;
            if (dy > -3.0f && dy < 3.0f && xLo <= e.rightX + pad && xHi >= e.leftX - pad)
            {
                return true;
            }
        }
        return false;
    }
#pragma dont_inline reset

    static void probePassThroughPlatforms(StageGeometry& g)
    {
        g.platformCount = 0;
        if (!g.floor.valid)
        {
            return;
        }
        float centerX = 0.5f * (g.floor.leftX + g.floor.rightX);
        float halfW = 0.5f * (g.floor.rightX - g.floor.leftX);
        static const float kFracs[] = {-0.55f, 0.55f, -0.35f, 0.35f, 0.0f, -0.75f, 0.75f};
        float limitL = g.blast.valid ? geomMax(g.blast.left + 12.0f, g.floor.leftX - 30.0f) : (g.floor.leftX - 30.0f);
        float limitR = g.blast.valid ? geomMin(g.blast.right - 12.0f, g.floor.rightX + 30.0f) : (g.floor.rightX + 30.0f);

        for (int i = 0; i < 7 && g.platformCount < kMaxStagePlatforms; i++)
        {
            float sx = centerX + kFracs[i] * halfW;
            float solidY = g.floor.centerY;
            rayCheckGround(sx, g.floor.centerY + 25.0f, 55.0f, solidY, false);
            float probeTop = g.blast.valid ? geomMin(g.blast.top - 15.0f, solidY + 85.0f) : (solidY + 85.0f);
            float minY = solidY + 5.0f;
            float curTop = probeTop;
            for (int layer = 0; layer < 3 && curTop > minY + 1.0f && g.platformCount < kMaxStagePlatforms; layer++)
            {
                float hitY = 0.0f;
                if (!rayCheckPassThroughOnly(sx, curTop, curTop - minY, hitY) || hitY <= minY)
                {
                    break;
                }
                if (!platformOverlaps(g, hitY, sx, sx, 2.0f))
                {
                    float plx = findPlatformEdge(sx, hitY, -1.0f, limitL);
                    float prx = findPlatformEdge(sx, hitY, 1.0f, limitR);
                    if (prx > plx + 6.0f && !platformOverlaps(g, hitY, plx, prx, 4.0f))
                    {
                        StagePlatform& p = g.platforms[g.platformCount++];
                        p.leftX = plx;
                        p.rightX = prx;
                        p.y = hitY;
                        p.valid = true;
                    }
                }
                curTop = hitY - 2.0f;
            }
        }
    }

    bool queryStageGeometry(StageGeometry& out, bool forceRefresh, bool includePlatforms)
    {
        StageGeometry* cache = s_cache;
        s32 now = game::trackerFrame();
        if (cache != NULL && !forceRefresh && s_cacheValid && now >= s_cacheFrame && now - s_cacheFrame < kCacheFrames)
        {
            if (includePlatforms && !s_cacheHasPlatforms)
            {
                probePassThroughPlatforms(*cache);
                s_cacheHasPlatforms = true;
            }
            memcpy(&out, cache, sizeof(out));
            return true;
        }

        StageGeometry g;
        memset(&g, 0, sizeof(g));
        g.floor.leftX = -68.0f;
        g.floor.rightX = 68.0f;

        readBlastZones(g.blast);

        // Probe downward for the solid main stage floor near x = 0.
        static const float kProbes[] = {0.0f, -20.0f, 20.0f, -40.0f, 40.0f};
        float startX = 0.0f;
        float startY = 0.0f;
        bool foundCenter = false;
        float probeTop = g.blast.valid ? geomMin(g.blast.top - 10.0f, 120.0f) : 120.0f;
        float probeSpan = g.blast.valid ? (probeTop - g.blast.bottom) : 260.0f;
        for (int i = 0; i < 5; i++)
        {
            float hy = 0.0f;
            if (rayCheckGround(kProbes[i], probeTop, probeSpan, hy, false))
            {
                startX = kProbes[i];
                startY = hy;
                foundCenter = true;
                break;
            }
        }
        if (foundCenter)
        {
            float lx = startX, ly = startY;
            float rx = startX, ry = startY;
            findEdge(false, startX, startY, -1.0f, g.blast.left + 5.0f, lx, ly);
            findEdge(false, startX, startY, 1.0f, g.blast.right - 5.0f, rx, ry);
            g.floor.leftX = lx;
            g.floor.rightX = rx;
            g.floor.leftY = ly;
            g.floor.rightY = ry;
            g.floor.centerY = startY;
            g.floor.valid = rx > lx + 4.0f;
            if (g.floor.valid)
            {
                g.leftLedge.x = lx;
                g.leftLedge.y = ly;
                g.leftLedge.dir = 1.0f;
                g.rightLedge.x = rx;
                g.rightLedge.y = ry;
                g.rightLedge.dir = -1.0f;
                g.hasLedges = true;
                if (includePlatforms)
                {
                    probePassThroughPlatforms(g);
                }
            }
        }

        if (cache != NULL)
        {
            memcpy(cache, &g, sizeof(g));
            s_cacheFrame = now;
            s_cacheValid = g.floor.valid;
            s_cacheHasPlatforms = g.floor.valid && includePlatforms;
        }
        memcpy(&out, &g, sizeof(out));
        return g.floor.valid;
    }

    bool findSafeRandomPoint(const StageGeometry& g, u32& rng, bool onGround, float& outX, float& outY)
    {
        for (int attempt = 0; attempt < 16; attempt++)
        {
            float cx = 0.0f, cy = 0.0f, hitY = 0.0f;
            if (!sampleSafeCandidate(g, rng, onGround, cx, cy))
            {
                break;
            }
            bool hasGround = rayCheckGround(cx, cy, 80.0f, hitY, true);
            if (onGround)
            {
                if (!hasGround) continue;
                cy = hitY;
            }
            else if (hasGround && cy < hitY + 4.0f)
            {
                cy = hitY + 8.0f;
            }
            if (!rayCheckCeiling(cx, cy + 4.0f, 150.0f))
            {
                outX = cx;
                outY = cy;
                return true;
            }
        }
        if (g.floor.valid)
        {
            outX = 0.5f * (g.floor.leftX + g.floor.rightX);
            outY = g.floor.centerY + (onGround ? 0.0f : 15.0f);
            return true;
        }
        return false;
    }

    static void setCorrectSingle(Fighter* f, int correctKind)
    {
        const soModuleEnumeration* m = game::fighterModules(f);
        if (m == NULL || !game::isRam(m->m_groundModule))
        {
            return;
        }
        m->m_groundModule->setCorrect(static_cast<soGroundShapeImpl::CorrectKind>(correctKind), 0);
    }

    void setFighterCorrect(const game::FighterRef& ref, int correctKind)
    {
        setCorrectSingle(ref.fighter, correctKind);
        if (ref.partner != NULL && ref.partner != ref.fighter)
        {
            setCorrectSingle(ref.partner, correctKind);
        }
    }

    int getFighterCorrect(Fighter* f)
    {
        const soModuleEnumeration* m = game::fighterModules(f);
        if (m == NULL || !game::isRam(m->m_groundModule))
        {
            return -1;
        }
        return static_cast<int>(m->m_groundModule->getCorrect(0));
    }

    static void setFacingSingle(Fighter* f, float lr)
    {
        const soModuleEnumeration* m = game::fighterModules(f);
        if (m == NULL || !game::isRam(m->m_postureModule))
        {
            return;
        }
        m->m_postureModule->setLr(lr >= 0.0f ? 1.0f : -1.0f);
        m->m_postureModule->updateRotYLr();
    }

#pragma dont_inline on
    static void warpSingle(Fighter* f, float x, float y, bool falling, bool setNoclip, float lr)
    {
        const soModuleEnumeration* m = game::fighterModules(f);
        if (m == NULL)
        {
            return;
        }
        if (game::isRam(m->m_stopModule) && m->m_stopModule->getHitStopRealFrame() > 0)
        {
            m->m_stopModule->cancelHitStop();
        }
        if (game::isRam(m->m_workManageModule))
        {
            m->m_workManageModule->setInt(0, kHitstunLaBasic);
        }
        float useLr = lr;
        if (useLr == 0.0f && game::isRam(m->m_postureModule))
        {
            float cur = *reinterpret_cast<const float*>(reinterpret_cast<u32>(m->m_postureModule) + 0x40);
            useLr = cur < 0.0f ? -1.0f : 1.0f;
        }
        if (useLr == 0.0f)
        {
            useLr = 1.0f;
        }
        soGroundModule* gm = game::isRam(m->m_groundModule) ? m->m_groundModule : NULL;
        Vec3f pos(x, y, 0.0f);
        // Fighter::warp atomically detaches any cliff/ground attachment and
        // resets grCollStatus to the new coordinates for both airborne and
        // grounded warps so Cliff_Wait never snaps back.
        f->warp(&pos, useLr, 0);
        if (!falling && gm != NULL && gm->attachGround(0))
        {
            gm->setStGroundForce(0);
        }
        if (game::isRam(m->m_statusModule) && game::isRam(f->m_moduleAccesser))
        {
            m->m_statusModule->changeStatusForce(falling ? game::kStatusFall : game::kStatusWait, f->m_moduleAccesser);
        }
        if (game::isRam(m->m_kineticModule))
        {
            m->m_kineticModule->clearSpeedAll();
        }
        cpu::clearKnockback(f);
        if (game::isRam(m->m_controllerModule))
        {
            m->m_controllerModule->resetButton();
            m->m_controllerModule->resetTrigger();
            m->m_controllerModule->resetMainStick();
            m->m_controllerModule->resetSubStick();
        }
        if (falling)
        {
            cpu::placeAt(f, x, y);
            setFacingSingle(f, useLr);
        }
        if (setNoclip && gm != NULL)
        {
            gm->setCorrect(soGroundShapeImpl::Correct_None, 0);
        }
    }
#pragma dont_inline reset

    namespace {
        enum {
            kLaBasicBase = 0x10000000,
            kLaFloatBase = 0x11000000,
            kLaBitBase = 0x12000000,
            kIcJumpCountMax = 23003,
            kAirRuleBit = 0,
            kAirRuleInt = 1,
            kAirRuleFloatZero = 2
        };

        // Packed 16-bit once-per-airtime reset entry:
        // bits 15..10: ftKind (0x3F = common), bits 9..8: type, bit 7: val (0/1), bits 6..0: idx.
#define AR(ft, ty, ix, v) static_cast<u16>((((ft) & 0x3F) << 10) | (((ty) & 3) << 8) | (((v) & 1) << 7) | ((ix) & 0x7F))
        static const u16 kAirRules[] = {
            // Common airborne resets (all fighters)
            AR(0x3F, kAirRuleInt, 2, 0), AR(0x3F, kAirRuleInt, 3, 0), AR(0x3F, kAirRuleInt, 4, 0),
            AR(0x3F, kAirRuleInt, 11, 0), AR(0x3F, kAirRuleInt, 12, 0), AR(0x3F, kAirRuleInt, 62, 0),
            AR(0x3F, kAirRuleInt, 79, 0), AR(0x3F, kAirRuleInt, 81, 0), AR(0x3F, kAirRuleBit, 22, 0),
            AR(0x3F, kAirRuleBit, 57, 0), AR(0x3F, kAirRuleFloatZero, 0, 0),
            // Per-fighter once-per-airtime resets
            AR(0x00, kAirRuleBit, 62, 0), AR(0x00, kAirRuleBit, 113, 0), AR(0x00, kAirRuleBit, 116, 0), AR(0x00, kAirRuleBit, 118, 0),
            AR(0x01, kAirRuleBit, 70, 0),
            AR(0x03, kAirRuleInt, 67, 0), AR(0x03, kAirRuleBit, 120, 0),
            AR(0x04, kAirRuleBit, 112, 0), AR(0x04, kAirRuleBit, 113, 0),
            AR(0x05, kAirRuleBit, 112, 0), AR(0x05, kAirRuleBit, 114, 0), AR(0x05, kAirRuleBit, 119, 0),
            AR(0x08, kAirRuleBit, 61, 0), AR(0x08, kAirRuleBit, 115, 0), AR(0x08, kAirRuleBit, 116, 0),
            AR(0x0A, kAirRuleBit, 66, 0), AR(0x0A, kAirRuleBit, 112, 1),
            AR(0x0B, kAirRuleBit, 119, 0),
            AR(0x0C, kAirRuleBit, 62, 0),
            AR(0x0D, kAirRuleBit, 126, 0), AR(0x0D, kAirRuleBit, 127, 0),
            AR(0x0E, kAirRuleBit, 114, 1),
            AR(0x11, kAirRuleBit, 115, 1),
            AR(0x12, kAirRuleBit, 114, 0),
            AR(0x14, kAirRuleBit, 115, 0), AR(0x14, kAirRuleBit, 120, 1), AR(0x14, kAirRuleInt, 98, 0),
            AR(0x15, kAirRuleBit, 118, 0), AR(0x15, kAirRuleBit, 122, 0),
            AR(0x16, kAirRuleBit, 61, 0), AR(0x16, kAirRuleBit, 115, 0), AR(0x16, kAirRuleBit, 116, 0),
            AR(0x17, kAirRuleBit, 113, 0), AR(0x17, kAirRuleBit, 114, 1), AR(0x17, kAirRuleBit, 125, 0),
            AR(0x18, kAirRuleBit, 113, 0),
            AR(0x19, kAirRuleBit, 122, 1),
            AR(0x1A, kAirRuleBit, 122, 1),
            AR(0x1D, kAirRuleBit, 113, 0), AR(0x1D, kAirRuleInt, 92, 0),
            AR(0x1E, kAirRuleBit, 115, 0),
            AR(0x1F, kAirRuleInt, 75, 0),
            AR(0x21, kAirRuleBit, 66, 0), AR(0x21, kAirRuleBit, 69, 0),
            AR(0x22, kAirRuleInt, 91, 1),
            AR(0x23, kAirRuleBit, 111, 0), AR(0x23, kAirRuleBit, 114, 0),
            AR(0x26, kAirRuleBit, 114, 0), AR(0x26, kAirRuleBit, 115, 0), AR(0x26, kAirRuleBit, 117, 0), AR(0x26, kAirRuleBit, 118, 0),
            AR(0x26, kAirRuleInt, 98, 0),
            AR(0x27, kAirRuleBit, 115, 1),
            AR(0x2D, kAirRuleBit, 61, 0), AR(0x2D, kAirRuleBit, 113, 0), AR(0x2D, kAirRuleBit, 122, 0),
            AR(0x2D, kAirRuleInt, 91, 0), AR(0x2D, kAirRuleInt, 92, 0), AR(0x2D, kAirRuleInt, 93, 0),
            AR(0x2E, kAirRuleBit, 61, 0), AR(0x2E, kAirRuleBit, 113, 0), AR(0x2E, kAirRuleFloatZero, 52, 0),
            AR(0x2F, kAirRuleBit, 61, 0), AR(0x2F, kAirRuleBit, 112, 0), AR(0x2F, kAirRuleBit, 113, 0),
            AR(0x2F, kAirRuleBit, 114, 0), AR(0x2F, kAirRuleInt, 91, 0)
        };
#undef AR

#pragma dont_inline on
        void refreshAirSingle(Fighter* f, int ftKind, bool grounded, bool spendDoubleJump)
        {
            const soModuleEnumeration* m = game::fighterModules(f);
            if (m == NULL || !game::isRam(m->m_workManageModule))
            {
                return;
            }
            soWorkManageModule* w = m->m_workManageModule;
            soModuleAccesser* acc = f->m_moduleAccesser;
            bool accOk = game::isRam(acc);
            int maxJumps = 2;
            if (accOk)
            {
                int v = soValueAccesser::getConstantInt(acc, kIcJumpCountMax, 0);
                if (v >= 2 && v <= 10) maxJumps = v;
            }
            w->setInt(grounded ? 0 : (spendDoubleJump ? maxJumps : 1), kLaBasicBase + 1);
            if (game::isRam(m->m_animCmdModule) && !m->m_animCmdModule->isNull() && accOk)
            {
                const u32* vt = *reinterpret_cast<const u32* const*>(m->m_animCmdModule);
                if (game::isRam(vt))
                {
                    typedef void (*ResetInterpFn)(void*, int, const void*, soModuleAccesser*);
                    ResetInterpFn fn = reinterpret_cast<ResetInterpFn>(vt[0x24 / 4]);
                    if (game::isRam(reinterpret_cast<const void*>(fn)))
                    {
                        fn(m->m_animCmdModule, soAnimCmdControlUnitKind::InfiniteLoop, NULL, acc);
                    }
                }
            }
            if (!grounded && game::isRam(m->m_statusModule))
            {
                m->m_statusModule->enableTransitionTermAll(game::kGroupAirSpecial);
                m->m_statusModule->enableTransitionTermAll(game::kGroupAirEscape);
            }
            if (ftKind == 0x23) w->setInt(3, kLaBasicBase + 75); // R.O.B. 3 boosters
            const u32 count = static_cast<u32>(sizeof(kAirRules) / sizeof(kAirRules[0]));
            for (u32 i = 0; i < count; i++)
            {
                u16 e = kAirRules[i];
                u32 rf = (e >> 10) & 0x3Fu;
                if (rf != 0x3Fu && static_cast<int>(rf) != ftKind) continue;
                u32 ty = (e >> 8) & 3u;
                u32 val = (e >> 7) & 1u;
                u32 idx = e & 0x7Fu;
                if (ty == kAirRuleBit) { if (val != 0) w->onFlag(kLaBitBase + idx); else w->offFlag(kLaBitBase + idx); }
                else if (ty == kAirRuleInt) w->setInt(static_cast<int>(val), kLaBasicBase + idx);
                else w->setFloat(0.0f, kLaFloatBase + idx);
            }
        }
#pragma dont_inline reset
    }

    void refreshAirResources(const game::FighterRef& ref, bool spendDoubleJump)
    {
        if (ref.fighter != NULL)
        {
            refreshAirSingle(ref.fighter, game::fighterKind(ref.entryId, false), false, spendDoubleJump);
        }
        if (ref.partner != NULL && ref.partner != ref.fighter)
        {
            refreshAirSingle(ref.partner, game::fighterKind(ref.entryId, true), false, spendDoubleJump);
        }
    }

#pragma dont_inline on
    static void placePair(const game::FighterRef& ref, float x, float y, bool falling, bool setNoclip, float damage,
                          float lr)
    {
        for (int i = 0; i < 2; i++)
        {
            Fighter* f = i == 0 ? ref.fighter : ((ref.partner != NULL && ref.partner != ref.fighter) ? ref.partner : NULL);
            if (f == NULL)
            {
                continue;
            }
            warpSingle(f, x, y, falling, setNoclip, lr);
            if (!falling)
            {
                refreshAirSingle(f, game::fighterKind(ref.entryId, i != 0), true, false);
            }
        }
        if (damage >= 0.0f)
        {
            game::setDamage(ref, damage);
        }
    }
#pragma dont_inline reset

    void placeStanding(const game::FighterRef& ref, float x, float y, bool setNoclip, float damage, float lr)
    {
        placePair(ref, x, y, false, setNoclip, damage, lr);
    }

    void placeFalling(const game::FighterRef& ref, float x, float y, bool setNoclip, float damage, float lr)
    {
        placePair(ref, x, y, true, setNoclip, damage, lr);
    }

    void setFighterFacing(const game::FighterRef& ref, float lr)
    {
        setFacingSingle(ref.fighter, lr);
        if (ref.partner != NULL && ref.partner != ref.fighter)
        {
            setFacingSingle(ref.partner, lr);
        }
    }
}
}
