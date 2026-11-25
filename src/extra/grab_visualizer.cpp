// Grab, Search / Tether, and Item-Pickup Visualizer for Project+ 3.2.1.
//
// Reads active collision shapes directly from P+ 3.2.1 fighter/weapon modules:
//   - Grab boxes: soCollisionCatchModuleImpl (+0x2C parts, +0xC0 soCollision, group 0)
//   - Search / Tether boxes: soCollisionSearchModuleImpl (+0x2C parts, +0xB8 soCollision,
//     part+0x58 group, +0xFC/+0x100 check interval)
//   - Item-pickup boxes: soAreaModuleImpl (+0x30 area instances, soAreaData+1 == 4
//     Fighter_Group_Item_Pickup, gfArea+0x1C bit 0x80 enabled)
//
// Renders capsules/spheres via stock P+ 3.2.1's kBubble (0x80541FA0, gated on
// 0x805483FF == 1) and rectangular item-pickup areas via GX_LINESTRIP, restoring
// camera GX state afterward. Zero heap allocation; zero work when disabled.
#include "extra/grab_visualizer.h"
#include "extra/pplus_collision.h"

#include <GX/GXTypes.h>
#include <ft/fighter.h>
#include <gf/gf_camera.h>
#include <gf/gf_task.h>
#include <gf/gf_task_scheduler.h>
#include <so/so_kind.h>
#include <so/so_module_accesser.h>
#include <so/stageobject.h>
#include <string.h>
#include <wn/weapon.h>

#include "main/display/display.h"
#include "main/game/fighters.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    namespace {
        enum {
            kDebugMagic = 0x544D4756,            // "TMGV"
            kAreaSetup = 0x807AE3F8,             // soAreaModuleImpl::setup (shared by ftAreaModuleImpl)
            kAreaSlotSetup = 0x28,
            kStageObjVptr = 0x3C,                // gfTask vptr (sizeof(gfTask) == 0x40)
            kSlotProcessAnim = 0x10,
            kStageObjProcessAnim = 0x8070F444,   // StageObject::processAnim
            kAccesserOffset = 0x60,
            kSearchPartGroup = 0x58,             // soCollisionSearchPart::m_groupIndex (88)
            kSearchInterval = 0xFC,              // soCollisionSearchModuleImpl::m_checkInterval (252)
            kSearchCounter = 0x100,              // soCollisionSearchModuleImpl::m_checkCounter (256)
            kAreaInstances = 0x30,               // soAreaModuleImpl::m_areaInstanceArray
            kAreaInstData = 0x00,                // soAreaInstance::m_areaData
            kAreaInstGf = 0x04,                  // soAreaInstance::m_gfArea
            kAreaInstFlags = 0x1D,               // soAreaInstance flags byte (0x807AE7CC)
            kAreaInstInitBit = 0x40,             // initialized bit (soAreaModuleImpl::update / setWhole)
            kAreaDataGroup = 0x01,               // soAreaData::m_group
            kAreaGroupItemPickup = 0x04,         // gfArea::Fighter_Group_Item_Pickup
            kGfAreaShapeType = 0x18,             // gfArea::m_shapeType (0 Rect, 1 Circle)
            kGfAreaFlags = 0x1C,                 // gfArea flags byte (bit 0x80 = enabled)
            kGfAreaEnabledBit = 0x80,
            kGfAreaPos = 0x20,                   // gfArea::m_pos (Vec2f)
            kGfAreaShapeData = 0x48,             // Rect: 4x Vec2f local corners; Circle: Vec2f offset + float r
            kGfAreaCircleRadius = 0x50,
            kCategoryLists = 0x58,               // gfTaskScheduler category list heads (0x8002EFE8)
            kTaskNext = 0x10,
            kTaskFlags = 0x2C,
            kTaskAlive = 0x40000000,
            kMaxAreas = 32,
            kMaxItemRects = 24,
            kMaxCategoryTasks = 96
        };

        const u8 kItemLineWidth = 18;

        // RAM telemetry block exposed for live Dolphin tests ("TMGV").
        struct GrabVizDebugBlock {
            u32 magic;            // 0x00 "TMGV"
            u32 shapeOk;          // 0x04
            u32 bubbleOk;         // 0x08
            u32 frame;            // 0x0C
            u32 grabShapes;       // 0x10 (current frame)
            u32 searchShapes;     // 0x14 (current frame)
            u32 itemShapes;       // 0x18 (current frame)
            u32 totalGrabDrawn;   // 0x1C
            u32 totalSearchDrawn; // 0x20
            u32 totalItemDrawn;   // 0x24
            u32 drawCalls;        // 0x28
        };

        struct FighterSlot {
            bool present;
            bool wanted;
            u32 mainTaskId;
            u32 partnerTaskId;
        };

        struct ItemRect {
            float xs[4];
            float ys[4];
        };

        GrabVizDebugBlock s_debug;

        const soModuleEnumeration* verifiedWeaponModules(const void* task, Weapon*& outWeapon)
        {
            outWeapon = NULL;
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
            if (obj->soGetKind() != StageObject_Weapon)
            {
                return NULL;
            }
            const soModuleAccesser* accesser =
                reinterpret_cast<const soModuleAccesser*>(game::wordAt(task, kAccesserOffset));
            // Weapon::getFounderTaskId (0x808E60FC) reads m_workManageModule off m_enumerationStart.
            if (!game::isRam(accesser) || !game::isRam(accesser->m_enumerationStart)
                || !game::isRam(accesser->m_enumerationStart->m_workManageModule))
            {
                return NULL;
            }
            outWeapon = static_cast<Weapon*>(obj);
            return accesser->m_enumerationStart;
        }

        int ownerSlotByTaskId(const FighterSlot slots[game::kMaxFighters], int taskId)
        {
            if (taskId <= 0)
            {
                return -1;
            }
            u32 tid = static_cast<u32>(taskId);
            for (int p = 0; p < game::kMaxFighters; p++)
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

        bool drawCollisionShape(const void* shape, u8 kind, const float viewMtx[3][4], bool& drewBubble)
        {
            if (!s_debug.bubbleOk)
            {
                return false;
            }
            u32 c1 = grabVizColor(kind);
            if (!collEmitShapeBubble(shape, c1, grabVizShadeColor(c1), viewMtx, drewBubble))
            {
                return false;
            }
            s_debug.drawCalls++;
            return true;
        }

        void drawCatchModule(const soModuleEnumeration* m, const float viewMtx[3][4], bool& drewBubble)
        {
            if (m == NULL || !s_debug.shapeOk || !game::isRam(m->m_collisionCatchModule))
            {
                return;
            }
            const void* catchMod = m->m_collisionCatchModule;
            const void* vt = reinterpret_cast<const void*>(game::wordAt(catchMod, 0));
            if (!game::isRam(vt) || game::wordAt(vt, kCollCatchSlotRenderDebug) != kCollCatchRenderDebug)
            {
                return;
            }
            const void* parts = NULL;
            const void* calls = NULL;
            const void* coll = NULL;
            int gCount = 0;
            int limit = collOpenParts(catchMod, kCollCatchParts, kCollCatchCollision, parts, calls, coll, gCount);
            for (int id = 0; id < limit; id++)
            {
                const void* part = collArrayAt(parts, calls, id);
                if (!game::isRam(part) || game::wordAt(part, 0) != kCollPartActive)
                {
                    continue;
                }
                const void* shape = collShapeOf(coll, gCount, id, 0);
                if (drawCollisionShape(shape, kGrabVizGrab, viewMtx, drewBubble))
                {
                    s_debug.grabShapes++;
                    s_debug.totalGrabDrawn++;
                }
            }
        }

        void drawSearchModule(const soModuleEnumeration* m, const float viewMtx[3][4], bool& drewBubble)
        {
            if (m == NULL || !s_debug.shapeOk || !game::isRam(m->m_collisionSearchModule))
            {
                return;
            }
            const void* searchMod = m->m_collisionSearchModule;
            const void* vt = reinterpret_cast<const void*>(game::wordAt(searchMod, 0));
            if (!game::isRam(vt) || game::wordAt(vt, kCollSearchSlotRenderDebug) != kCollSearchRenderDebug)
            {
                return;
            }
            u32 interval = game::wordAt(searchMod, kSearchInterval);
            if (interval != 0 && game::wordAt(searchMod, kSearchCounter) != interval)
            {
                return;
            }
            const void* parts = NULL;
            const void* calls = NULL;
            const void* coll = NULL;
            int gCount = 0;
            int limit = collOpenParts(searchMod, kCollSearchParts, kCollSearchCollision, parts, calls, coll, gCount);
            for (int id = 0; id < limit; id++)
            {
                const void* part = collArrayAt(parts, calls, id);
                if (!game::isRam(part) || game::wordAt(part, 0) != kCollPartActive)
                {
                    continue;
                }
                u32 group = game::wordAt(part, kSearchPartGroup);
                const void* shape = collShapeOf(coll, gCount, id, group);
                if (drawCollisionShape(shape, kGrabVizSearch, viewMtx, drewBubble))
                {
                    s_debug.searchShapes++;
                    s_debug.totalSearchDrawn++;
                }
            }
        }

        void collectItemAreas(const soModuleEnumeration* m, const float viewMtx[3][4], bool& drewBubble,
                              ItemRect rects[kMaxItemRects], int& rectCount)
        {
            if (m == NULL || !game::isRam(m->m_areaModule))
            {
                return;
            }
            const void* areaMod = m->m_areaModule;
            const void* vt = reinterpret_cast<const void*>(game::wordAt(areaMod, 0));
            if (!game::isRam(vt) || game::wordAt(vt, kAreaSlotSetup) != kAreaSetup)
            {
                return;
            }
            const void* instArr = reinterpret_cast<const void*>(game::wordAt(areaMod, kAreaInstances));
            const void* calls = game::isRam(instArr) ? reinterpret_cast<const void*>(game::wordAt(instArr, 0)) : NULL;
            if (!game::isRam(calls))
            {
                return;
            }
            int count = collArraySize(instArr, calls);
            if (count <= 0)
            {
                return;
            }
            for (int i = 0; i < count && i < kMaxAreas; i++)
            {
                const void* inst = collArrayAt(instArr, calls, i);
                if (!game::isRam(inst) || (game::byteAt(inst, kAreaInstFlags) & kAreaInstInitBit) == 0)
                {
                    continue;
                }
                const void* data = reinterpret_cast<const void*>(game::wordAt(inst, kAreaInstData));
                const void* gf = reinterpret_cast<const void*>(game::wordAt(inst, kAreaInstGf));
                if (!game::isRam(data) || !game::isRam(gf))
                {
                    continue;
                }
                if (game::byteAt(data, kAreaDataGroup) != kAreaGroupItemPickup)
                {
                    continue;
                }
                if ((game::byteAt(gf, kGfAreaFlags) & kGfAreaEnabledBit) == 0)
                {
                    continue;
                }
                u32 shapeType = game::wordAt(gf, kGfAreaShapeType);
                float px = collFloatAt(gf, kGfAreaPos);
                float py = collFloatAt(gf, kGfAreaPos + 4);
                if (shapeType == 0)
                {
                    if (rectCount >= kMaxItemRects)
                    {
                        continue;
                    }
                    ItemRect r;
                    for (int k = 0; k < 4; k++)
                    {
                        r.xs[k] = px + collFloatAt(gf, kGfAreaShapeData + 8u * static_cast<u32>(k));
                        r.ys[k] = py + collFloatAt(gf, kGfAreaShapeData + 8u * static_cast<u32>(k) + 4u);
                    }
                    if (grabVizValidRect(r.xs, r.ys))
                    {
                        rects[rectCount++] = r;
                    }
                }
                else if (shapeType == 1 && s_debug.bubbleOk)
                {
                    float cx = px + collFloatAt(gf, kGfAreaShapeData);
                    float cy = py + collFloatAt(gf, kGfAreaShapeData + 4);
                    float radius = collFloatAt(gf, kGfAreaCircleRadius);
                    if (grabVizValidCoord(cx) && grabVizValidCoord(cy) && grabVizValidRadius(radius))
                    {
                        float pos[3] = {cx, cy, 0.0f};
                        float scaleMtx[3][4];
                        collIdentityMatrix34(scaleMtx);
                        u32 c1 = grabVizColor(kGrabVizItem);
                        collDrawBubble(static_cast<double>(radius), scaleMtx, pos, pos, c1, grabVizShadeColor(c1),
                                       viewMtx);
                        drewBubble = true;
                        s_debug.itemShapes++;
                        s_debug.totalItemDrawn++;
                        s_debug.drawCalls++;
                    }
                }
            }
        }

        void drawWeaponModules(u8 who, const FighterSlot slots[game::kMaxFighters], const float viewMtx[3][4],
                               bool& drewBubble)
        {
            const void* scheduler = g_taskScheduler;
            if (!game::isRam(scheduler))
            {
                return;
            }
            const void* task = reinterpret_cast<const void*>(
                game::wordAt(scheduler, kCategoryLists + 4u * static_cast<u32>(gfTask::Category_Weapon)));
            for (int n = 0; n < kMaxCategoryTasks && game::isRam(task); n++)
            {
                Weapon* w = NULL;
                const soModuleEnumeration* m = verifiedWeaponModules(task, w);
                if (m != NULL && w != NULL)
                {
                    int ownerTaskId = w->getFounderTaskId();
                    int slot = ownerSlotByTaskId(slots, ownerTaskId);
                    bool drawIt = slot >= 0 ? slots[slot].wanted : (who == display::kWhoBoth);
                    if (drawIt)
                    {
                        drawCatchModule(m, viewMtx, drewBubble);
                        drawSearchModule(m, viewMtx, drewBubble);
                    }
                }
                task = reinterpret_cast<const void*>(game::wordAt(task, kTaskNext));
            }
        }
    }

    void grabVizAttach()
    {
        memset(&s_debug, 0, sizeof(s_debug));
        s_debug.magic = kDebugMagic;
        bool shapeOk = collShapeSupported();
        bool bubbleOk = collBubbleSupported();
        s_debug.shapeOk = shapeOk ? 1u : 0u;
        s_debug.bubbleOk = bubbleOk ? 1u : 0u;
        TM_LOG("grabviz: debug block at %08x; shape %s, bubble %s\n", &s_debug, shapeOk ? "ok" : "off",
               bubbleOk ? "ok" : "off");
    }

    void grabVizDetach()
    {
        s_debug.grabShapes = 0;
        s_debug.searchShapes = 0;
        s_debug.itemShapes = 0;
        s_debug.drawCalls = 0;
    }

    void grabVizDraw(const game::MatchState& match)
    {
        s_debug.grabShapes = 0;
        s_debug.searchShapes = 0;
        s_debug.itemShapes = 0;
        const main::DisplaySettings& d = main::settings().display;
        if (!match.running || match.paused || d.grabVisualizer == display::kWhoNone)
        {
            return;
        }
        gfCameraManager* camMgr = gfCameraManager::getManager();
        if (!game::isRam(camMgr))
        {
            return;
        }
        s_debug.frame = static_cast<u32>(game::trackerFrame());

        float viewMtx[3][4];
        memcpy(viewMtx, &camMgr->m_cameras[0], sizeof(viewMtx));

        FighterSlot slots[game::kMaxFighters];
        memset(slots, 0, sizeof(slots));
        ItemRect itemRects[kMaxItemRects];
        int itemRectCount = 0;
        bool drewBubble = false;

        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            int p = refs[i].playerNo;
            if (p < 0 || p >= game::kMaxFighters)
            {
                continue;
            }
            bool wanted = grabVizWhoMatches(d.grabVisualizer, refs[i].cpu);
            slots[p].present = true;
            slots[p].wanted = wanted;
            slots[p].mainTaskId = game::isRam(refs[i].fighter) ? refs[i].fighter->m_taskId : 0;
            slots[p].partnerTaskId = (refs[i].partner != NULL && game::isRam(refs[i].partner))
                                         ? refs[i].partner->m_taskId
                                         : 0;
            if (!wanted)
            {
                continue;
            }
            const soModuleEnumeration* m = game::fighterModules(refs[i].fighter);
            if (m != NULL)
            {
                drawCatchModule(m, viewMtx, drewBubble);
                drawSearchModule(m, viewMtx, drewBubble);
                collectItemAreas(m, viewMtx, drewBubble, itemRects, itemRectCount);
            }
            if (refs[i].partner != NULL && refs[i].partner != refs[i].fighter)
            {
                const soModuleEnumeration* pm = game::fighterModules(refs[i].partner);
                if (pm != NULL)
                {
                    drawCatchModule(pm, viewMtx, drewBubble);
                    drawSearchModule(pm, viewMtx, drewBubble);
                    collectItemAreas(pm, viewMtx, drewBubble, itemRects, itemRectCount);
                }
            }
        }

        drawWeaponModules(d.grabVisualizer, slots, viewMtx, drewBubble);

        // kBubble alters GX TEV/blend/matrix state; restore camera GX state
        // before drawing item-pickup line outlines or any subsequent pass.
        if (drewBubble)
        {
            camMgr->m_cameras[0].setGX();
            gfx::forgetState();
        }

        for (int i = 0; i < itemRectCount; i++)
        {
            const ItemRect& r = itemRects[i];
            gfx::lineBegin(GX_LINESTRIP, 5, kItemLineWidth);
            gfx::vertex(r.xs[0], r.ys[0], kGrabVizItemLineColor);
            gfx::vertex(r.xs[1], r.ys[1], kGrabVizItemLineColor);
            gfx::vertex(r.xs[2], r.ys[2], kGrabVizItemLineColor);
            gfx::vertex(r.xs[3], r.ys[3], kGrabVizItemLineColor);
            gfx::vertex(r.xs[0], r.ys[0], kGrabVizItemLineColor);
            s_debug.itemShapes++;
            s_debug.totalItemDrawn++;
            s_debug.drawCalls++;
        }
    }
}
}
