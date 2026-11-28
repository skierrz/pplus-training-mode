// Bit names from ProjectPunch's common.h (MIT; see THIRD_PARTY_NOTICES.md);
// details: docs/internals.md, "Fighter body values".
#include "main/game/body.h"

#include <so/so_module_accesser.h>
#ifdef __MWERKS__
#include <so/controller/so_controller_module_impl.h>
#endif

#include "main/game/fighters.h"
#include "main/game/motion.h"
#include "shared/game/mem.h"

// Values of one fighter read from its modules; reads only. Where each comes
// from: docs/internals.md, "Fighter body values".
namespace tm {
namespace game {
    enum {
        kPosturePos = 0xC,
        kPosturePrevPos = 0x18,
        kHitGroupArray = 0x58,
        kGroupInArray = 0xC,
        kGroupPosX = 0x8,
        kGroupWhole = 0x1C,
        kGroupGlobal = 0x20,
        kGroupXlu = 0x24,
        kGroupInvincible = 0x28,
        kWorkInstance = 0x20,
        kWorkStatus = 0x24,
        kWorkInts = 0xC,
        kWorkIntCount = 0x10,
        kWorkFloats = 0x14,
        kWorkFloatCount = 0x18,
        kWorkFlags = 0x1C,
        kWorkFlagCount = 0x20,
        kLaCStickY = 35,
        kLCancelInt = 90,
        kExtraInfoInt = 180,
        kPowershieldWord = 3 // LA bit 96
    };

    static float floatAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
    }

    static const void* work(const soModuleEnumeration* m, u32 which)
    {
        if (!isRam(m->m_workManageModule))
        {
            return NULL;
        }
        const void* w = reinterpret_cast<const void*>(wordAt(m->m_workManageModule, which));
        return isRam(w) ? w : NULL;
    }

    static u32 flagWord(const soModuleEnumeration* m, u32 index, u32 which = kWorkStatus)
    {
        const void* w = work(m, which);
        if (w == NULL || index >= wordAt(w, kWorkFlagCount))
        {
            return 0;
        }
        const u32* words = reinterpret_cast<const u32*>(wordAt(w, kWorkFlags));
        return isRam(words) ? words[index] : 0;
    }

    s32 instanceInt(const soModuleEnumeration* m, u32 index)
    {
        const void* w = work(m, kWorkInstance);
        if (w == NULL || index >= wordAt(w, kWorkIntCount))
        {
            return 0;
        }
        const s32* ints = reinterpret_cast<const s32*>(wordAt(w, kWorkInts));
        return isRam(ints) ? ints[index] : 0;
    }

#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    const void* extraInfo(const soModuleEnumeration* m)
    {
        const void* w = work(m, kWorkInstance);
        const u32* ints = w != NULL ? reinterpret_cast<const u32*>(wordAt(w, kWorkInts)) : NULL;
        const void* p = isRam(ints) ? reinterpret_cast<const void*>(ints[kExtraInfoInt]) : NULL;
        return isRam(p) ? p : NULL;
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

    void readWorkValues(const soModuleEnumeration* m, u32& flags, s32& lCancel)
    {
        flags = flagWord(m, 0);
        lCancel = instanceInt(m, kLCancelInt);
    }

    // Brawl soCollisionHitGroup::getTotalStatus (0x8074D2F4) and ::update
    // (0x8074CDEC) take max(0, m_whole, m_global). HitStatus values
    // (updateGlobal 0x8074CF80, update 0x8074CE1C): 0 = Normal, 1 = Invincible,
    // 2 = Intangible (XLU), 3 = None (hurtbox group disabled). State 3 disables
    // hurtboxes rather than granting invincibility or intangibility.
    static inline u8 hitGroupIntangible(u32 whole, u32 global)
    {
        s32 total = 0;
        s32 w = static_cast<s32>(whole);
        s32 g = static_cast<s32>(global);
        if (w > total)
        {
            total = w;
        }
        if (g > total)
        {
            total = g;
        }
        return static_cast<u8>((total == 1 || total == 2) ? 1 : 0);
    }

    bool readBody(Fighter* fighter, Body& out)
    {
        const soModuleEnumeration* m = fighterModules(fighter);
        if (m == NULL || !isRam(m->m_postureModule))
        {
            return false;
        }
        const void* posture = m->m_postureModule;
        out.x = floatAt(posture, kPosturePos);
        out.y = floatAt(posture, kPosturePos + 4);
        out.vy = out.y - floatAt(posture, kPosturePrevPos + 4);

        out.stickX = 0.0f;
        out.stickY = 0.0f;
        out.cStickY = 0.0f;
        out.buttons = 0;
        out.triggerButtons = 0;
        out.clatter = 0.0f;
        const void* inst = work(m, kWorkInstance);
        if (inst != NULL && kLaCStickY < wordAt(inst, kWorkFloatCount))
        {
            const float* floats = reinterpret_cast<const float*>(wordAt(inst, kWorkFloats));
            if (isRam(floats))
            {
                float cy = floats[kLaCStickY];
                if (cy >= -1.5f && cy <= 1.5f)
                {
                    out.cStickY = cy;
                }
            }
        }
#ifdef __MWERKS__
        soControllerModule* c = m->m_controllerModule;
        if (isRam(c))
        {
            float sx = c->getStickX();
            float sy = c->getStickY();
            if (sx >= -1.5f && sx <= 1.5f && sy >= -1.5f && sy <= 1.5f)
            {
                out.stickX = sx;
                out.stickY = sy;
            }
            if (out.cStickY == 0.0f)
            {
                float subY = c->getSubStickY();
                if (subY >= -1.5f && subY <= 1.5f)
                {
                    out.cStickY = subY;
                }
            }
            out.buttons = c->getButton().m_mask;
            out.triggerButtons = c->getTrigger().m_mask;
            out.clatter = c->getClatterTime(0);
        }
#endif

        out.xluFrames = 0;
        out.invFrames = 0;
        out.intangible = 0;
        const soCollisionHitModule* hit = m->m_collisionHitModule;
        if (isRam(hit))
        {
            const void* array = reinterpret_cast<const void*>(wordAt(hit, kHitGroupArray));
            if (isRam(array))
            {
                const void* group = reinterpret_cast<const void*>(reinterpret_cast<u32>(array) + kGroupInArray);
                float gx = floatAt(group, kGroupPosX);
                if (gx - out.x < 0.5f && out.x - gx < 0.5f)
                {
                    out.xluFrames = static_cast<s32>(wordAt(group, kGroupXlu));
                    u32 inv = wordAt(group, kGroupInvincible);
                    out.invFrames = static_cast<u8>(inv > 255 ? 255 : inv);
                    out.intangible = hitGroupIntangible(wordAt(group, kGroupWhole), wordAt(group, kGroupGlobal));
                }
            }
        }

        u32 w0 = flagWord(m, 0);
        out.shortHop = (w0 >> 6) & 1;
        out.landingLag = (w0 >> 30) & 1;
        out.fastfall = (w0 >> 2) & 1;
        out.lCancel = instanceInt(m, kLCancelInt);
        out.powershield = flagWord(m, kPowershieldWord, kWorkInstance) & 1;

        MotionInfo motion;
        if (readMotion(fighter, motion))
        {
            out.motionKind = motion.kind;
            out.motionFrame = motion.frame;
        }
        else
        {
            out.motionKind = -1;
            out.motionFrame = 0;
        }
        return true;
    }
}
}
