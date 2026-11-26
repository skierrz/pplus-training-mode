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

        struct ItemRect {
            float xs[4];
            float ys[4];
        };

        GrabVizDebugBlock s_debug;

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

#pragma dont_inline on
        void drawCollModule(const void* mod, bool isSearch, const float viewMtx[3][4], bool& drewBubble)
        {
            if (!s_debug.shapeOk || !game::isRam(mod))
            {
                return;
            }
            const void* vt = reinterpret_cast<const void*>(game::wordAt(mod, 0));
            u32 slotOff = isSearch ? kCollSearchSlotRenderDebug : kCollCatchSlotRenderDebug;
            u32 renderFn = isSearch ? kCollSearchRenderDebug : kCollCatchRenderDebug;
            if (!game::isRam(vt) || game::wordAt(vt, slotOff) != renderFn)
            {
                return;
            }
            if (isSearch)
            {
                u32 interval = game::wordAt(mod, kSearchInterval);
                if (interval != 0 && game::wordAt(mod, kSearchCounter) != interval)
                {
                    return;
                }
            }
            const void* parts = NULL;
            const void* calls = NULL;
            const void* coll = NULL;
            int gCount = 0;
            int limit = collOpenParts(mod, isSearch ? kCollSearchParts : kCollCatchParts,
                                      isSearch ? kCollSearchCollision : kCollCatchCollision,
                                      parts, calls, coll, gCount);
            for (int id = 0; id < limit; id++)
            {
                const void* part = collArrayAt(parts, calls, id);
                if (!game::isRam(part) || game::wordAt(part, 0) != kCollPartActive)
                {
                    continue;
                }
                u32 group = isSearch ? game::wordAt(part, kSearchPartGroup) : 0u;
                const void* shape = collShapeOf(coll, gCount, id, group);
                if (drawCollisionShape(shape, isSearch ? kGrabVizSearch : kGrabVizGrab, viewMtx, drewBubble))
                {
                    if (isSearch)
                    {
                        s_debug.searchShapes++;
                        s_debug.totalSearchDrawn++;
                    }
                    else
                    {
                        s_debug.grabShapes++;
                        s_debug.totalGrabDrawn++;
                    }
                }
            }
        }
#pragma dont_inline reset

        void drawCatchAndSearch(const soModuleEnumeration* m, const float viewMtx[3][4], bool& drewBubble)
        {
            drawCollModule(m->m_collisionCatchModule, false, viewMtx, drewBubble);
            drawCollModule(m->m_collisionSearchModule, true, viewMtx, drewBubble);
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

        void drawWeaponModules(u8 who, const CollFighterSlot slots[game::kMaxFighters], const float viewMtx[3][4],
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
                StageObject* obj = NULL;
                const soModuleEnumeration* m = collVerifiedTaskModules(task, StageObject_Weapon, true, obj);
                if (m != NULL && obj != NULL)
                {
                    int ownerTaskId = static_cast<Weapon*>(obj)->getFounderTaskId();
                    int slot = collOwnerSlotByTaskId(slots, game::kMaxFighters, ownerTaskId);
                    bool drawIt = slot >= 0 ? slots[slot].wanted : (who == display::kWhoBoth);
                    if (drawIt)
                    {
                        drawCatchAndSearch(m, viewMtx, drewBubble);
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

        CollFighterSlot slots[game::kMaxFighters];
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
            Fighter* pair[2] = {refs[i].fighter, (refs[i].partner != refs[i].fighter) ? refs[i].partner : NULL};
            for (int k = 0; k < 2; k++)
            {
                const soModuleEnumeration* m = pair[k] != NULL ? game::fighterModules(pair[k]) : NULL;
                if (m != NULL)
                {
                    drawCatchAndSearch(m, viewMtx, drewBubble);
                    collectItemAreas(m, viewMtx, drewBubble, itemRects, itemRectCount);
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
