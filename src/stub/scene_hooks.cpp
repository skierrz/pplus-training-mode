// The scene hooks: just before a scene's start() and exit(), the loader
// loads or unloads that scene's modules.
#include "stub/scene_hooks.h"

#include <types.h>

#include "stub/hook.h"
#include "stub/hook_entry.h"
#include "stub/loader.h"

namespace tm {
namespace stub {
    // gfSceneManager::process (0x8002D1A0, RSBE01_02 symbol), r31 = the
    // manager. Read from memory on 3.2.1:
    //  - 0x8002D248..50 load the current scene (+0x4), its vtable and the
    //    start() slot (+0x8) for the bctrl at 0x8002D258. That runs once per
    //    scene, after the scene's modules have loaded.
    //  - 0x8002D288..90 do the same for exit() (+0x10), called once when
    //    processStep (+0x288) is 2, before changeNextScene (0x8002D020).
    // At both sites r0 and LR are dead (r0 is reloaded before its next use,
    // a bctrl follows); the hook entry preserves CR, CTR, XER and the FPRs.
    static const u32 kStartWords[] = {
        0x807F0004, // lwz r3,0x4(r31)
        0x81830004, // lwz r12,0x4(r3)
        0x818C0008  // lwz r12,0x8(r12)    start()
    };
    static const u32 kExitWords[] = {
        0x807F0004, // lwz r3,0x4(r31)
        0x81830004, // lwz r12,0x4(r3)
        0x818C0010  // lwz r12,0x10(r12)   exit()
    };
    static const hook::Site kStartSite = {"scene start", 0x8002D248, kStartWords, 3};
    static const hook::Site kExitSite = {"scene exit", 0x8002D288, kExitWords, 3};

    extern "C" void tmSceneStartBody()
    {
        loader::onSceneStart();
    }

    extern "C" void tmSceneExitBody()
    {
        loader::onSceneExit();
    }

    static asm void sceneStartEntry()
    {
        nofralloc
        lis r12, tmSceneStartBody@h
        ori r12, r12, tmSceneStartBody@l
        b tmPreservingCall
    }

    static asm void sceneExitEntry()
    {
        nofralloc
        lis r12, tmSceneExitBody@h
        ori r12, r12, tmSceneExitBody@l
        b tmPreservingCall
    }

    bool installSceneHooks(CoreApi* api)
    {
        // The exit hook goes in first: without it a loaded main module could
        // outlive its heap, so the start hook is only installed after it.
        if (!hook::installInline(api, kExitSite, sceneExitEntry))
        {
            return false;
        }
        return hook::installInline(api, kStartSite, sceneStartEntry);
    }
}
}
