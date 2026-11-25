#include "main/game/freeze.h"

#include <gf/gf_task_scheduler.h>
#include <types.h>

#include "shared/game/mem.h"

namespace tm {
namespace game {
    // Stops the game with bit 0x8 of the task scheduler's byte +0xB, as
    // ProjectPunch's checkMenuPaused does (source/pp/main.cpp). On P+
    // 3.2.1, gfTaskScheduler::process (0x8002E578) runs 17 passes (a debug
    // pass, then the 16 process phases) and skips a pass while +0xB is
    // non-zero, except that 2 (P+'s debug pause) still runs the debug pass.
    // Drawing runs separately and goes on. P+ hooks this loop: "Debug
    // Controls" (Project+/Debug/modifiedDebug.asm; 0x8002E5AC, 0x8002E68C),
    // "Custom RenderDebug Call" and "Frame Advance Convert" (Project+/Debug/
    // renderDebug.asm; 0x8002E5B4, 0x8002E5C4, loop count at 0x8002E684).
    // In a memory dump: 0x8002E5AC -> 0x80578B58, 0x8002E5B4 -> 0x80576048,
    // 0x8002E5C4 -> 0x80576060.
    //
    // P+'s "Debug Start Input" (modifiedDebug.asm, pad read at 0x801E6D1C)
    // writes 0x0004 ("debug unpause") to the halfword +0xA while it is
    // non-zero and Start is held. That wipes our bit, so it is set again
    // before every step (lab.cpp handles Start).
    enum {
        kSchedulerStopHalf = 0xA,
        kSchedulerStopByte = 0xB,
        kStopBit = 0x8
    };

    static bool s_frozen = false;
    static bool s_gameplayPaused = false;
    static bool s_cameraPaused = false;

    // The gfTask categories (gf_task.h) of the simulation. In a Training
    // match on Final Destination the scheduler's lists held tasks in 6, 7,
    // 0xA, 0xB, 0xE, 0x21, 0x23, 0x24 and 0x27; the HUD (0x12), the camera
    // (0x15) and the operators (0) keep running.
    static const gfTask::Category kGameplay[] = {
        gfTask::Category_Enemy, gfTask::Category_Ground, gfTask::Category_GrCollision,
        gfTask::Category_Fighter, gfTask::Category_Item, gfTask::Category_Weapon, gfTask::Category_Effect,
        gfTask::Category_Stage, gfTask::Category_Physics, gfTask::Category_AI, gfTask::Category_CoinShooter,
        gfTask::Category_SoCollision
    };

    static u8* stopByte()
    {
        gfTaskScheduler* scheduler = g_taskScheduler;
        if (!isRam(scheduler))
        {
            return NULL;
        }
        return reinterpret_cast<u8*>(scheduler) + kSchedulerStopByte;
    }

    void setFrozen(bool frozen)
    {
        u8* byte = stopByte();
        if (byte == NULL)
        {
            s_frozen = false;
            return;
        }
        if (frozen)
        {
            // Set on every call, not only on a change: it read back 0 after
            // Start was held (measured; setup not recorded), as Debug Start
            // Input's write to +0xA would do.
            *byte |= kStopBit;
        }
        else if (s_frozen)
        {
            *byte &= ~kStopBit;
        }
        s_frozen = frozen;
    }

    bool frozen()
    {
        return s_frozen;
    }

    void reapplyFrozen()
    {
        u8* byte = stopByte();
        if (byte != NULL)
        {
            *byte = s_frozen ? (*byte | kStopBit) : (*byte & ~kStopBit);
        }
    }

    bool categoriesPaused()
    {
        return s_gameplayPaused || s_cameraPaused;
    }

    void setGameplayPaused(bool paused)
    {
        gfTaskScheduler* scheduler = g_taskScheduler;
        if (paused == s_gameplayPaused || !isRam(scheduler))
        {
            return;
        }
        for (u32 i = 0; i < sizeof(kGameplay) / sizeof(kGameplay[0]); i++)
        {
            scheduler->setPauseCategory(kGameplay[i], paused);
        }
        s_gameplayPaused = paused;
    }

    void setCameraPaused(bool paused)
    {
        gfTaskScheduler* scheduler = g_taskScheduler;
        if (paused == s_cameraPaused || !isRam(scheduler))
        {
            return;
        }
        scheduler->setPauseCategory(gfTask::Category_Camera, paused);
        s_cameraPaused = paused;
    }

    u32 otherStopBits()
    {
        u8* byte = stopByte();
        if (byte == NULL)
        {
            return 0;
        }
        const u16* half = reinterpret_cast<const u16*>(byte - (kSchedulerStopByte - kSchedulerStopHalf));
        return *half & ~static_cast<u32>(kStopBit);
    }

    bool heldByOthers()
    {
        u8* byte = stopByte();
        return byte != NULL && !s_frozen && (*byte & kStopBit) != 0;
    }
}
}
