// The pad hooks: three inline hooks in the game's pad code that hand the
// system pads, the game pads and each fighter's pad copy to the loaded
// modules (dispatch.cpp) before the game uses them.
#include "stub/pad_hooks.h"

#include <gf/gf_pad_system.h>
#include <types.h>

#include "shared/game/mem.h"
#include "shared/interface.h"
#include "stub/dispatch.h"
#include "stub/hook.h"
#include "stub/hook_entry.h"
#include "stub/perf_hooks.h"

namespace tm {
namespace stub {
    // gfPadSystem::updateSystem (0x8002A210, RSBE01_02 symbol), r31 = this,
    // read in memory on 3.2.1: 0x8002A240..68 copy each system pad into its
    // debug pad (+0x244; fn_80028A08 recomputes held/pressed/released, with
    // interrupts off until 0x8002A270). From 0x8002A274 the debug pads build
    // the menu pads (+0x644), hold-repeat and the merged pads, so a change
    // made there reaches every pad the menus read. r0 is reloaded before its
    // next use, LR is overwritten by the loop's bl; the hook entry preserves
    // CR, CTR, XER and the FPRs.
    static const u32 kSystemWords[] = {
        0x3B9F0644, // addi r28,r31,0x644
        0x3BBF0244, // addi r29,r31,0x244
        0x3B600000  // li r27,0
    };
    // gfPadSystem::updateGame (0x8002A4F8), r31 = this: both paths (pad
    // queue or system pads) have filled the game pads (+0x444) by 0x8002A7A8,
    // ahead of the bl at 0x8002A7B8 that merges them into +0x884. Once per
    // game step, before its tasks (inferred: fn_80017618, the game step,
    // calls into ipSwitch before gfTaskScheduler::process; in tests it ran
    // as often as the updateSystem hook, also with the tasks stopped). r0 is
    // reloaded at 0x8002A7BC; LR is overwritten by that bl.
    static const u32 kGameWords[] = {
        0xA0BF003C, // lhz r5,0x3C(r31)
        0x387F0444, // addi r3,r31,0x444
        0x38DF0884  // addi r6,r31,0x884
    };
    // ipSwitch::update (0x8004A9EC), an offline match's path (read in memory
    // on 3.2.1), makes each player's controls (r26 = player 0-3) from one pad:
    //   0x8004AA40 lbz r0,0xB5(r3)   the player's port (r3 = ipPadConfig + player)
    //   0x8004AA5C addi r5,r1,0x20   a gfPadStatus on its stack
    //   0x8004AA60 bl getGamePadStatus(pad system, port, r5)
    //   0x8004AA64 bl ipPadConfig::getInstance
    //   0x8004AA68 mr r4,r26 / 0x8004AA6C mr r6,r29 / 0x8004AA70 addi r5,r1,0x20
    //   0x8004AA74 bl 0x8004A468     the pad -> buttons and stick at ipSwitch +8 +4 * player
    // which IpHuman::update (0x80048250) reads. P+'s "Controller Input Lag
    // Fix" (a branch at 0x8002AD8C, in getGamePadStatus) copies the port's
    // system pad there, not its game pad, so game-pad changes never reach a
    // fighter; the stack copy does, so the hook is on the last instruction
    // before the conversion. The stub runs that instruction, then saves r3
    // (ipPadConfig), r4 (the player) and r5 (the copy) at 0xC, 0x10 and 0x14
    // of its frame. r0 is dead (two calls returned, a third follows).
    static const u32 kFighterWords[] = {
        0x38A10020, // addi r5,r1,0x20
        0x4BFFF9F5  // bl 0x8004A468
    };
    static const hook::Site kSystemSite = {"pads system", 0x8002A274, kSystemWords, 3};
    static const hook::Site kGameSite = {"pads game", 0x8002A7A8, kGameWords, 3};
    static const hook::Site kFighterSite = {"pads fighter", 0x8004AA70, kFighterWords, 2};
    enum {
        kPlayerPorts = 0xB5, // ipPadConfig: u8 port of each player
        kPlayers = 4
    };

    static void dispatchPads(u32 phase, gfPadStatus* pads)
    {
        void (*callback)(u32, gfPadStatus*) = dispatch::table().callbacks.pads;
        if (callback != NULL && game::isRam(pads) && !perfBypassed())
        {
            callback(phase, pads);
        }
    }

    extern "C" void tmPadsSystemBody()
    {
#ifdef TM_PERF
        perfLoopTop();
        TM_PERF_SCOPE(kStubPadsSystem);
#endif
        gfPadSystem* system = g_gfPadSystem;
        if (game::isRam(system))
        {
            dispatchPads(iface::kPadsSystem, system->m_debugGcnPads);
        }
    }

    extern "C" void tmPadsGameBody()
    {
#ifdef TM_PERF
        perfGameTop();
#endif
        TM_PERF_SCOPE(kStubPadsGame);
        gfPadSystem* system = g_gfPadSystem;
        if (game::isRam(system))
        {
            dispatchPads(iface::kPadsGame, system->m_gameGcnPads);
        }
    }

    extern "C" void tmPadsFighterBody(const u8* config, u32 player, gfPadStatus* pad)
    {
        TM_PERF_SCOPE(kStubFighterPad);
        void (*callback)(u32, gfPadStatus*) = dispatch::table().callbacks.fighterPad;
        if (callback != NULL && !perfBypassed() && player < kPlayers && game::isRam(config) && game::isRam(pad))
        {
            callback(config[kPlayerPorts + player] & 0xF, pad);
        }
    }

    static asm void padsSystemEntry()
    {
        nofralloc
        lis r12, tmPadsSystemBody@h
        ori r12, r12, tmPadsSystemBody@l
        b tmPreservingCall
    }

    static asm void padsGameEntry()
    {
        nofralloc
        lis r12, tmPadsGameBody@h
        ori r12, r12, tmPadsGameBody@l
        b tmPreservingCall
    }

    static asm void padsFighterEntry()
    {
        nofralloc
        lwz r3, 0xC(r1)
        lwz r4, 0x10(r1)
        lwz r5, 0x14(r1)
        lis r12, tmPadsFighterBody@h
        ori r12, r12, tmPadsFighterBody@l
        b tmPreservingCall
    }

    bool installPadHooks(CoreApi* api)
    {
        bool system = hook::installInline(api, kSystemSite, padsSystemEntry);
        bool game = hook::installInline(api, kGameSite, padsGameEntry);
        bool fighter = hook::installInline(api, kFighterSite, padsFighterEntry);
        return system && game && fighter;
    }
}
}
