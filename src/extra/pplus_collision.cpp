#include "extra/pplus_collision.h"

#include <so/so_kind.h>
#include <so/so_module_accesser.h>
#include <so/stageobject.h>
#include <string.h>

#include "shared/game/mem.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kCodeStart = 0x80000000,
            kCodeEnd = 0x81000000,
            kShapeOfAddr = 0x80740A3C,    // soCollision::getShape(coll, id, group)
            kBubbleAddr = 0x80541FA0,     // P+ 3.2.1 hitbox bubble drawer trampoline
            kBubbleGateAddr = 0x805483FF, // P+ 3.2.1 debug color/bubble init gate (== 1)
            kCollGroups = 0x08,
            kArrayAt = 0x0C,
            kArraySize = 0x14
        };

        // Verified in P+ 3.2.1 RSBE01 rev 2 RAM dump:
        // soCollision::getShape (0x80740A3C): stwu r1,-16(r1); mflr r0; lwz r3,8(r3); ...
        const u32 kGetShapeWords[] = {
            0x9421FFF0, 0x7C0802A6, 0x80630008, 0x90010014,
            0x93E1000C, 0x7C9F2378, 0x7CA42B78, 0x81830000
        };

        // Target of branch at 0x80541FA0 (0x80576E18 in P+ 3.2.1):
        // stwu r1,-336(r1); mflr r0; stw r0,340(r1); stmw r22,272(r1); ...
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
    }

#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    bool collShapeSupported()
    {
        return wordsMatch(kShapeOfAddr, kGetShapeWords, static_cast<int>(sizeof(kGetShapeWords) / 4));
    }

    bool collBubbleSupported()
    {
        u32 w = *reinterpret_cast<const u32*>(kBubbleAddr);
        if ((w & 0xFC000003u) != 0x48000000u)
        {
            return false;
        }
        s32 off = static_cast<s32>(w & 0x03FFFFFCu);
        if ((off & 0x02000000) != 0)
        {
            off -= 0x04000000;
        }
        u32 body = kBubbleAddr + static_cast<u32>(off);
        return body != 0 && *reinterpret_cast<const u8*>(kBubbleGateAddr) == 1
               && wordsMatch(body, kBubbleBodyWords, static_cast<int>(sizeof(kBubbleBodyWords) / 4));
    }

    int collArraySize(const void* arr, const void* calls)
    {
        const void* fn = reinterpret_cast<const void*>(game::wordAt(calls, kArraySize));
        return game::isRam(fn) ? reinterpret_cast<ArraySizeFn>(fn)(arr) : 0;
    }

    const void* collArrayAt(const void* arr, const void* calls, int idx)
    {
        const void* fn = reinterpret_cast<const void*>(game::wordAt(calls, kArrayAt));
        return game::isRam(fn) ? reinterpret_cast<ArrayAtFn>(fn)(arr, idx) : NULL;
    }

    int collOpenParts(const void* mod, u32 partsOff, u32 collOff, const void*& outParts,
                      const void*& outCalls, const void*& outColl, int& outGroups)
    {
        outParts = reinterpret_cast<const void*>(game::wordAt(mod, partsOff));
        outCalls = game::isRam(outParts) ? reinterpret_cast<const void*>(game::wordAt(outParts, 0)) : NULL;
        if (!game::isRam(outCalls))
        {
            return 0;
        }
        int count = collArraySize(outParts, outCalls);
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
        outGroups = collArraySize(groups, gVt);
        if (outGroups <= 0)
        {
            return 0;
        }
        return count < kCollMaxParts ? count : kCollMaxParts;
    }

    const void* collShapeOf(const void* coll, int gCount, int id, u32 group)
    {
        if (gCount <= 0 || group >= static_cast<u32>(gCount))
        {
            return NULL;
        }
        return reinterpret_cast<ShapeOfFn>(kShapeOfAddr)(coll, id, group);
    }

    void collIdentityMatrix34(float m[3][4])
    {
        memset(m, 0, sizeof(float) * 12);
        m[0][0] = 1.0f;
        m[1][1] = 1.0f;
        m[2][2] = 1.0f;
    }

    void collDrawBubble(double radius, const float scaleMtx[3][4], const float pos1[3],
                        const float pos2[3], u32 c1, u32 c2, const float viewMtx[3][4])
    {
        reinterpret_cast<BubbleFn>(kBubbleAddr)(radius, scaleMtx, pos1, pos2, &c1, &c2, viewMtx);
    }

    bool collEmitShapeBubble(const void* shape, u32 c1, u32 c2, const float viewMtx[3][4],
                             bool& drewBubble)
    {
        if (!game::isRam(shape) || game::byteAt(shape, kCollShapeActive) != 1)
        {
            return false;
        }
        float r = collFloatAt(shape, kCollShapeRadius);
        if (!(r > 0.0f && r < 500.0f))
        {
            return false;
        }
        float pos1[3] = {collFloatAt(shape, 0), collFloatAt(shape, 4), collFloatAt(shape, 8)};
        float pos2[3] = {collFloatAt(shape, kCollShapeBefore), collFloatAt(shape, kCollShapeBefore + 4),
                         collFloatAt(shape, kCollShapeBefore + 8)};
        float scaleMtx[3][4];
        if (game::wordAt(shape, kCollShapeVtable) == kCollCapsuleVtable)
        {
            memcpy(scaleMtx, reinterpret_cast<const void*>(reinterpret_cast<u32>(shape) + kCollCapsuleMatrix),
                   sizeof(scaleMtx));
        }
        else
        {
            collIdentityMatrix34(scaleMtx);
        }
        collDrawBubble(static_cast<double>(r), scaleMtx, pos1, pos2, c1, c2, viewMtx);
        drewBubble = true;
        return true;
    }

    const soModuleEnumeration* collVerifiedTaskModules(const void* task, int expectedKind, bool requireWork,
                                                       StageObject*& outObj)
    {
        enum {
            kTaskFlags = 0x2C,
            kTaskAlive = 0x40000000,
            kStageObjVptr = 0x3C,
            kSlotProcessAnim = 0x10,
            kStageObjProcessAnim = 0x8070F444,
            kAccesserOffset = 0x60
        };
        outObj = NULL;
        if (!game::isRam(task) || (game::wordAt(task, kTaskFlags) & kTaskAlive) == 0)
        {
            return NULL;
        }
        const void* vt = reinterpret_cast<const void*>(game::wordAt(task, kStageObjVptr));
        if (!game::isRam(vt) || game::wordAt(vt, kSlotProcessAnim) != kStageObjProcessAnim)
        {
            return NULL;
        }
        StageObject* obj = const_cast<StageObject*>(reinterpret_cast<const StageObject*>(task));
        if (static_cast<int>(obj->soGetKind()) != expectedKind)
        {
            return NULL;
        }
        const soModuleAccesser* accesser =
            reinterpret_cast<const soModuleAccesser*>(game::wordAt(task, kAccesserOffset));
        if (!game::isRam(accesser) || !game::isRam(accesser->m_enumerationStart)
            || (requireWork && !game::isRam(accesser->m_enumerationStart->m_workManageModule)))
        {
            return NULL;
        }
        outObj = obj;
        return accesser->m_enumerationStart;
    }

    int collOwnerSlotByTaskId(const CollFighterSlot* slots, int count, int taskId)
    {
        if (taskId <= 0)
        {
            return -1;
        }
        u32 tid = static_cast<u32>(taskId);
        for (int p = 0; p < count; p++)
        {
            if (slots[p].present && (slots[p].mainTaskId == tid || (slots[p].partnerTaskId != 0 && slots[p].partnerTaskId == tid)))
            {
                return p;
            }
        }
        return -1;
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif
}
}
