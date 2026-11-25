// Hitbox Heatmap and Live Reach Bounds for Project+ 3.2.1
// (algorithm adapted from fudgepop01's SuperTraining hitbox heatmap; see
// CREDITS.md and THIRD_PARTY_NOTICES.md).
//
// Samples active attack parts on tracker frames via stock P+ 3.2.1's resident
// soCollision::getShape (0x80740A3C) into a ring buffer allocated from
// Heaps::Network on attach/detach, and renders capsules/spheres during
// display::render3d() via stock P+ 3.2.1's resident kBubble (0x80541FA0,
// gated on 0x805483FF == 1), restoring the camera GX state afterward.
#include "extra/hitbox_heatmap.h"
#include "extra/net_alloc.h"

#include <GX/GXTypes.h>
#include <ft/fighter.h>
#include <gf/gf_camera.h>
#include <gf/gf_task.h>
#include <gf/gf_task_scheduler.h>
#include <it/item.h>
#include <so/collision/so_collision_attack_module_impl.h>
#include <so/motion/so_motion_module_impl.h>
#include <so/posture/so_posture_module_impl.h>
#include <so/so_kind.h>
#include <so/so_module_accesser.h>
#include <so/stageobject.h>
#include <string.h>
#include <wn/weapon.h>

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
            kDebugMagic = 0x544D484D, // "TMHM"
            kCodeStart = 0x80000000,
            kCodeEnd = 0x81000000,
            kShapeOfAddr = 0x80740A3C,        // soCollision::getShape(coll, id, group)
            kBubbleAddr = 0x80541FA0,         // P+ 3.2.1 hitbox bubble drawer trampoline
            kBubbleGateAddr = 0x805483FF,     // P+ 3.2.1 debug color/bubble init gate (== 1)
            kAttackRenderDebug = 0x8074BDB0,  // soCollisionAttackModuleImpl::renderDebug
            kSlotRenderDebug = 0x120,
            kStageObjVptr = 0x3C,             // gfTask vptr (sizeof(gfTask) == 0x40)
            kSlotProcessAnim = 0x10,
            kStageObjProcessAnim = 0x8070F444, // StageObject::processAnim
            kAccesserOffset = 0x60,
            kPosturePos = 0x0C,
            kAttackParts = 0x30,
            kAttackCollision = 0x68,
            kAttackFlags = 0x9C,
            kAttackAsleep = 0x40000000,
            kPartActive = 2,
            kPartGroup = 0x88,
            kCollGroups = 0x08,
            kShapeRadius = 0x0C,
            kShapeActive = 0x24,
            kShapeVtable = 0x30,
            kShapeBefore = 0x34,
            kCapsuleMatrix = 0x40,
            kCapsuleScale = 0x70,
            kCapsuleVtable = 0x80ADEEE8,      // clCapsule
            kArrayAt = 0x0C,
            kArraySize = 0x14,
            kCategoryLists = 0x58,            // gfTaskScheduler category list heads (0x8002EFE8)
            kTaskNext = 0x10,
            kTaskFlags = 0x2C,
            kTaskAlive = 0x40000000,
            kMaxAttackParts = 32,
            kMaxCategoryTasks = 96,
            kUnownedSlot = 0xFF
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

        const gfx::Rgba kBoundsColor = 0xFACC15E0;
        const u8 kBoundsLineWidth = 18;

        typedef const void* (*ShapeOfFn)(const void* collision, int id, u32 group);
        typedef int (*ArraySizeFn)(const void* array);
        typedef const void* (*ArrayAtFn)(const void* array, int index);
        typedef void (*BubbleFn)(double radius, const float scaleMatrix[3][4], const float pos1[3],
                                 const float pos2[3], const u32* color1, const u32* color2,
                                 const float viewMatrix[3][4]);

        // RAM debug struct exposed for live Dolphin tests ("TMHM").
        struct HeatmapDebugBlock {
            u32 magic;        // 0x00 "TMHM"
            u32 shapeOk;      // 0x04
            u32 bubbleOk;     // 0x08
            u32 ringCount;    // 0x0C
            u32 totalSampled; // 0x10
            u32 drawCalls;    // 0x14
            u32 boundsDrawn;  // 0x18
            u32 resets;       // 0x1C
            HitboxBounds bounds[kHeatmapMaxFighters]; // 0x20
        };

        struct FighterSlotInfo {
            bool present;
            bool wanted;
            u32 mainTaskId;
            u32 partnerTaskId;
            float originX;
            float originY;
        };

        HeatmapDebugBlock s_debug;
        HeatmapRing* s_ring = NULL;
        u8 s_framesAfterActionable[kHeatmapMaxFighters];
        bool s_boundsOverwrite[kHeatmapMaxFighters];
        bool s_playerWanted[kHeatmapMaxFighters];

        u32 readWord(u32 address)
        {
            return *reinterpret_cast<const u32*>(address);
        }

        float floatAt(const void* base, u32 offset)
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
        }

        u32 branchTarget(u32 address)
        {
            u32 w = readWord(address);
            if ((w & 0xFC000003u) != 0x48000000u)
            {
                return 0;
            }
            s32 offset = static_cast<s32>(w & 0x03FFFFFCu);
            if ((offset & 0x02000000) != 0)
            {
                offset -= 0x04000000;
            }
            return address + static_cast<u32>(offset);
        }

        bool wordsMatch(u32 address, const u32* words, int count)
        {
            if (address < kCodeStart || address >= kCodeEnd)
            {
                return false;
            }
            for (int i = 0; i < count; i++)
            {
                if (readWord(address + 4u * static_cast<u32>(i)) != words[i])
                {
                    return false;
                }
            }
            return true;
        }

        int safeArraySize(const void* arr, const void* calls)
        {
            const void* fn = reinterpret_cast<const void*>(game::wordAt(calls, kArraySize));
            return game::isRam(fn) ? reinterpret_cast<ArraySizeFn>(fn)(arr) : 0;
        }

        const void* safeArrayAt(const void* arr, const void* calls, int index)
        {
            const void* fn = reinterpret_cast<const void*>(game::wordAt(calls, kArrayAt));
            return game::isRam(fn) ? reinterpret_cast<ArrayAtFn>(fn)(arr, index) : NULL;
        }

        // Queries the coll+8 group array count once per attack module before
        // calling soCollision::getShape (0x80740A3C).
        int safeGroupCount(const void* coll)
        {
            if (!s_debug.shapeOk || !game::isRam(coll))
            {
                return 0;
            }
            const void* groups = reinterpret_cast<const void*>(game::wordAt(coll, kCollGroups));
            const void* gVt = game::isRam(groups) ? reinterpret_cast<const void*>(game::wordAt(groups, 0)) : NULL;
            if (!game::isRam(gVt))
            {
                return 0;
            }
            const void* atFn = reinterpret_cast<const void*>(game::wordAt(gVt, kArrayAt));
            return game::isRam(atFn) ? safeArraySize(groups, gVt) : 0;
        }

        const void* safeShapeOf(const void* coll, int gCount, int id, u32 group)
        {
            if (gCount <= 0 || group >= static_cast<u32>(gCount))
            {
                return NULL;
            }
            return reinterpret_cast<ShapeOfFn>(kShapeOfAddr)(coll, id, group);
        }

        // Verifies a non-fighter gfTask is a StageObject of `expectedKind`
        // (StageObject_Weapon or StageObject_Item) BEFORE reading its
        // soModuleAccesser (+0x60) and soCollisionAttackModule.
        const soModuleEnumeration* verifiedTaskModules(const void* task, soKind expectedKind, StageObject*& outObj)
        {
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
            if (obj->soGetKind() != expectedKind)
            {
                return NULL;
            }
            const soModuleAccesser* accesser =
                reinterpret_cast<const soModuleAccesser*>(game::wordAt(task, kAccesserOffset));
            if (!game::isRam(accesser) || !game::isRam(accesser->m_enumerationStart))
            {
                return NULL;
            }
            outObj = obj;
            return accesser->m_enumerationStart;
        }

        bool whoMatches(u8 who, bool cpu)
        {
            return (who & (cpu ? display::kWhoCpu : display::kWhoHuman)) != 0;
        }

        void sampleAttackModule(const soModuleEnumeration* m, u8 playerSlot, u8 frameOffset, bool updateBounds,
                                float originX, float originY)
        {
            if (m == NULL || !game::isRam(m->m_collisionAttackModule))
            {
                return;
            }
            const void* attack = m->m_collisionAttackModule;
            const void* vt = reinterpret_cast<const void*>(game::wordAt(attack, 0));
            if (!game::isRam(vt) || game::wordAt(vt, kSlotRenderDebug) != kAttackRenderDebug
                || (game::wordAt(attack, kAttackFlags) & kAttackAsleep) != 0)
            {
                return;
            }
            const void* parts = reinterpret_cast<const void*>(game::wordAt(attack, kAttackParts));
            const void* calls = game::isRam(parts) ? reinterpret_cast<const void*>(game::wordAt(parts, 0)) : NULL;
            if (!game::isRam(calls))
            {
                return;
            }
            int count = safeArraySize(parts, calls);
            if (count <= 0)
            {
                return;
            }
            const void* atkColl = reinterpret_cast<const void*>(reinterpret_cast<u32>(attack) + kAttackCollision);
            int gCount = safeGroupCount(atkColl);
            if (gCount <= 0)
            {
                return;
            }

            for (int id = 0; id < count && id < kMaxAttackParts; id++)
            {
                const void* part = safeArrayAt(parts, calls, id);
                if (!game::isRam(part) || game::wordAt(part, 0) != kPartActive)
                {
                    continue;
                }
                const void* shape = safeShapeOf(atkColl, gCount, id, game::wordAt(part, kPartGroup));
                if (!game::isRam(shape) || game::byteAt(shape, kShapeActive) != 1)
                {
                    continue;
                }
                float r = floatAt(shape, kShapeRadius);
                if (!(r > 0.0f && r < 500.0f))
                {
                    continue;
                }

                HeatmapEntry e;
                e.pos1[0] = floatAt(shape, 0);
                e.pos1[1] = floatAt(shape, 4);
                e.pos1[2] = floatAt(shape, 8);
                e.pos2[0] = floatAt(shape, kShapeBefore);
                e.pos2[1] = floatAt(shape, kShapeBefore + 4);
                e.pos2[2] = floatAt(shape, kShapeBefore + 8);
                e.radius = r;
                e.playerSlot = playerSlot;
                e.frameOffset = frameOffset;
                e.life = kHeatmapDefaultLife;
                e.active = 1;

                float effectiveR = r;
                if (game::wordAt(shape, kShapeVtable) == kCapsuleVtable)
                {
                    memcpy(e.scaleMatrix, reinterpret_cast<const void*>(reinterpret_cast<u32>(shape) + kCapsuleMatrix),
                           sizeof(e.scaleMatrix));
                    float sc = floatAt(shape, kCapsuleScale);
                    if (sc > 0.0f && sc < 10.0f)
                    {
                        effectiveR = r * sc;
                    }
                }
                else
                {
                    identityMatrix34(e.scaleMatrix);
                }

                if (s_ring != NULL)
                {
                    s_ring->push(e);
                }

                if (updateBounds && playerSlot < kHeatmapMaxFighters)
                {
                    if (s_boundsOverwrite[playerSlot])
                    {
                        boundsReset(s_debug.bounds[playerSlot]);
                        s_boundsOverwrite[playerSlot] = false;
                    }
                    boundsAccumulateCapsule(s_debug.bounds[playerSlot], originX, originY, e.pos1, e.pos2, effectiveR);
                }
            }
        }

        int ownerSlotByTaskId(const FighterSlotInfo slots[kHeatmapMaxFighters], int taskId)
        {
            if (taskId <= 0)
            {
                return -1;
            }
            u32 tid = static_cast<u32>(taskId);
            for (int p = 0; p < kHeatmapMaxFighters; p++)
            {
                if (!slots[p].present)
                {
                    continue;
                }
                if (slots[p].mainTaskId == tid || (slots[p].partnerTaskId != 0 && slots[p].partnerTaskId == tid))
                {
                    return p;
                }
            }
            return -1;
        }

        void sampleCategoryTasks(gfTask::Category category, soKind expectedKind, u8 heatmapWho,
                                 const FighterSlotInfo slots[kHeatmapMaxFighters])
        {
            const void* scheduler = g_taskScheduler;
            if (!game::isRam(scheduler))
            {
                return;
            }
            const void* task = reinterpret_cast<const void*>(
                game::wordAt(scheduler, kCategoryLists + 4u * static_cast<u32>(category)));
            for (int n = 0; n < kMaxCategoryTasks && game::isRam(task); n++)
            {
                StageObject* obj = NULL;
                const soModuleEnumeration* m = verifiedTaskModules(task, expectedKind, obj);
                if (m != NULL && obj != NULL)
                {
                    int ownerTaskId = -1;
                    if (expectedKind == StageObject_Weapon)
                    {
                        ownerTaskId = static_cast<Weapon*>(obj)->getFounderTaskId();
                    }
                    else if (expectedKind == StageObject_Item)
                    {
                        ownerTaskId = static_cast<BaseItem*>(obj)->m_createOwnerTaskId;
                    }
                    if (ownerTaskId <= 0 && game::isRam(m->m_collisionAttackModule))
                    {
                        ownerTaskId = m->m_collisionAttackModule->getIndirectTaskId();
                    }
                    int slot = ownerSlotByTaskId(slots, ownerTaskId);
                    if (slot >= 0)
                    {
                        if (slots[slot].wanted)
                        {
                            sampleAttackModule(m, static_cast<u8>(slot), s_framesAfterActionable[slot], true,
                                               slots[slot].originX, slots[slot].originY);
                        }
                    }
                    else if (heatmapWho == display::kWhoBoth)
                    {
                        sampleAttackModule(m, kUnownedSlot, 0, false, 0.0f, 0.0f);
                    }
                }
                task = reinterpret_cast<const void*>(game::wordAt(task, kTaskNext));
            }
        }
    }

    void heatmapReset()
    {
        if (s_ring != NULL)
        {
            s_ring->clear();
        }
        for (int p = 0; p < kHeatmapMaxFighters; p++)
        {
            s_framesAfterActionable[p] = 0;
            s_boundsOverwrite[p] = true;
            s_playerWanted[p] = false;
            boundsReset(s_debug.bounds[p]);
        }
        s_debug.ringCount = 0;
        s_debug.drawCalls = 0;
        s_debug.boundsDrawn = 0;
        s_debug.resets++;
    }

    void heatmapAttach()
    {
        freeNetPtr(s_ring);
        s_ring = allocNetArray<HeatmapRing>(1);
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kDebugMagic;
        bool shapeOk = wordsMatch(kShapeOfAddr, kGetShapeWords, static_cast<int>(sizeof(kGetShapeWords) / 4));
        u32 bubbleBody = branchTarget(kBubbleAddr);
        bool bubbleGate = *reinterpret_cast<const u8*>(kBubbleGateAddr) == 1;
        bool bubbleOk = bubbleBody != 0 && bubbleGate
                        && wordsMatch(bubbleBody, kBubbleBodyWords, static_cast<int>(sizeof(kBubbleBodyWords) / 4));
        s_debug.shapeOk = shapeOk ? 1u : 0u;
        s_debug.bubbleOk = bubbleOk ? 1u : 0u;
        heatmapReset();
        s_debug.resets = 0;
        TM_LOG("heatmap: debug block at %08x; shape %s, bubble %s\n", &s_debug, shapeOk ? "ok" : "off",
               bubbleOk ? "ok" : "off");
    }

    void heatmapDetach()
    {
        heatmapReset();
        freeNetPtr(s_ring);
    }

    void heatmapTick()
    {
        if (s_ring == NULL)
        {
            return;
        }
        const main::DisplaySettings& d = main::settings().display;
        if (d.heatmap == display::kWhoNone)
        {
            bool anyBounds = false;
            for (int p = 0; p < kHeatmapMaxFighters; p++)
            {
                anyBounds = anyBounds || s_debug.bounds[p].valid != 0;
            }
            if (s_ring->count > 0 || anyBounds)
            {
                heatmapReset();
            }
            return;
        }

        if (s_ring->count > 0)
        {
            s_ring->tickAge();
        }

        FighterSlotInfo slots[kHeatmapMaxFighters];
        memset(slots, 0, sizeof(slots));
        for (int p = 0; p < kHeatmapMaxFighters; p++)
        {
            s_playerWanted[p] = false;
        }

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p < 0 || p >= kHeatmapMaxFighters)
            {
                continue;
            }
            FighterSlotInfo& info = slots[p];
            info.present = true;
            info.wanted = whoMatches(d.heatmap, refs[i].cpu);
            s_playerWanted[p] = info.wanted;
            info.mainTaskId = game::isRam(refs[i].fighter) ? refs[i].fighter->m_taskId : 0;
            info.partnerTaskId = (refs[i].partner != NULL && game::isRam(refs[i].partner))
                                     ? refs[i].partner->m_taskId
                                     : 0;

            const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
            if (m != NULL)
            {
                int status = game::fighterStatus(m);
                float animFrame = game::isRam(m->m_motionModule) ? m->m_motionModule->getFrame() : 0.0f;
                s_framesAfterActionable[p] = stepActionableGroundFrames(s_framesAfterActionable[p], status, animFrame);
                if (isHeatmapResetStatus(status, animFrame))
                {
                    s_boundsOverwrite[p] = true;
                }
                if (game::isRam(m->m_postureModule))
                {
                    info.originX = floatAt(m->m_postureModule, kPosturePos);
                    info.originY = floatAt(m->m_postureModule, kPosturePos + 4);
                }
                if (info.wanted && s_debug.shapeOk)
                {
                    sampleAttackModule(m, static_cast<u8>(p), s_framesAfterActionable[p], true, info.originX,
                                       info.originY);
                }
            }
            if (refs[i].partner != NULL && refs[i].partner != refs[i].fighter && info.wanted && s_debug.shapeOk)
            {
                const soModuleEnumeration* pm = game::fighterModules(refs[i].partner);
                if (pm != NULL)
                {
                    sampleAttackModule(pm, static_cast<u8>(p), s_framesAfterActionable[p], true, info.originX,
                                       info.originY);
                }
            }
        }

        if (s_debug.shapeOk)
        {
            sampleCategoryTasks(gfTask::Category_Weapon, StageObject_Weapon, d.heatmap, slots);
            sampleCategoryTasks(gfTask::Category_Item, StageObject_Item, d.heatmap, slots);
        }

        s_debug.ringCount = s_ring->count;
        s_debug.totalSampled = s_ring->totalPushed;
    }

    void heatmapDraw(const game::MatchState& match)
    {
        s_debug.drawCalls = 0;
        s_debug.boundsDrawn = 0;
        const main::DisplaySettings& d = main::settings().display;
        if (!match.running || match.paused || d.heatmap == display::kWhoNone || s_ring == NULL)
        {
            return;
        }
        gfCameraManager* camMgr = gfCameraManager::getManager();
        if (!game::isRam(camMgr))
        {
            return;
        }
        float viewMtx[3][4];
        memcpy(viewMtx, &camMgr->m_cameras[0], sizeof(viewMtx));

        u8 alpha = opacityPercentToAlpha(d.heatmapOpacity);
        u8 step = clampColorFrames(d.heatmapColorFrames);

        if (s_debug.bubbleOk && s_ring->count > 0)
        {
            BubbleFn drawBubble = reinterpret_cast<BubbleFn>(kBubbleAddr);
            int idx = static_cast<int>(s_ring->head);
            u16 drawn = 0;
            u16 total = s_ring->count;
            int lastOffset = -1;
            u32 c1 = 0;
            u32 c2 = 0;
            for (int k = 0; k < kHeatmapRing && drawn < total; k++)
            {
                const HeatmapEntry& e = s_ring->entries[idx];
                if (++idx >= kHeatmapRing)
                {
                    idx = 0;
                }
                if (e.active == 0)
                {
                    continue;
                }
                if (static_cast<int>(e.frameOffset) != lastOffset)
                {
                    lastOffset = static_cast<int>(e.frameOffset);
                    c1 = heatmapColor(e.frameOffset, step, alpha);
                    c2 = heatmapShadeColor(c1);
                }
                drawBubble(static_cast<double>(e.radius), e.scaleMatrix, e.pos1, e.pos2, &c1, &c2, viewMtx);
                drawn++;
                s_debug.drawCalls++;
            }
            // kBubble alters GX TEV/blend/matrix state; restore main camera GX
            // and invalidate our 2D/line GX cache for any subsequent draws.
            camMgr->m_cameras[0].setGX();
            gfx::forgetState();
        }

        if (d.heatmapBounds != 0)
        {
            for (int p = 0; p < kHeatmapMaxFighters; p++)
            {
                const HitboxBounds& b = s_debug.bounds[p];
                if (!b.valid || !s_playerWanted[p])
                {
                    continue;
                }
                gfx::lineBegin(GX_LINESTRIP, 5, kBoundsLineWidth);
                gfx::vertex(b.worldXMin, b.worldYMin, kBoundsColor);
                gfx::vertex(b.worldXMax, b.worldYMin, kBoundsColor);
                gfx::vertex(b.worldXMax, b.worldYMax, kBoundsColor);
                gfx::vertex(b.worldXMin, b.worldYMax, kBoundsColor);
                gfx::vertex(b.worldXMin, b.worldYMin, kBoundsColor);
                s_debug.boundsDrawn++;
            }
        }
    }

    const HitboxBounds* heatmapBounds(int playerNo)
    {
        if (playerNo < 0 || playerNo >= kHeatmapMaxFighters)
        {
            return NULL;
        }
        return &s_debug.bounds[playerNo];
    }
}
}
