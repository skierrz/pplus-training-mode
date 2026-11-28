#include "main/display/debug_draw.h"

#include <gf/gf_camera.h>

#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// Draws hitboxes, hurtboxes, shields, ECBs and stage collision by calling
// P+ 3.2.1's renderDebug routines from the render hook: docs/internals.md,
// "Hitbox and collision drawing". Each entry is checked against its expected
// code first; a mismatch turns the option off.
namespace tm {
namespace display {
    enum {
        kAttackDraw = 0x8074BDB0,
        kHitDraw = 0x80750F58,
        kShieldColourHook = 0x807513F4,
        kEcbPatch1 = 0x80137850,
        kEcbPatch2 = 0x80137860,
        kDrawCollisionList = 0x80541F90,
        kCollisionList = 0x8049E57C,
        kDebugDataReady = 0x805483FF,
        kSlotHit = 0xD8,
        kSlotAttack = 0x120,
        kSlotShield = 0x88,
        kSlotEcb = 0x21C,
        kModGround = 0x10,
        kModAttack = 0x1C,
        kModHit = 0x20,
        kModShield = 0x24,
        kModReflector = 0x28,
        kModAbsorber = 0x2C,
        kSoraMeleeText = 0x8070AA14,
        kSoraMeleeEnd = 0x80A00000,
        kCodeStart = 0x80000000,
        kCodeEnd = 0x81000000,
        kMaxCollisionEntries = 512
    };

    // First words of the bodies the entries branch to (read in the emulator).
    static const u32 kAttackBody[] = {0x9421FFB0, 0x7C0802A6, 0x90010054, 0x93E1004C, 0x93C10048,
                                      0x93A10044, 0x93810040, 0x7C7C1B78, 0x3D808001, 0x618C9FA4};
    static const u32 kHitBody[] = {0x9421FFB0, 0x7C0802A6, 0x90010054, 0x93E1004C, 0x93C10048,
                                   0x93A10044, 0x93810040, 0x7C7C1B78, 0x7C9D2378};
    static const u32 kCollisionBody[] = {0x9421FFB0, 0x7C0802A6, 0x90010054, 0xBEE1002C, 0x7C771B78, 0x88A3002C};
    static const u32 kEcbPatchWords[] = {0x480000EC, 0x38A00000};

    static bool s_checked = false;
    static bool s_boxes = false;
    static bool s_collision = false;
    static bool s_ecb = false;
    static bool s_ecbChecked = false;

    static u32 word(u32 address)
    {
        return *reinterpret_cast<const u32*>(address);
    }

    // P+ 3.2.1's DebugFileLoader (HOOK @ 0x800B08A0) sets 0x805483FF to 1 once
    // /menu3/data.debug is loaded at 0x80548400 (capsule display lists + colours).
    static bool debugDataReady()
    {
        return *reinterpret_cast<volatile const u8*>(kDebugDataReady) == 1;
    }

    // The target of the unconditional relative branch at `address`, or 0.
    static u32 branchTarget(u32 address)
    {
        u32 w = word(address);
        if ((w & 0xFC000003) != 0x48000000)
        {
            return 0;
        }
        s32 offset = static_cast<s32>(w & 0x03FFFFFC);
        if (offset & 0x02000000)
        {
            offset -= 0x04000000;
        }
        return address + offset;
    }

    static bool wordsAt(u32 address, const u32* words, int count)
    {
        if (address < kCodeStart || address >= kCodeEnd)
        {
            return false;
        }
        for (int i = 0; i < count; i++)
        {
            if (word(address + 4 * i) != words[i])
            {
                return false;
            }
        }
        return true;
    }

    // A Gecko C2 code at `entry` whose body starts with `body`.
    static bool hookedWith(u32 entry, const u32* body, int count)
    {
        u32 target = branchTarget(entry);
        return target != 0 && wordsAt(target, body, count);
    }

    void debugDrawCheck()
    {
        s_checked = true;
        s_boxes = hookedWith(kAttackDraw, kAttackBody, sizeof(kAttackBody) / 4)
                  && hookedWith(kHitDraw, kHitBody, sizeof(kHitBody) / 4) && branchTarget(kShieldColourHook) != 0;
        s_collision = hookedWith(kDrawCollisionList, kCollisionBody, sizeof(kCollisionBody) / 4)
                      && wordsAt(kEcbPatch1, kEcbPatchWords, 1) && wordsAt(kEcbPatch2, kEcbPatchWords + 1, 1);
        s_ecbChecked = false;
        TM_LOG("display: Project+ debug draw: boxes %s, collision %s\n", s_boxes ? "ok" : "not as expected: off",
               s_collision ? "ok" : "not as expected: off");
    }

    bool boxesAvailable()
    {
        if (!s_checked)
        {
            debugDrawCheck();
        }
        return s_boxes && debugDataReady();
    }

    bool collisionAvailable()
    {
        if (!s_checked)
        {
            debugDrawCheck();
        }
        return s_collision && debugDataReady();
    }

    // Calls fn(object) with r31 set to `r31Value` for the call.
    static asm void callWithR31(register void* object, register const void* r31Value, register u32 fn)
    {
        nofralloc
        stwu r1, -0x20(r1)
        mflr r0
        stw r0, 0x24(r1)
        stw r31, 0x1C(r1)
        mr r31, r4
        mtctr r5
        bctrl
        lwz r31, 0x1C(r1)
        lwz r0, 0x24(r1)
        mtlr r0
        addi r1, r1, 0x20
        blr
    }

    // The function in vtable slot `slot` of the object at `module`, whose
    // vtable pointer is at `vtableAt`; 0 if anything is not in RAM.
    static u32 slotOf(const void* module, u32 vtableAt, u32 slot)
    {
        if (!game::isRam(module))
        {
            return 0;
        }
        const void* vtable = reinterpret_cast<const void*>(game::wordAt(module, vtableAt));
        if (!game::isRam(vtable))
        {
            return 0;
        }
        return game::wordAt(vtable, slot);
    }

    static void* moduleAt(const void* modules, u32 offset)
    {
        void* m = reinterpret_cast<void*>(game::wordAt(modules, offset));
        return game::isRam(m) ? m : NULL;
    }

    void debugDrawBegin()
    {
        gfCameraManager::getManager()->m_cameras[0].setGX();
    }

    void debugDrawEnd()
    {
    }

    void drawBoxes(Fighter* fighter)
    {
        const soModuleEnumeration* modules = game::fighterModules(fighter);
        if (!boxesAvailable() || modules == NULL)
        {
            return;
        }
        const void* accesser = reinterpret_cast<const void*>(game::wordAt(fighter, 0x60));
        void* hit = moduleAt(modules, kModHit);
        void* attack = moduleAt(modules, kModAttack);
        if (hit != NULL && slotOf(hit, 0, kSlotHit) == kHitDraw)
        {
            callWithR31(hit, accesser, kHitDraw);
        }
        static const u32 kShields[] = {kModShield, kModReflector, kModAbsorber};
        for (int i = 0; i < 3; i++)
        {
            void* m = moduleAt(modules, kShields[i]);
            u32 fn = slotOf(m, 0, kSlotShield);
            if (m != NULL && fn >= kCodeStart && fn < kCodeEnd)
            {
                callWithR31(m, accesser, fn);
            }
        }
        if (attack != NULL && slotOf(attack, 0, kSlotAttack) == kAttackDraw)
        {
            bool colored = main::settings().display.hitboxColorMode != main::kHitboxColorDefault
                           && main::extraDrawColoredHitboxes(fighter);
            if (!colored)
            {
                callWithR31(attack, accesser, kAttackDraw);
            }
        }
    }

    void drawEcb(Fighter* fighter)
    {
        const soModuleEnumeration* modules = game::fighterModules(fighter);
        if (!collisionAvailable() || modules == NULL)
        {
            return;
        }
        void* ground = moduleAt(modules, kModGround);
        u32 fn = slotOf(ground, 8, kSlotEcb);
        if (!s_ecbChecked)
        {
            // The patched routine holds both of P+'s patch sites.
            s_ecbChecked = true;
            s_ecb = fn >= kSoraMeleeText && fn < kSoraMeleeEnd;
            TM_LOG("display: ECB routine %08x %s\n", fn, s_ecb ? "ok" : "not as expected: off");
        }
        if (s_ecb && ground != NULL && fn != 0)
        {
            const void* accesser = reinterpret_cast<const void*>(game::wordAt(fighter, 0x60));
            callWithR31(ground, accesser, fn);
        }
    }

    void drawStageCollision()
    {
        if (!collisionAvailable())
        {
            return;
        }
        u32 sentinel = kCollisionList + 4;
        u32 node = word(sentinel);
        for (int n = 0; n < kMaxCollisionEntries && node != sentinel; n++)
        {
            if (!game::isRam(reinterpret_cast<const void*>(node)))
            {
                return;
            }
            if (*reinterpret_cast<const u8*>(node - 4) & 0x80)
            {
                callWithR31(reinterpret_cast<void*>(node - 48), NULL, kDrawCollisionList);
            }
            node = word(node);
        }
    }
}
}
