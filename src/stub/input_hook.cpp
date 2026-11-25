// The fighter input hook: hands each fighter's input object, after its
// update, to the loaded module's fighterInput callback.
#include "stub/input_hook.h"

#include <stddef.h>
#include <types.h>

#include "shared/game/mem.h"
#include "shared/interface.h"
#include "shared/log.h"
#include "stub/dispatch.h"
#include "stub/hook_entry.h"
#include "stub/module_hook.h"
#include "stub/perf_hooks.h"

namespace tm {
namespace stub {
    // soControllerImpl::update (sora_melee 0x80764DD0, r28 = the input
    // object, r30 = the controller) calls the input's update() through its
    // vtable at 0x80764E98, then its getters: main stick (from 0x80764E9C),
    // trigger, and button (P+'s Gecko hook at 0x80764F18 makes that call).
    // The site is right after the bctrl to update(); in a whole-frame
    // instruction trace nothing else read an input object's controls
    // (measured; setup not recorded). P+ hooks 0x80764E88, 0x80764F14 and
    // 0x80764F18 here (LegacyTE/Replays.asm, ProjectM/ButtonPresses.asm) and
    // nothing between 0x80764E8C and 0x80764EB0. r0 and LR are dead (a call
    // just returned; r12 and CTR are reloaded next); the hook entry
    // preserves CR, CTR, XER and the FPRs.
    static const u32 kSiteWords[] = {
        0x819C0004, // lwz r12,4(r28)    the input's vtable
        0x7F83E378, // mr r3,r28
        0x818C0010, // lwz r12,0x10(r12) getStickMain
        0x7D8903A6  // mtctr r12
    };
    // sora_melee is module 27; its .text started at 0x8070AA14 in every boot
    // logged so far (the site is given relative to it).
    static const hook::ModuleSite kSite = {"fighter input", 27, 0x80764E9C - 0x8070AA14, kSiteWords, 4};

    enum {
        kEntryId = 0x2C, // s32
        kAiIndex = 0x42  // u8
    };

    static bool s_seen = false; // a call reached the module since it attached

    extern "C" void tmFighterInputBody(void* input)
    {
        TM_PERF_SCOPE(kStubFighterInput);
        void (*callback)(const iface::FighterInput*) = dispatch::table().callbacks.fighterInput;
        if (callback == NULL || perfBypassed())
        {
            s_seen = false;
            return;
        }
        if (!game::isRam(input))
        {
            return;
        }
        if (!s_seen)
        {
            s_seen = true;
            TM_LOG("hook fighter input: first call with a module attached; site still ours: %s\n",
                   hook::moduleSiteInstalled(kSite) ? "yes" : "no");
        }
        iface::FighterInput call;
        call.input = input;
        call.entryId = static_cast<s32>(game::wordAt(input, kEntryId));
        call.aiIndex = game::byteAt(input, kAiIndex);
        callback(&call);
    }

    // Entered from sy_core's stub, which saved r3-r31 with stmw r3,0xC(r1)
    // (inline_stub checks that word): the site's r28 is at 0xC + 25 * 4.
    static asm void inputEntry()
    {
        nofralloc
        lwz r3, 0x70(r1)
        lis r12, tmFighterInputBody@h
        ori r12, r12, tmFighterInputBody@l
        b tmPreservingCall
    }

    bool installInputHook(CoreApi* api)
    {
        return hook::installInlineOnModuleLoad(api, kSite, inputEntry);
    }

    // Fighter::processFixCamera (sora_melee 0x8083A8B0, r30 = Fighter*): runs
    // setCursor/setNameCursor on both paths, then bl CameraController::getInstance
    // at 0x8083ABD8. At 0x8083ABDC, LR and CR0 are dead (0x8083ABE0 is
    // cmpwi r0, 11); r0 is live across the call (loaded by the replaced
    // lwz r0, 0x40(r3) and tested at 0x8083ABE0) and preserved by sy_core's
    // inline stub (repaired by inline_stub::checkAndFix). P+ hooks 0x8083AA54,
    // 0x8083ADE0 and 0x8083ADE4, leaving 0x8083ABDC-0x8083ABE4 untouched.
    static const u32 kCursorSiteWords[] = {
        0x80030040, // lwz r0,0x40(r3)
        0x2C00000B, // cmpwi r0,11
        0x41820190  // beq- 0x8083AD74
    };
    static const hook::ModuleSite kCursorSite = {"fighter cursor", 27, 0x8083ABDC - 0x8070AA14, kCursorSiteWords, 3};

    typedef char kCursorSlotOffsetCheck[offsetof(iface::Interface, callbacks)
                                            + offsetof(iface::Callbacks, fighterFixCamera) == 0x20
                                        ? 1
                                        : -1];

#ifdef TM_PERF
    extern "C" void tmFighterFixCameraBody(void* fighter)
    {
        void (*callback)(void*) = g_tmDispatchTable.callbacks.fighterFixCamera;
        if (callback != NULL && !perfBypassed())
        {
            callback(fighter);
        }
    }
#endif

    // Entered from sy_core's stub, which saved r3-r31 with stmw r3,0xC(r1):
    // r30 (Fighter* this) is at 0xC + 27 * 4 = 0x78. CR0 is dead at 0x8083ABDC,
    // so bailing out on callback == NULL here skips tmPreservingCall and its
    // FPR save/restore whenever no match module is loaded.
    static asm void cursorEntry()
    {
        nofralloc
        lis r12, g_tmDispatchTable@h
        ori r12, r12, g_tmDispatchTable@l
        lwz r12, 0x20(r12)
        cmpwi r12, 0
        beqlr
        lwz r3, 0x78(r1)
#ifdef TM_PERF
        lis r12, tmFighterFixCameraBody@h
        ori r12, r12, tmFighterFixCameraBody@l
#endif
        b tmPreservingCall
    }

    bool installCursorHook(CoreApi* api)
    {
        return hook::installInlineOnModuleLoad(api, kCursorSite, cursorEntry);
    }
}
}
