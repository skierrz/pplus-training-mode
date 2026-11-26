#include "extra/pplus_recovery.h"

#include <gf/gf_task.h>
#include <gf/gf_task_scheduler.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <so/work/so_work_manage_module_impl.h>
#include <wn/weapon.h>

#include "extra/pplus_recovery_core.h"
#include "extra/stage_geom.h"
#include "main/cpu/driver.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/motion.h"
#include "main/settings.h"
#include "shared/game/frame.h"
#include "shared/game/mem.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    enum {
        kWorkInstance = 0x20,
        kWorkInts = 0x0C,
        kWorkIntCount = 0x10,
        kWorkFloats = 0x14,
        kWorkFloatCount = 0x18,
        kWorkFlags = 0x1C,
        kWorkFlagCount = 0x20,
        kAccesserOffset = 0x60,
        kPosturePos = 0x0C,
        kPosturePrevPos = 0x18,
        kJumpCountBasic = 0x10000001,
        kJumpCountMaxParam = 23003,
        kCategoryLists = 0x58, // gfTaskScheduler category list heads (cpu/view.cpp:668)
        kTaskNext = 0x10,
        kTaskFlags = 0x2C,
        kTaskAlive = 0x40000000,
        kStageObjVptr = 0x3C,
        kSlotProcessAnim = 0x10,
        kStageObjProcessAnim = 0x8070F444,
        kMaxWeaponWalk = 96,
        kCliffFirst = 0x73,
        kCliffLast = 0x7B
    };

    static RecoveryState s_states[cpu::kMaxSlots];
    static u32 s_rng = 0xA341316Cu;

    static float floatAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
    }

    static bool safeReadLaBit(const soModuleEnumeration* m, u32 idx)
    {
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return false;
        }
        const void* w = reinterpret_cast<const void*>(game::wordAt(m->m_workManageModule, kWorkInstance));
        if (!game::isRam(w))
        {
            return false;
        }
        u32 wordIdx = idx >> 5;
        u32 bitIdx = idx & 31u;
        if (wordIdx >= game::wordAt(w, kWorkFlagCount))
        {
            return false;
        }
        const u32* flags = reinterpret_cast<const u32*>(game::wordAt(w, kWorkFlags));
        return game::isRam(flags) && ((flags[wordIdx] >> bitIdx) & 1u) != 0;
    }

    static int safeReadLaInt(const soModuleEnumeration* m, u32 idx)
    {
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return 0;
        }
        const void* w = reinterpret_cast<const void*>(game::wordAt(m->m_workManageModule, kWorkInstance));
        if (!game::isRam(w) || idx >= game::wordAt(w, kWorkIntCount))
        {
            return 0;
        }
        const s32* ints = reinterpret_cast<const s32*>(game::wordAt(w, kWorkInts));
        return game::isRam(ints) ? ints[idx] : 0;
    }

    static float safeReadLaFloat(const soModuleEnumeration* m, u32 idx)
    {
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return 0.0f;
        }
        const void* w = reinterpret_cast<const void*>(game::wordAt(m->m_workManageModule, kWorkInstance));
        if (!game::isRam(w) || idx >= game::wordAt(w, kWorkFloatCount))
        {
            return 0.0f;
        }
        const float* floats = reinterpret_cast<const float*>(game::wordAt(w, kWorkFloats));
        return game::isRam(floats) ? floats[idx] : 0.0f;
    }

    static int remainingJumps(Fighter* f, const soModuleEnumeration* m)
    {
        if (m == NULL || !game::isRam(m->m_workManageModule))
        {
            return 0;
        }
        soModuleAccesser* accesser =
            *reinterpret_cast<soModuleAccesser**>(reinterpret_cast<u32>(f) + kAccesserOffset);
        if (!game::isRam(accesser))
        {
            return 0;
        }
        int used = m->m_workManageModule->getInt(kJumpCountBasic);
        int maxJ = soValueAccesser::getConstantInt(accesser, kJumpCountMaxParam, 0);
        int rem = maxJ - used;
        return rem > 0 ? rem : 0;
    }

    static bool isLedgeOccupiedByOther(int selfEntryId, float ledgeX, float ledgeY)
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            if (refs[i].entryId == selfEntryId)
            {
                continue;
            }
            game::FighterState st;
            if (!game::readState(refs[i], st))
            {
                continue;
            }
            if (st.status >= kCliffFirst && st.status <= kCliffLast)
            {
                float dx = st.x - ledgeX;
                float dy = st.y - ledgeY;
                if (dx * dx + dy * dy < 400.0f)
                {
                    return true;
                }
            }
        }
        return false;
    }

    // Walks g_taskScheduler's gfTask::Category_Weapon list to locate an active
    // PK Thunder weapon founded by `founderTaskId` (Ness / Lucas recovery).
    static bool findPkThunderWeapon(int founderTaskId, float& outX, float& outY, float& outVx, float& outVy)
    {
        const void* sched = g_taskScheduler;
        if (!game::isRam(sched) || founderTaskId < 0)
        {
            return false;
        }
        const void* heads = reinterpret_cast<const void*>(reinterpret_cast<u32>(sched) + kCategoryLists);
        void* task = reinterpret_cast<void*>(game::wordAt(heads, gfTask::Category_Weapon * 4));
        for (int i = 0; i < kMaxWeaponWalk && game::isRam(task); i++)
        {
            void* next = reinterpret_cast<void*>(game::wordAt(task, kTaskNext));
            if ((game::wordAt(task, kTaskFlags) & kTaskAlive) != 0)
            {
                const void* vt = reinterpret_cast<const void*>(game::wordAt(task, kStageObjVptr));
                if (game::isRam(vt) && game::wordAt(vt, kSlotProcessAnim) == kStageObjProcessAnim)
                {
                    StageObject* obj = reinterpret_cast<StageObject*>(task);
                    if (obj->soGetKind() == StageObject_Weapon)
                    {
                        Weapon* wn = reinterpret_cast<Weapon*>(task);
                        // Filter specifically for PK Thunder (art_ness_uspecial = 2, art_lucas_uspecial = 2).
                        if (wn->getFounderTaskId() == founderTaskId && wn->getArticleId() == 2)
                        {
                            const soModuleAccesser* acc =
                                reinterpret_cast<const soModuleAccesser*>(game::wordAt(task, kAccesserOffset));
                            if (game::isRam(acc) && game::isRam(acc->m_enumerationStart)
                                && game::isRam(acc->m_enumerationStart->m_postureModule))
                            {
                                const void* posture = acc->m_enumerationStart->m_postureModule;
                                outX = floatAt(posture, kPosturePos);
                                outY = floatAt(posture, kPosturePos + 4);
                                outVx = outX - floatAt(posture, kPosturePrevPos);
                                outVy = outY - floatAt(posture, kPosturePrevPos + 4);
                                return true;
                            }
                        }
                    }
                }
            }
            task = next;
        }
        return false;
    }

    void pplusRecoveryAttach()
    {
        for (int i = 0; i < cpu::kMaxSlots; i++)
        {
            recoveryStateReset(s_states[i]);
        }
        s_rng = game::frameCounter() | 1u;
    }

    void pplusRecoveryReset(int slot)
    {
        if (slot >= 0 && slot < cpu::kMaxSlots)
        {
            recoveryStateReset(s_states[slot]);
        }
    }

    bool pplusRecoveryStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons)
    {
        if (who.slot < 0 || who.slot >= cpu::kMaxSlots || f == NULL)
        {
            return false;
        }
        const soModuleEnumeration* m = game::fighterModules(f);
        if (m == NULL)
        {
            return false;
        }

        StageGeometry geom;
        if (!queryStageGeometry(geom) || !geom.hasLedges)
        {
            return false;
        }
        LedgeInfo ledge;
        if (!nearestLedge(geom, v.x, v.y, ledge))
        {
            return false;
        }

        int ftKind = game::fighterKind(who.entryId, who.sub);
        const FighterRecoverySpec& spec = recoverySpec(ftKind);

        RecoveryInput in;
        in.ftKind = ftKind;
        in.status = v.status;
        in.subaction = -1;
        in.animFrame = v.motionFrame;
        game::MotionInfo mi;
        if (game::readMotion(f, mi))
        {
            in.subaction = mi.kind;
            in.animFrame = static_cast<float>(mi.frame);
        }
        in.x = v.x;
        in.y = v.y;
        in.vx = v.vx;
        in.vy = v.vy;
        in.lr = v.lr;
        in.ledgeX = ledge.x;
        in.ledgeY = ledge.y;
        in.ledgeDir = ledge.dir;
        in.stageLeftX = geom.floor.leftX;
        in.stageRightX = geom.floor.rightX;
        in.stageY = geom.floor.centerY;
        in.blastBottom = geom.blast.bottom;
        in.air = v.air;
        in.canAct = cpu::canAct(v);
        in.numJumps = remainingJumps(f, m);
        in.jumpsLeft = in.numJumps > 0;
        in.ledgeOccupied = isLedgeOccupiedByOther(who.entryId, ledge.x, ledge.y);
        in.ceilingAbove = rayCheckCeiling(v.x, v.y + 6.0f, 150.0f);

        float towardDir = ledge.x >= v.x ? 1.0f : -1.0f;
        in.wallDist = 100.0f;
        in.wallAhead = rayCheckWall(v.x, v.y + 6.0f, towardDir, 45.0f, &in.wallDist);

        float floorY = 0.0f;
        in.floorBelow = rayCheckGround(v.x, v.y + 4.0f, 120.0f, floorY, true);
        in.yDistFloor = in.floorBelow ? (v.y - floorY) : -1.0f;

        // Fighter-specific LA resource reads (bounds-checked).
        in.laFlag = false;
        in.laInt = 0;
        in.laFloat = 0.0f;
        if (spec.laResourceIdx != 0)
        {
            if (ftKind == 0x21) // Lucario: Aura float 0x1B, DownB bit 0x42
            {
                in.laFloat = safeReadLaFloat(m, spec.laResourceIdx);
                in.laFlag = safeReadLaBit(m, 0x42);
            }
            else if (ftKind == 0x26) // Mewtwo: hover-used bit 0x73 (active hover is LA-Bit[97] / status 0x120)
            {
                in.laFlag = safeReadLaBit(m, 0x73);
            }
            else if (ftKind == 0x22 || ftKind == 0x15 || ftKind == 0x14 || ftKind == 0x2D)
            {
                in.laInt = safeReadLaInt(m, spec.laResourceIdx);
            }
            else
            {
                in.laFlag = safeReadLaBit(m, spec.laResourceIdx);
            }
        }

        // PK Thunder weapon search for Ness (0x0A) and Lucas (0x1A).
        in.hasPkt = false;
        in.pktX = in.pktY = in.pktVx = in.pktVy = 0.0f;
        if ((spec.flags & kRfPkThunder) != 0 && v.status >= 0x114 && v.status <= 0x11C)
        {
            int founderTaskId = reinterpret_cast<const gfTask*>(f)->getTaskId();
            in.hasPkt = findPkThunderWeapon(founderTaskId, in.pktX, in.pktY, in.pktVx, in.pktVy);
        }
        in.lastButtons = lastButtons;
        in.recoveryMixup = main::settings().comboTrainer.recoveryMixup;

        s_rng ^= game::frameCounter() * 0x9E3779B9u;
        RecoveryOutput recOut;
        stepRecovery(in, s_states[who.slot], s_rng, recOut);
        if (!recOut.active)
        {
            return false;
        }
        out.buttons = recOut.buttons;
        out.x = recOut.stickX;
        out.y = recOut.stickY;
        return true;
    }
}
}
