#include "extra/hitbox_colors.h"

#include <gf/gf_camera.h>
#include <so/so_module_accesser.h>
#include <string.h>

#include "extra/hitbox_colors_core.h"
#include "extra/pplus_collision.h"
#include "main/display/display.h"
#include "main/game/fighters.h"
#include "main/gfx/draw.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kDebugMagic = 0x544D4843u, // "TMHC"
            kPartPower = 0x04,
            kPartVector = 0x18,
            kPartFlags38 = 0x38,
            kPartGroup = 0x88
        };

        struct HitboxColorDebugBlock {
            u32 magic;          // 0x00 "TMHC"
            u32 shapeOk;        // 0x04
            u32 bubbleOk;       // 0x08
            u32 lastMode;       // 0x0C
            u32 lastParts;      // 0x10
            u32 lastGrabParts;  // 0x14
            u32 totalParts;     // 0x18
            u32 lastColors[8];  // 0x1C
        };

        HitboxColorDebugBlock s_debug;

        bool emitColoredShape(const void* coll, int gCount, int id, u32 group, u32 c1, const float viewMtx[3][4],
                              bool& drewBubble)
        {
            const void* shape = collShapeOf(coll, gCount, id, group);
            return collEmitShapeBubble(shape, c1, hitboxShadeColor(c1), viewMtx, drewBubble);
        }
    }

    void hitboxColorsAttach()
    {
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kDebugMagic;
        bool shapeOk = collShapeSupported();
        bool bubbleOk = collBubbleSupported();
        s_debug.shapeOk = shapeOk ? 1u : 0u;
        s_debug.bubbleOk = bubbleOk ? 1u : 0u;
        TM_LOG("hitbox_colors: debug block at %08x; shape %s, bubble %s\n", &s_debug, shapeOk ? "ok" : "off",
               bubbleOk ? "ok" : "off");
    }

    void hitboxColorsDetach()
    {
        s_debug.lastParts = 0;
        s_debug.lastGrabParts = 0;
    }

    bool drawColoredHitboxes(Fighter* fighter)
    {
        const main::DisplaySettings& d = main::settings().display;
        u8 mode = d.hitboxColorMode;
        if (mode == main::kHitboxColorDefault || !s_debug.shapeOk || !s_debug.bubbleOk)
        {
            return false;
        }
        const soModuleEnumeration* m = game::fighterModules(fighter);
        gfCameraManager* camMgr = gfCameraManager::getManager();
        if (m == NULL || !game::isRam(m->m_collisionAttackModule) || !game::isRam(camMgr))
        {
            return false;
        }
        const void* attack = m->m_collisionAttackModule;
        const void* vt = reinterpret_cast<const void*>(game::wordAt(attack, 0));
        if (!game::isRam(vt) || game::wordAt(vt, kCollAttackSlotRenderDebug) != kCollAttackRenderDebug)
        {
            return false;
        }

        float viewMtx[3][4];
        memcpy(viewMtx, &camMgr->m_cameras[0], sizeof(viewMtx));

        bool drewBubble = false;
        s_debug.lastMode = mode;
        s_debug.lastParts = 0;
        s_debug.lastGrabParts = 0;

        if ((game::wordAt(attack, kCollAttackFlags) & kCollAttackAsleep) == 0)
        {
            const void* parts = NULL;
            const void* calls = NULL;
            const void* coll = NULL;
            int gCount = 0;
            int limit = collOpenParts(attack, kCollAttackParts, kCollAttackCollision, parts, calls, coll, gCount);
            if (limit > 0)
            {
                s32 minP = 0x7FFFFFFF;
                s32 maxP = -0x7FFFFFFF;
                if (mode == main::kHitboxColorProperty)
                {
                    for (int id = 0; id < limit; id++)
                    {
                        const void* part = collArrayAt(parts, calls, id);
                        if (!game::isRam(part) || game::wordAt(part, 0) != kCollPartActive)
                        {
                            continue;
                        }
                        s32 pwr = static_cast<s32>(game::wordAt(part, kPartPower));
                        if (pwr < minP) minP = pwr;
                        if (pwr > maxP) maxP = pwr;
                    }
                }
                for (int id = 0; id < limit; id++)
                {
                    const void* part = collArrayAt(parts, calls, id);
                    if (!game::isRam(part) || game::wordAt(part, 0) != kCollPartActive)
                    {
                        continue;
                    }
                    HitboxPartProps props = decodeAttackPartFlags(
                        static_cast<s32>(game::wordAt(part, kPartPower)),
                        static_cast<s32>(game::wordAt(part, kPartVector)),
                        game::wordAt(part, kPartFlags38));
                    u32 color = hitboxColorForMode(mode, id, props, minP, maxP);
                    if (emitColoredShape(coll, gCount, id, game::wordAt(part, kPartGroup), color, viewMtx, drewBubble))
                    {
                        if (s_debug.lastParts < 8)
                        {
                            s_debug.lastColors[s_debug.lastParts] = color;
                        }
                        s_debug.lastParts++;
                        s_debug.totalParts++;
                    }
                }
            }
        }

        // In Property mode, draw emerald grabboxes when Grab Visualizer is Off.
        if (mode == main::kHitboxColorProperty && d.grabVisualizer == display::kWhoNone
            && game::isRam(m->m_collisionCatchModule))
        {
            const void* catchMod = m->m_collisionCatchModule;
            const void* cvt = reinterpret_cast<const void*>(game::wordAt(catchMod, 0));
            if (game::isRam(cvt) && game::wordAt(cvt, kCollCatchSlotRenderDebug) == kCollCatchRenderDebug)
            {
                const void* parts = NULL;
                const void* calls = NULL;
                const void* coll = NULL;
                int gCount = 0;
                int limit = collOpenParts(catchMod, kCollCatchParts, kCollCatchCollision, parts, calls, coll, gCount);
                for (int id = 0; id < limit; id++)
                {
                    const void* part = collArrayAt(parts, calls, id);
                    if (game::isRam(part) && game::wordAt(part, 0) == kCollPartActive
                        && emitColoredShape(coll, gCount, id, 0, kHitboxColorGrab, viewMtx, drewBubble))
                    {
                        s_debug.lastGrabParts++;
                        s_debug.totalParts++;
                    }
                }
            }
        }

        // Always restore main camera GX state before returning to the fighter loop in display.cpp.
        if (drewBubble)
        {
            camMgr->m_cameras[0].setGX();
            gfx::forgetState();
        }
        return true;
    }
}
}
