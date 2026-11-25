#include "extra/stage_geom.h"

#include <ft/fighter.h>
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

    static StageGeometry s_cache;
    static s32 s_cacheFrame = -1000;
    static bool s_cacheValid = false;
    static bool s_cacheHasPlatforms = false;

    void stageGeomAttach()
    {
        stageGeomReset();
    }

    void stageGeomReset()
    {
        s_cacheValid = false;
        s_cacheHasPlatforms = false;
        s_cacheFrame = -1000;
    }

    bool readBlastZones(BlastZones& out)
    {
        out.left = -220.0f;
        out.right = 220.0f;
        out.top = 180.0f;
        out.bottom = -140.0f;
        out.valid = false;

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

    bool rayCheckGround(float x, float y, float maxDist, float& hitY, bool passThrough)
    {
        if (maxDist <= 0.0f)
        {
            return false;
        }
        Vec3f start(x, y, 0.0f);
        Vec3f dir(0.0f, -maxDist, 0.0f);
        Vec3f hit(0.0f, 0.0f, 0.0f);
        Vec3f normal(0.0f, 0.0f, 0.0f);
        if (!stRayCheck(&start, &dir, &hit, &normal, passThrough, NULL, false, kRayFighterMask))
        {
            return false;
        }
        if (normal.m_y < 0.5f)
        {
            return false;
        }
        hitY = hit.m_y;
        return true;
    }

    bool rayCheckCeiling(float x, float y, float dist)
    {
        if (dist <= 0.0f)
        {
            return false;
        }
        Vec3f start(x, y, 0.0f);
        Vec3f dir(0.0f, dist, 0.0f);
        Vec3f hit(0.0f, 0.0f, 0.0f);
        Vec3f normal(0.0f, 0.0f, 0.0f);
        if (!stRayCheck(&start, &dir, &hit, &normal, false, NULL, false, kRayFighterMask))
        {
            return false;
        }
        return normal.m_y < 0.5f;
    }

    bool rayCheckWall(float x, float y, float dirX, float dist, float* hitDist)
    {
        if (dist <= 0.0f || dirX == 0.0f)
        {
            return false;
        }
        float sign = dirX > 0.0f ? 1.0f : -1.0f;
        Vec3f start(x, y, 0.0f);
        Vec3f dir(sign * dist, 0.0f, 0.0f);
        Vec3f hit(0.0f, 0.0f, 0.0f);
        Vec3f normal(0.0f, 0.0f, 0.0f);
        if (!stRayCheck(&start, &dir, &hit, &normal, false, NULL, false, kRayFighterMask))
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

    // Finds one solid floor edge starting from an on-floor x (`inX`, `inY`)
    // stepping outward along `stepSign` (-1 left, +1 right), then refining
    // with 6 binary-search steps (~0.125 unit accuracy).
    static void findFloorEdge(float inX, float inY, float stepSign, float limitX, float& edgeX, float& edgeY)
    {
        const float kCoarse = 8.0f;
        const float kProbeLift = 20.0f;
        const float kProbeDepth = 60.0f;
        float lastX = inX;
        float lastY = inY;
        float curX = inX + stepSign * kCoarse;
        bool leftStage = false;
        for (int i = 0; i < 40; i++)
        {
            if ((stepSign < 0.0f && curX < limitX) || (stepSign > 0.0f && curX > limitX))
            {
                break;
            }
            float hy = 0.0f;
            if (rayCheckGround(curX, lastY + kProbeLift, kProbeDepth, hy, false))
            {
                lastX = curX;
                lastY = hy;
                curX += stepSign * kCoarse;
            }
            else
            {
                leftStage = true;
                break;
            }
        }
        if (leftStage)
        {
            float lo = lastX;
            float hi = curX;
            for (int iter = 0; iter < 6; iter++)
            {
                float mid = 0.5f * (lo + hi);
                float hy = 0.0f;
                if (rayCheckGround(mid, lastY + kProbeLift, kProbeDepth, hy, false))
                {
                    lo = mid;
                    lastY = hy;
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

    static float findPlatformEdge(float startX, float platY, float stepSign, float limitX)
    {
        const float kCoarse = 4.0f;
        float lastX = startX;
        float curX = startX + stepSign * kCoarse;
        bool leftPlat = false;
        for (int i = 0; i < 32; i++)
        {
            if ((stepSign < 0.0f && curX < limitX) || (stepSign > 0.0f && curX > limitX))
            {
                break;
            }
            float hy = 0.0f;
            if (rayCheckPassThroughOnly(curX, platY + 3.0f, 6.0f, hy) && (hy - platY) > -2.0f && (hy - platY) < 2.0f)
            {
                lastX = curX;
                curX += stepSign * kCoarse;
            }
            else
            {
                leftPlat = true;
                break;
            }
        }
        if (leftPlat)
        {
            float lo = lastX;
            float hi = curX;
            for (int iter = 0; iter < 5; iter++)
            {
                float mid = 0.5f * (lo + hi);
                float hy = 0.0f;
                if (rayCheckPassThroughOnly(mid, platY + 3.0f, 6.0f, hy) && (hy - platY) > -2.0f && (hy - platY) < 2.0f)
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
        return lastX;
    }

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
                bool covered = false;
                for (int k = 0; k < g.platformCount; k++)
                {
                    const StagePlatform& e = g.platforms[k];
                    float dy = e.y - hitY;
                    if (dy > -3.0f && dy < 3.0f && sx >= e.leftX - 2.0f && sx <= e.rightX + 2.0f)
                    {
                        covered = true;
                        break;
                    }
                }
                if (covered)
                {
                    curTop = hitY - 2.0f;
                    continue;
                }
                float plx = findPlatformEdge(sx, hitY, -1.0f, limitL);
                float prx = findPlatformEdge(sx, hitY, 1.0f, limitR);
                if (prx > plx + 6.0f)
                {
                    bool dup = false;
                    for (int k = 0; k < g.platformCount; k++)
                    {
                        const StagePlatform& e = g.platforms[k];
                        float dy = e.y - hitY;
                        if (dy > -3.0f && dy < 3.0f && plx <= e.rightX + 4.0f && prx >= e.leftX - 4.0f)
                        {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup)
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
        s32 now = game::trackerFrame();
        if (!forceRefresh && s_cacheValid && now >= s_cacheFrame && now - s_cacheFrame < kCacheFrames)
        {
            if (includePlatforms && !s_cacheHasPlatforms)
            {
                probePassThroughPlatforms(s_cache);
                s_cacheHasPlatforms = true;
            }
            out = s_cache;
            return true;
        }

        StageGeometry g;
        memset(&g, 0, sizeof(g));
        g.floor.leftX = -68.0f;
        g.floor.rightX = 68.0f;
        g.floor.leftY = 0.0f;
        g.floor.rightY = 0.0f;
        g.floor.centerY = 0.0f;
        g.floor.valid = false;
        g.hasLedges = false;

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
            findFloorEdge(startX, startY, -1.0f, g.blast.left + 5.0f, lx, ly);
            findFloorEdge(startX, startY, 1.0f, g.blast.right - 5.0f, rx, ry);
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

        s_cache = g;
        s_cacheFrame = now;
        s_cacheValid = g.floor.valid;
        s_cacheHasPlatforms = g.floor.valid && includePlatforms;
        out = g;
        return g.floor.valid;
    }

    bool liveGeometry(StageGeometry& out, bool forceRefresh, bool includePlatforms)
    {
        return queryStageGeometry(out, forceRefresh, includePlatforms);
    }

    bool findSafeRandomPoint(const StageGeometry& g, u32& rng, bool onGround, float& outX, float& outY)
    {
        for (int attempt = 0; attempt < 16; attempt++)
        {
            float cx = 0.0f;
            float cy = 0.0f;
            if (!sampleSafeCandidate(g, rng, onGround, cx, cy))
            {
                break;
            }
            if (onGround)
            {
                float hitY = 0.0f;
                if (rayCheckGround(cx, cy, 80.0f, hitY, true) && !rayCheckCeiling(cx, hitY + 4.0f, 150.0f))
                {
                    outX = cx;
                    outY = hitY;
                    return true;
                }
            }
            else
            {
                float hitY = 0.0f;
                // Keep airborne spawns over stage/platforms or with open headroom
                // above and not embedded inside solid geometry.
                if (!rayCheckCeiling(cx, cy + 4.0f, 150.0f))
                {
                    if (rayCheckGround(cx, cy, 80.0f, hitY, true) && cy < hitY + 4.0f)
                    {
                        cy = hitY + 8.0f;
                    }
                    outX = cx;
                    outY = cy;
                    return true;
                }
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
        if (falling)
        {
            cpu::clearKnockback(f);
            if (game::isRam(m->m_kineticModule))
            {
                m->m_kineticModule->clearSpeedAll();
            }
            if (setNoclip && game::isRam(m->m_groundModule))
            {
                m->m_groundModule->setCorrect(soGroundShapeImpl::Correct_None, 0);
            }
            cpu::placeAt(f, x, y);
            if (lr != 0.0f)
            {
                setFacingSingle(f, lr);
            }
            if (game::isRam(m->m_statusModule) && game::isRam(f->m_moduleAccesser))
            {
                m->m_statusModule->changeStatusForce(game::kStatusFall, f->m_moduleAccesser);
            }
            if (setNoclip && game::isRam(m->m_groundModule))
            {
                m->m_groundModule->setCorrect(soGroundShapeImpl::Correct_None, 0);
            }
            return;
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
        // Fighter::warp moves posture and resets ground shape (needed for
        // cross-platform warps), then attachGround + setStGroundForce binds
        // the target floor/platform, then changeStatusForce(Wait) clears
        // Fighter::warp's pending Fall request.
        Vec3f pos(x, y, 0.0f);
        f->warp(&pos, useLr, 0);
        if (game::isRam(m->m_groundModule))
        {
            if (m->m_groundModule->attachGround(0))
            {
                m->m_groundModule->setStGroundForce(0);
            }
        }
        if (game::isRam(m->m_statusModule) && game::isRam(f->m_moduleAccesser))
        {
            m->m_statusModule->changeStatusForce(game::kStatusWait, f->m_moduleAccesser);
        }
        if (game::isRam(m->m_kineticModule))
        {
            m->m_kineticModule->clearSpeedAll();
        }
        cpu::clearKnockback(f);
        if (setNoclip && game::isRam(m->m_groundModule))
        {
            m->m_groundModule->setCorrect(soGroundShapeImpl::Correct_None, 0);
        }
    }

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

        struct AirResetRule {
            u8 ftKind; // 0xFF = common (every fighter)
            u8 type;   // kAirRuleBit / kAirRuleInt / kAirRuleFloatZero
            u8 idx;
            u8 val;
        };

        // Complete P+ 3.2.1 once-per-airtime resource table verified against
        // ProjectM/Modifier/VariableSet.asm, Turbo.asm, MoveDef/Misc.asm,
        // Ledge.asm, AirDodge.asm, and Fit*.pac Landing/Wait1 scripts.
        static const AirResetRule kAirRules[] = {
            // Common airborne resets (all fighters)
            {0xFF, kAirRuleInt, 2, 0},       // ledge regrab cooldown
            {0xFF, kAirRuleInt, 3, 0},       // wall jump count
            {0xFF, kAirRuleInt, 4, 0},       // wall jump past frame
            {0xFF, kAirRuleInt, 11, 0},      // no attach wall frame
            {0xFF, kAirRuleInt, 12, 0},      // attach wall count
            {0xFF, kAirRuleInt, 62, 0},      // AirLassoHang tether count
            {0xFF, kAirRuleInt, 79, 0},      // 5-ledge-grab invuln counter
            {0xFF, kAirRuleInt, 81, 0},      // P+ AGT counter
            {0xFF, kAirRuleBit, 22, 0},      // glide used
            {0xFF, kAirRuleBit, 57, 0},      // P+ airdodge / momentum scratch
            {0xFF, kAirRuleFloatZero, 0, 0}, // landing lag override
            // Per-fighter once-per-airtime resets
            {0x00, kAirRuleBit, 62, 0}, {0x00, kAirRuleBit, 113, 0}, {0x00, kAirRuleBit, 116, 0}, {0x00, kAirRuleBit, 118, 0}, // Mario
            {0x01, kAirRuleBit, 70, 0},                                                                                          // DK
            {0x03, kAirRuleInt, 67, 0}, {0x03, kAirRuleBit, 120, 0},                                                             // Samus
            {0x04, kAirRuleBit, 112, 0}, {0x04, kAirRuleBit, 113, 0},                                                            // Yoshi
            {0x05, kAirRuleBit, 112, 0}, {0x05, kAirRuleBit, 114, 0}, {0x05, kAirRuleBit, 119, 0},                               // Kirby
            {0x08, kAirRuleBit, 61, 0}, {0x08, kAirRuleBit, 115, 0}, {0x08, kAirRuleBit, 116, 0},                                // Luigi
            {0x0A, kAirRuleBit, 66, 0}, {0x0A, kAirRuleBit, 112, 1},                                                             // Ness
            {0x0B, kAirRuleBit, 119, 0},                                                                                         // Bowser
            {0x0C, kAirRuleBit, 62, 0},                                                                                          // Peach
            {0x0D, kAirRuleBit, 126, 0}, {0x0D, kAirRuleBit, 127, 0},                                                            // Zelda
            {0x0E, kAirRuleBit, 114, 1},                                                                                         // Sheik
            {0x11, kAirRuleBit, 115, 1},                                                                                         // Marth
            {0x12, kAirRuleBit, 114, 0},                                                                                         // G&W
            {0x14, kAirRuleBit, 115, 0}, {0x14, kAirRuleBit, 120, 1}, {0x14, kAirRuleInt, 98, 0},                                // Ganondorf
            {0x15, kAirRuleBit, 118, 0}, {0x15, kAirRuleBit, 122, 0},                                                            // Wario
            {0x16, kAirRuleBit, 61, 0}, {0x16, kAirRuleBit, 115, 0}, {0x16, kAirRuleBit, 116, 0},                                // MK
            {0x17, kAirRuleBit, 113, 0}, {0x17, kAirRuleBit, 114, 1}, {0x17, kAirRuleBit, 125, 0},                               // Pit
            {0x18, kAirRuleBit, 113, 0},                                                                                         // ZSS
            {0x19, kAirRuleBit, 122, 1},                                                                                         // Olimar
            {0x1A, kAirRuleBit, 122, 1},                                                                                         // Lucas
            {0x1D, kAirRuleBit, 113, 0}, {0x1D, kAirRuleInt, 92, 0},                                                             // Charizard
            {0x1E, kAirRuleBit, 115, 0},                                                                                         // Squirtle
            {0x1F, kAirRuleInt, 75, 0},                                                                                          // Ivysaur
            {0x21, kAirRuleBit, 66, 0}, {0x21, kAirRuleBit, 69, 0},                                                              // Lucario
            {0x22, kAirRuleInt, 91, 1},                                                                                          // Ike
            {0x23, kAirRuleBit, 111, 0}, {0x23, kAirRuleBit, 114, 0}, {0x23, kAirRuleInt, 75, 3},                                // R.O.B.
            {0x26, kAirRuleBit, 114, 0}, {0x26, kAirRuleBit, 115, 0}, {0x26, kAirRuleBit, 117, 0}, {0x26, kAirRuleBit, 118, 0},
            {0x26, kAirRuleInt, 98, 0},                                                                                          // Mewtwo
            {0x27, kAirRuleBit, 115, 1},                                                                                         // Roy
            {0x2D, kAirRuleBit, 61, 0}, {0x2D, kAirRuleBit, 113, 0}, {0x2D, kAirRuleBit, 122, 0},
            {0x2D, kAirRuleInt, 91, 0}, {0x2D, kAirRuleInt, 92, 0}, {0x2D, kAirRuleInt, 93, 0},                                  // Knuckles
            {0x2E, kAirRuleBit, 61, 0}, {0x2E, kAirRuleBit, 113, 0}, {0x2E, kAirRuleFloatZero, 52, 0},                           // Snake
            {0x2F, kAirRuleBit, 61, 0}, {0x2F, kAirRuleBit, 112, 0}, {0x2F, kAirRuleBit, 113, 0},
            {0x2F, kAirRuleBit, 114, 0}, {0x2F, kAirRuleInt, 91, 0}                                                              // Sonic
        };

#pragma dont_inline on
        void refreshAirSingle(Fighter* f, int ftKind, bool grounded, bool spendDoubleJump)
        {
            const soModuleEnumeration* m = game::fighterModules(f);
            if (m == NULL || !game::isRam(m->m_workManageModule))
            {
                return;
            }
            soWorkManageModule* w = m->m_workManageModule;
            int maxJumps = 2;
            if (game::isRam(f->m_moduleAccesser))
            {
                int v = soValueAccesser::getConstantInt(f->m_moduleAccesser, kIcJumpCountMax, 0);
                if (v >= 2 && v <= 10)
                {
                    maxJumps = v;
                }
            }
            int jumpVal = grounded ? 0 : (spendDoubleJump ? maxJumps : 1);
            w->setInt(jumpVal, kLaBasicBase + 1);
            const u32 count = static_cast<u32>(sizeof(kAirRules) / sizeof(kAirRules[0]));
            for (u32 i = 0; i < count; i++)
            {
                const AirResetRule& r = kAirRules[i];
                if (r.ftKind != 0xFF && static_cast<int>(r.ftKind) != ftKind)
                {
                    continue;
                }
                if (r.type == kAirRuleBit)
                {
                    if (r.val != 0)
                    {
                        w->onFlag(kLaBitBase + r.idx);
                    }
                    else
                    {
                        w->offFlag(kLaBitBase + r.idx);
                    }
                }
                else if (r.type == kAirRuleInt)
                {
                    w->setInt(r.val, kLaBasicBase + r.idx);
                }
                else
                {
                    w->setFloat(0.0f, kLaFloatBase + r.idx);
                }
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

    void placeStanding(const game::FighterRef& ref, float x, float y, bool setNoclip, float damage, float lr)
    {
        warpSingle(ref.fighter, x, y, false, setNoclip, lr);
        if (ref.fighter != NULL)
        {
            refreshAirSingle(ref.fighter, game::fighterKind(ref.entryId, false), true, false);
        }
        if (ref.partner != NULL && ref.partner != ref.fighter)
        {
            warpSingle(ref.partner, x, y, false, setNoclip, lr);
            refreshAirSingle(ref.partner, game::fighterKind(ref.entryId, true), true, false);
        }
        if (damage >= 0.0f)
        {
            game::setDamage(ref, damage);
        }
    }

    void placeFalling(const game::FighterRef& ref, float x, float y, bool setNoclip, float damage, float lr)
    {
        warpSingle(ref.fighter, x, y, true, setNoclip, lr);
        if (ref.partner != NULL && ref.partner != ref.fighter)
        {
            warpSingle(ref.partner, x, y, true, setNoclip, lr);
        }
        if (damage >= 0.0f)
        {
            game::setDamage(ref, damage);
        }
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
