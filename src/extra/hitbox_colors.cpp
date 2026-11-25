#include "extra/hitbox_colors.h"

#include <gf/gf_camera.h>
#include <so/so_module_accesser.h>
#include <string.h>

#include "extra/hitbox_colors_core.h"
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
            kDebugMagic = 0x544D4843u,        // "TMHC"
            kCodeStart = 0x80000000,
            kCodeEnd = 0x81000000,
            kShapeOfAddr = 0x80740A3C,        // soCollision::getShape(coll, id, group)
            kBubbleAddr = 0x80541FA0,         // P+ 3.2.1 hitbox bubble drawer trampoline
            kBubbleGateAddr = 0x805483FF,     // P+ 3.2.1 debug color/bubble init gate (== 1)
            kAttackRenderDebug = 0x8074BDB0,  // soCollisionAttackModuleImpl::renderDebug
            kAttackSlotRenderDebug = 0x120,
            kAttackParts = 0x30,
            kAttackCollision = 0x68,
            kAttackFlags = 0x9C,
            kAttackAsleep = 0x40000000,
            kCatchRenderDebug = 0x80756A48,   // soCollisionCatchModuleImpl::renderDebug
            kCatchSlotRenderDebug = 0x48,
            kCatchParts = 0x2C,
            kCatchCollision = 0xC0,
            kPartActive = 2,
            kPartPower = 0x04,
            kPartVector = 0x18,
            kPartFlags38 = 0x38,
            kPartGroup = 0x88,
            kCollGroups = 0x08,
            kShapeRadius = 0x0C,
            kShapeActive = 0x24,
            kShapeVtable = 0x30,
            kShapeBefore = 0x34,
            kCapsuleMatrix = 0x40,
            kCapsuleVtable = 0x80ADEEE8,      // clCapsule
            kArrayAt = 0x0C,
            kArraySize = 0x14,
            kMaxParts = 32
        };

        const u32 kGetShapeWords[] = {
            0x9421FFF0, 0x7C0802A6, 0x80630008, 0x90010014,
            0x93E1000C, 0x7C9F2378, 0x7CA42B78, 0x81830000
        };

        const u32 kBubbleBodyWords[] = {
            0x9421FEB0, 0x7C0802A6, 0x90010154, 0xBEC10110,
            0xDBE10138, 0xDBC10140, 0xFFE00890, 0x7C7C1B78
        };

        typedef const void* (*ShapeOfFn)(const void* collision, int id, u32 group);
        typedef int (*ArraySizeFn)(const void* array);
        typedef const void* (*ArrayAtFn)(const void* array, int index);
        typedef void (*BubbleFn)(double radius, const float scaleMatrix[3][4], const float pos1[3],
                                 const float pos2[3], const u32* color1, const u32* color2,
                                 const float viewMatrix[3][4]);

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

        inline float floatAt(const void* base, u32 offset)
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
        }

        bool wordsMatch(u32 address, const u32* words, int count)
        {
            if (address < kCodeStart || address >= kCodeEnd)
            {
                return false;
            }
            for (int i = 0; i < count; i++)
            {
                if (*reinterpret_cast<const u32*>(address + 4u * static_cast<u32>(i)) != words[i])
                {
                    return false;
                }
            }
            return true;
        }

        int arraySize(const void* arr, const void* calls)
        {
            const void* fn = reinterpret_cast<const void*>(game::wordAt(calls, kArraySize));
            return game::isRam(fn) ? reinterpret_cast<ArraySizeFn>(fn)(arr) : 0;
        }

        const void* arrayAt(const void* arr, const void* calls, int idx)
        {
            const void* fn = reinterpret_cast<const void*>(game::wordAt(calls, kArrayAt));
            return game::isRam(fn) ? reinterpret_cast<ArrayAtFn>(fn)(arr, idx) : NULL;
        }

        // Validates a collision module's parts array and group array, returning part count (<= kMaxParts).
        int openCollisionParts(const void* mod, u32 partsOff, u32 collOff, const void*& outParts,
                               const void*& outCalls, const void*& outColl, int& outGroups)
        {
            outParts = reinterpret_cast<const void*>(game::wordAt(mod, partsOff));
            outCalls = game::isRam(outParts) ? reinterpret_cast<const void*>(game::wordAt(outParts, 0)) : NULL;
            if (!game::isRam(outCalls))
            {
                return 0;
            }
            int count = arraySize(outParts, outCalls);
            if (count <= 0)
            {
                return 0;
            }
            outColl = reinterpret_cast<const void*>(reinterpret_cast<u32>(mod) + collOff);
            const void* groups = reinterpret_cast<const void*>(game::wordAt(outColl, kCollGroups));
            const void* gVt = game::isRam(groups) ? reinterpret_cast<const void*>(game::wordAt(groups, 0)) : NULL;
            if (!game::isRam(gVt) || !game::isRam(reinterpret_cast<const void*>(game::wordAt(gVt, kArrayAt))))
            {
                return 0;
            }
            outGroups = arraySize(groups, gVt);
            if (outGroups <= 0)
            {
                return 0;
            }
            return count < kMaxParts ? count : kMaxParts;
        }

        bool emitShapeBubble(const void* coll, int gCount, int id, u32 group, u32 c1, const float viewMtx[3][4],
                             bool& drewBubble)
        {
            if (group >= static_cast<u32>(gCount))
            {
                return false;
            }
            const void* shape = reinterpret_cast<ShapeOfFn>(kShapeOfAddr)(coll, id, group);
            if (!game::isRam(shape) || game::byteAt(shape, kShapeActive) != 1)
            {
                return false;
            }
            float r = floatAt(shape, kShapeRadius);
            if (!(r > 0.0f && r < 500.0f))
            {
                return false;
            }
            float pos1[3] = {floatAt(shape, 0), floatAt(shape, 4), floatAt(shape, 8)};
            float pos2[3] = {floatAt(shape, kShapeBefore), floatAt(shape, kShapeBefore + 4),
                             floatAt(shape, kShapeBefore + 8)};
            float scaleMtx[3][4];
            if (game::wordAt(shape, kShapeVtable) == kCapsuleVtable)
            {
                memcpy(scaleMtx, reinterpret_cast<const void*>(reinterpret_cast<u32>(shape) + kCapsuleMatrix),
                       sizeof(scaleMtx));
            }
            else
            {
                hitboxIdentityMatrix34(scaleMtx);
            }
            u32 c2 = hitboxShadeColor(c1);
            reinterpret_cast<BubbleFn>(kBubbleAddr)(static_cast<double>(r), scaleMtx, pos1, pos2, &c1, &c2, viewMtx);
            drewBubble = true;
            return true;
        }
    }

    void hitboxColorsAttach()
    {
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kDebugMagic;
        bool shapeOk = wordsMatch(kShapeOfAddr, kGetShapeWords, static_cast<int>(sizeof(kGetShapeWords) / 4));
        u32 w = *reinterpret_cast<const u32*>(kBubbleAddr);
        u32 bubbleBody = 0;
        if ((w & 0xFC000003u) == 0x48000000u)
        {
            s32 off = static_cast<s32>(w & 0x03FFFFFCu);
            if ((off & 0x02000000) != 0)
            {
                off -= 0x04000000;
            }
            bubbleBody = kBubbleAddr + static_cast<u32>(off);
        }
        bool bubbleOk = bubbleBody != 0 && *reinterpret_cast<const u8*>(kBubbleGateAddr) == 1
                        && wordsMatch(bubbleBody, kBubbleBodyWords, static_cast<int>(sizeof(kBubbleBodyWords) / 4));
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
        if (!game::isRam(vt) || game::wordAt(vt, kAttackSlotRenderDebug) != kAttackRenderDebug)
        {
            return false;
        }

        float viewMtx[3][4];
        memcpy(viewMtx, &camMgr->m_cameras[0], sizeof(viewMtx));

        bool drewBubble = false;
        s_debug.lastMode = mode;
        s_debug.lastParts = 0;
        s_debug.lastGrabParts = 0;

        if ((game::wordAt(attack, kAttackFlags) & kAttackAsleep) == 0)
        {
            const void* parts = NULL;
            const void* calls = NULL;
            const void* coll = NULL;
            int gCount = 0;
            int limit = openCollisionParts(attack, kAttackParts, kAttackCollision, parts, calls, coll, gCount);
            if (limit > 0)
            {
                s32 minP = 0x7FFFFFFF;
                s32 maxP = -0x7FFFFFFF;
                if (mode == main::kHitboxColorProperty)
                {
                    for (int id = 0; id < limit; id++)
                    {
                        const void* part = arrayAt(parts, calls, id);
                        if (!game::isRam(part) || game::wordAt(part, 0) != kPartActive)
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
                    const void* part = arrayAt(parts, calls, id);
                    if (!game::isRam(part) || game::wordAt(part, 0) != kPartActive)
                    {
                        continue;
                    }
                    HitboxPartProps props = decodeAttackPartFlags(
                        static_cast<s32>(game::wordAt(part, kPartPower)),
                        static_cast<s32>(game::wordAt(part, kPartVector)),
                        game::wordAt(part, kPartFlags38));
                    u32 color = hitboxColorForMode(mode, id, props, minP, maxP);
                    if (emitShapeBubble(coll, gCount, id, game::wordAt(part, kPartGroup), color, viewMtx, drewBubble))
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
            if (game::isRam(cvt) && game::wordAt(cvt, kCatchSlotRenderDebug) == kCatchRenderDebug)
            {
                const void* parts = NULL;
                const void* calls = NULL;
                const void* coll = NULL;
                int gCount = 0;
                int limit = openCollisionParts(catchMod, kCatchParts, kCatchCollision, parts, calls, coll, gCount);
                for (int id = 0; id < limit; id++)
                {
                    const void* part = arrayAt(parts, calls, id);
                    if (game::isRam(part) && game::wordAt(part, 0) == kPartActive
                        && emitShapeBubble(coll, gCount, id, 0, kHitboxColorGrab, viewMtx, drewBubble))
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
