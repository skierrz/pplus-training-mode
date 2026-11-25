// Boot to Training. P+'s "Boot Directly to CSS" (Source/Project+/BootToCSS.asm,
// hook at 0x806DD5F8 in sqBoot::setNext) picks the first sequence from the
// buttons held: L/R Training, Start the title, Z its custom target, nothing
// Versus. Where it picked Versus we pick Training instead, unless X or Y is
// held on a GameCube controller, and give the menu to a pad as its L/R path
// does. Changes gmSetRule's menu pad (+0x24) on that boot only.
#include "stub/boot_target.h"

#include <gf/gf_pad_system.h>
#include <gm/gm_global.h>
#include <string.h>
#include <types.h>

#include "shared/game/mem.h"
#include "shared/log.h"
#include "stub/hook_entry.h"
#include "stub/module_hook.h"

namespace tm {
namespace stub {
    // sqBoot::setNext (sora_scene 0x806DD490). P+'s hook returns to
    // 0x806DD5FC with r4 = the sequence's name and r5 its parameter, which
    // the bl setNextSequence at 0x806DD610 takes. r0 and LR are dead (r0 is
    // reloaded at 0x806DD604; the bl follows).
    static const u32 kSiteWords[] = {
        0x80C60000, // lwz r6,0(r6)
        0x88060028, // lbz r0,0x28(r6)
        0x60000040, // ori r0,r0,0x40
        0x98060028  // stb r0,0x28(r6)
    };
    // sora_scene is module 1; its .text started at 0x806BB554 in every boot
    // logged so far (the site is given relative to it).
    static const hook::ModuleSite kSite = {"boot target", 1, 0x806DD600 - 0x806BB554, kSiteWords, 4};

    enum {
        // The names' offsets in sora_scene's data (BootToCSS.asm:9-11).
        kVersusName = 0x1B54,
        kTrainingName = 0x1870,
        kPads = 8,               // 0-3 GameCube, 4-7 Wii
        kVersusHold = gfPadButtons::X | gfPadButtons::Y,
        kMenuPad = 0x24          // gmSetRule: read by 0x80052DB0, written by P+'s L/R path
    };

    // `saved` is the inline stub's copy of r3-r31; r4 is restored from it.
    extern "C" void tmBootTargetBody(u32* saved)
    {
        const char* next = reinterpret_cast<const char*>(saved[1]);
        if (!game::isRam(next) || strcmp(next, "sqVsMelee") != 0)
        {
            return;
        }
        const char* training = next - kVersusName + kTrainingName;
        gfPadSystem* pads = g_gfPadSystem;
        if (strcmp(training, "sqTraining") != 0 || !game::isRam(pads) || !game::isRam(g_GameGlobal)
            || !game::isRam(g_GameGlobal->m_setRule))
        {
            return;
        }
        // The menu goes to the first pad without an error, as Brawl's own
        // menu-pad setter picks (0x800BA2E8), less its A/Start check.
        s32 menuPad = -1;
        for (int i = 0; i < kPads; i++)
        {
            gfPadStatus status;
            pads->getSysPadStatus(i, &status);
            if (status.m_error != gfPadError::NONE)
            {
                continue;
            }
            // GameCube controllers only: the Wii pads' X/Y bits are unread.
            if (i < 4 && (status.m_buttonsCurrentFrame.bits & kVersusHold) != 0)
            {
                TM_LOG("boot: X/Y held on pad %d, Versus\n", i);
                return;
            }
            menuPad = menuPad < 0 ? i : menuPad;
        }
        menuPad = menuPad < 0 ? 0 : menuPad;
        saved[1] = reinterpret_cast<u32>(training);
        *reinterpret_cast<s32*>(reinterpret_cast<u8*>(g_GameGlobal->m_setRule) + kMenuPad) = menuPad;
        TM_LOG("boot: Training, menu on pad %d\n", menuPad);
    }

    static asm void bootTargetEntry()
    {
        nofralloc
        addi r3, r1, 0xC
        lis r12, tmBootTargetBody@h
        ori r12, r12, tmBootTargetBody@l
        b tmPreservingCall
    }

    void installBootTarget(CoreApi* api)
    {
        hook::installInlineOnModuleLoad(api, kSite, bootTargetEntry);
    }
}
}
