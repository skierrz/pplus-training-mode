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
#include "extra/pplus_collision.h"

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
            kStageObjVptr = 0x3C,             // gfTask vptr (sizeof(gfTask) == 0x40)
            kSlotProcessAnim = 0x10,
            kStageObjProcessAnim = 0x8070F444, // StageObject::processAnim
            kAccesserOffset = 0x60,
            kPosturePos = 0x0C,
            kPartGroup = 0x88,
            kCategoryLists = 0x58,            // gfTaskScheduler category list heads (0x8002EFE8)
            kTaskNext = 0x10,
            kTaskFlags = 0x2C,
            kTaskAlive = 0x40000000,
            kMaxCategoryTasks = 96,
            kUnownedSlot = 0xFF
        };

        const gfx::Rgba kBoundsColor = 0xFACC15E0;
        const u8 kBoundsLineWidth = 18;

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

        struct HeatmapNet {
            HeatmapRing ring;
            HeatmapDebugBlock debug;
            u8 framesAfterActionable[kHeatmapMaxFighters];
            bool boundsOverwrite[kHeatmapMaxFighters];
            bool playerWanted[kHeatmapMaxFighters];
        };

        HeatmapNet* s_net = NULL;

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
            if (!game::isRam(vt) || game::wordAt(vt, kCollAttackSlotRenderDebug) != kCollAttackRenderDebug
                || (game::wordAt(attack, kCollAttackFlags) & kCollAttackAsleep) != 0)
            {
                return;
            }
            const void* parts = NULL;
            const void* calls = NULL;
            const void* atkColl = NULL;
            int gCount = 0;
            int limit = collOpenParts(attack, kCollAttackParts, kCollAttackCollision, parts, calls, atkColl, gCount);
            for (int id = 0; id < limit; id++)
            {
                const void* part = collArrayAt(parts, calls, id);
                if (!game::isRam(part) || game::wordAt(part, 0) != kCollPartActive)
                {
                    continue;
                }
                const void* shape = collShapeOf(atkColl, gCount, id, game::wordAt(part, kPartGroup));
                if (!game::isRam(shape) || game::byteAt(shape, kCollShapeActive) != 1)
                {
                    continue;
                }
                float r = collFloatAt(shape, kCollShapeRadius);
                if (!(r > 0.0f && r < 500.0f))
                {
                    continue;
                }

                HeatmapEntry e;
                e.pos1[0] = collFloatAt(shape, 0);
                e.pos1[1] = collFloatAt(shape, 4);
                e.pos1[2] = collFloatAt(shape, 8);
                e.pos2[0] = collFloatAt(shape, kCollShapeBefore);
                e.pos2[1] = collFloatAt(shape, kCollShapeBefore + 4);
                e.pos2[2] = collFloatAt(shape, kCollShapeBefore + 8);
                e.radius = r;
                e.playerSlot = playerSlot;
                e.frameOffset = frameOffset;
                e.life = kHeatmapDefaultLife;
                e.active = 1;

                float effectiveR = r;
                if (game::wordAt(shape, kCollShapeVtable) == kCollCapsuleVtable)
                {
                    memcpy(e.scaleMatrix, reinterpret_cast<const void*>(reinterpret_cast<u32>(shape) + kCollCapsuleMatrix),
                           sizeof(e.scaleMatrix));
                    float sc = collFloatAt(shape, kCollCapsuleScale);
                    if (sc > 0.0f && sc < 10.0f)
                    {
                        effectiveR = r * sc;
                    }
                }
                else
                {
                    collIdentityMatrix34(e.scaleMatrix);
                }

                if (s_net != NULL)
                {
                    s_net->ring.push(e);
                    if (updateBounds && playerSlot < kHeatmapMaxFighters)
                    {
                        if (s_net->boundsOverwrite[playerSlot])
                        {
                            boundsReset(s_net->debug.bounds[playerSlot]);
                            s_net->boundsOverwrite[playerSlot] = false;
                        }
                        boundsAccumulateCapsule(s_net->debug.bounds[playerSlot], originX, originY, e.pos1, e.pos2,
                                                effectiveR);
                    }
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
                        if (slots[slot].wanted && s_net != NULL)
                        {
                            sampleAttackModule(m, static_cast<u8>(slot), s_net->framesAfterActionable[slot], true,
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
        if (s_net == NULL)
        {
            return;
        }
        s_net->ring.clear();
        for (int p = 0; p < kHeatmapMaxFighters; p++)
        {
            s_net->framesAfterActionable[p] = 0;
            s_net->boundsOverwrite[p] = true;
            s_net->playerWanted[p] = false;
            boundsReset(s_net->debug.bounds[p]);
        }
        s_net->debug.ringCount = 0;
        s_net->debug.drawCalls = 0;
        s_net->debug.boundsDrawn = 0;
        s_net->debug.resets++;
    }

    void heatmapAttach()
    {
        freeNetPtr(s_net);
        s_net = allocNetArray<HeatmapNet>(1);
        if (s_net == NULL)
        {
            return;
        }
        memset(&s_net->debug, 0, sizeof(s_net->debug));
        s_net->debug.magic = kDebugMagic;
        bool shapeOk = collShapeSupported();
        bool bubbleOk = collBubbleSupported();
        s_net->debug.shapeOk = shapeOk ? 1u : 0u;
        s_net->debug.bubbleOk = bubbleOk ? 1u : 0u;
        heatmapReset();
        s_net->debug.resets = 0;
        TM_LOG("heatmap: debug block at %08x; shape %s, bubble %s\n", &s_net->debug, shapeOk ? "ok" : "off",
               bubbleOk ? "ok" : "off");
    }

    void heatmapDetach()
    {
        heatmapReset();
        freeNetPtr(s_net);
    }

    void heatmapTick()
    {
        if (s_net == NULL)
        {
            return;
        }
        const main::DisplaySettings& d = main::settings().display;
        if (d.heatmap == display::kWhoNone)
        {
            bool anyBounds = false;
            for (int p = 0; p < kHeatmapMaxFighters; p++)
            {
                anyBounds = anyBounds || s_net->debug.bounds[p].valid != 0;
            }
            if (s_net->ring.count > 0 || anyBounds)
            {
                heatmapReset();
            }
            return;
        }

        if (s_net->ring.count > 0)
        {
            s_net->ring.tickAge();
        }

        FighterSlotInfo slots[kHeatmapMaxFighters];
        memset(slots, 0, sizeof(slots));
        for (int p = 0; p < kHeatmapMaxFighters; p++)
        {
            s_net->playerWanted[p] = false;
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
            s_net->playerWanted[p] = info.wanted;
            info.mainTaskId = game::isRam(refs[i].fighter) ? refs[i].fighter->m_taskId : 0;
            info.partnerTaskId = (refs[i].partner != NULL && game::isRam(refs[i].partner))
                                     ? refs[i].partner->m_taskId
                                     : 0;

            const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
            if (m != NULL)
            {
                int status = game::fighterStatus(m);
                float animFrame = game::isRam(m->m_motionModule) ? m->m_motionModule->getFrame() : 0.0f;
                s_net->framesAfterActionable[p] =
                    stepActionableGroundFrames(s_net->framesAfterActionable[p], status, animFrame);
                if (isHeatmapResetStatus(status, animFrame))
                {
                    s_net->boundsOverwrite[p] = true;
                }
                if (game::isRam(m->m_postureModule))
                {
                    info.originX = collFloatAt(m->m_postureModule, kPosturePos);
                    info.originY = collFloatAt(m->m_postureModule, kPosturePos + 4);
                }
                if (info.wanted && s_net->debug.shapeOk)
                {
                    sampleAttackModule(m, static_cast<u8>(p), s_net->framesAfterActionable[p], true, info.originX,
                                       info.originY);
                }
            }
            if (refs[i].partner != NULL && refs[i].partner != refs[i].fighter && info.wanted && s_net->debug.shapeOk)
            {
                const soModuleEnumeration* pm = game::fighterModules(refs[i].partner);
                if (pm != NULL)
                {
                    sampleAttackModule(pm, static_cast<u8>(p), s_net->framesAfterActionable[p], true, info.originX,
                                       info.originY);
                }
            }
        }

        if (s_net->debug.shapeOk)
        {
            sampleCategoryTasks(gfTask::Category_Weapon, StageObject_Weapon, d.heatmap, slots);
            sampleCategoryTasks(gfTask::Category_Item, StageObject_Item, d.heatmap, slots);
        }

        s_net->debug.ringCount = s_net->ring.count;
        s_net->debug.totalSampled = s_net->ring.totalPushed;
    }

    void heatmapDraw(const game::MatchState& match)
    {
        if (s_net == NULL)
        {
            return;
        }
        s_net->debug.drawCalls = 0;
        s_net->debug.boundsDrawn = 0;
        const main::DisplaySettings& d = main::settings().display;
        if (!match.running || match.paused || d.heatmap == display::kWhoNone)
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

        if (s_net->debug.bubbleOk && s_net->ring.count > 0)
        {
            int idx = static_cast<int>(s_net->ring.head);
            u16 drawn = 0;
            u16 total = s_net->ring.count;
            int lastOffset = -1;
            u32 c1 = 0;
            u32 c2 = 0;
            for (int k = 0; k < kHeatmapRing && drawn < total; k++)
            {
                const HeatmapEntry& e = s_net->ring.entries[idx];
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
                collDrawBubble(static_cast<double>(e.radius), e.scaleMatrix, e.pos1, e.pos2, c1, c2, viewMtx);
                drawn++;
                s_net->debug.drawCalls++;
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
                const HitboxBounds& b = s_net->debug.bounds[p];
                if (!b.valid || !s_net->playerWanted[p])
                {
                    continue;
                }
                gfx::lineBegin(GX_LINESTRIP, 5, kBoundsLineWidth);
                gfx::vertex(b.worldXMin, b.worldYMin, kBoundsColor);
                gfx::vertex(b.worldXMax, b.worldYMin, kBoundsColor);
                gfx::vertex(b.worldXMax, b.worldYMax, kBoundsColor);
                gfx::vertex(b.worldXMin, b.worldYMax, kBoundsColor);
                gfx::vertex(b.worldXMin, b.worldYMin, kBoundsColor);
                s_net->debug.boundsDrawn++;
            }
        }
    }

    const HitboxBounds* heatmapBounds(int playerNo)
    {
        if (s_net == NULL || playerNo < 0 || playerNo >= kHeatmapMaxFighters)
        {
            return NULL;
        }
        return &s_net->debug.bounds[playerNo];
    }
}
}
