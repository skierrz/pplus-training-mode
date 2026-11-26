#pragma once

#include <types.h>

#include "main/game/fighters.h"

class StageObject;
struct soModuleEnumeration;

// Shared P+ 3.2.1 soCollision / kBubble inspection and bubble-rendering helpers
// used by hitbox_colors, grab_visualizer, and hitbox_heatmap inside the extra module.
namespace tm {
namespace extra {
    enum {
        kCollAttackRenderDebug = 0x8074BDB0,  // soCollisionAttackModuleImpl::renderDebug
        kCollAttackSlotRenderDebug = 0x120,
        kCollAttackParts = 0x30,
        kCollAttackCollision = 0x68,
        kCollAttackFlags = 0x9C,
        kCollAttackAsleep = 0x40000000,
        kCollCatchRenderDebug = 0x80756A48,   // soCollisionCatchModuleImpl::renderDebug
        kCollCatchSlotRenderDebug = 0x48,
        kCollCatchParts = 0x2C,
        kCollCatchCollision = 0xC0,
        kCollSearchRenderDebug = 0x807597DC,  // soCollisionSearchModuleImpl::renderDebug
        kCollSearchSlotRenderDebug = 0x50,
        kCollSearchParts = 0x2C,
        kCollSearchCollision = 0xB8,
        kCollPartActive = 2,
        kCollShapeRadius = 0x0C,
        kCollShapeActive = 0x24,
        kCollShapeVtable = 0x30,
        kCollShapeBefore = 0x34,
        kCollCapsuleMatrix = 0x40,
        kCollCapsuleScale = 0x70,
        kCollCapsuleVtable = 0x80ADEEE8,      // clCapsule
        kCollMaxParts = 32
    };

    inline float collFloatAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
    }

#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    bool x_collShapeSupported();
    bool x_collBubbleSupported();
    const void* x_collArrayAt(const void* arr, const void* calls, int idx);
    int x_collOpenParts(const void* mod, u32 partsOff, u32 collOff, const void*& outParts,
                        const void*& outCalls, const void*& outColl, int& outGroups);
    const void* x_collShapeOf(const void* coll, int gCount, int id, u32 group);
    void x_collIdentityMatrix34(float m[3][4]);
    void x_collDrawBubble(double radius, const float scaleMtx[3][4], const float pos1[3],
                          const float pos2[3], u32 c1, u32 c2, const float viewMtx[3][4]);
#endif

    // Verifies P+ 3.2.1 resident soCollision::getShape (0x80740A3C) and
    // kBubble trampoline (0x80541FA0 + gate 0x805483FF == 1) signatures.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    static inline bool collShapeSupported() { return x_collShapeSupported(); }
    static inline bool collBubbleSupported() { return x_collBubbleSupported(); }

    int collArraySize(const void* arr, const void* calls);
    static inline const void* collArrayAt(const void* arr, const void* calls, int idx)
    {
        return x_collArrayAt(arr, calls, idx);
    }

    static inline int collOpenParts(const void* mod, u32 partsOff, u32 collOff, const void*& outParts,
                                    const void*& outCalls, const void*& outColl, int& outGroups)
    {
        return x_collOpenParts(mod, partsOff, collOff, outParts, outCalls, outColl, outGroups);
    }

    static inline const void* collShapeOf(const void* coll, int gCount, int id, u32 group)
    {
        return x_collShapeOf(coll, gCount, id, group);
    }

    static inline void collIdentityMatrix34(float m[3][4]) { x_collIdentityMatrix34(m); }

    static inline void collDrawBubble(double radius, const float scaleMtx[3][4], const float pos1[3],
                                      const float pos2[3], u32 c1, u32 c2, const float viewMtx[3][4])
    {
        x_collDrawBubble(radius, scaleMtx, pos1, pos2, c1, c2, viewMtx);
    }
#else
    bool collShapeSupported();
    bool collBubbleSupported();

    int collArraySize(const void* arr, const void* calls);
    const void* collArrayAt(const void* arr, const void* calls, int idx);

    // Validates a collision module's parts array and group array; returns min(count, kCollMaxParts).
    int collOpenParts(const void* mod, u32 partsOff, u32 collOff, const void*& outParts,
                      const void*& outCalls, const void*& outColl, int& outGroups);

    const void* collShapeOf(const void* coll, int gCount, int id, u32 group);

    void collIdentityMatrix34(float m[3][4]);

    void collDrawBubble(double radius, const float scaleMtx[3][4], const float pos1[3],
                        const float pos2[3], u32 c1, u32 c2, const float viewMtx[3][4]);
#endif

    // Validates an active soCollision shape and emits it through kBubble.
    bool collEmitShapeBubble(const void* shape, u32 c1, u32 c2, const float viewMtx[3][4],
                             bool& drewBubble);

    struct CollFighterSlot {
        bool present;
        bool wanted;
        u32 mainTaskId;
        u32 partnerTaskId;
        float originX;
        float originY;
    };

#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    const soModuleEnumeration* x_collVerifiedTaskModules(const void* task, int expectedKind, bool requireWork,
                                                         StageObject*& outObj);
    int x_collOwnerSlotByTaskId(const CollFighterSlot* slots, int count, int taskId);
#endif

    // Verifies a non-fighter gfTask is a StageObject of `expectedKind`
    // (StageObject_Weapon or StageObject_Item) before reading its module array.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    static inline const soModuleEnumeration* collVerifiedTaskModules(const void* task, int expectedKind,
                                                                     bool requireWork, StageObject*& outObj)
    {
        return x_collVerifiedTaskModules(task, expectedKind, requireWork, outObj);
    }

    static inline int collOwnerSlotByTaskId(const CollFighterSlot* slots, int count, int taskId)
    {
        return x_collOwnerSlotByTaskId(slots, count, taskId);
    }
#else
    const soModuleEnumeration* collVerifiedTaskModules(const void* task, int expectedKind, bool requireWork,
                                                       StageObject*& outObj);

    int collOwnerSlotByTaskId(const CollFighterSlot* slots, int count, int taskId);
#endif
}
}
