#pragma once

#include <types.h>

#include "shared/game/mem.h"

// Project+'s Code Menu state (Source/CodeMenu/CodeMenu.asm, data.cmnu loaded
// at 0x804E0000).
namespace tm {
namespace game {
    enum {
        kCodeMenuBase = 0x804E0000,
        kCodeMenuRootOffset = 0x4,
        kCodeMenuRootAddr = 0x804E07D8,
        kCodeMenuStateOffset = 0x34,
        kCodeMenuStateOpen = 4
    };

    // True once P+'s Code Menu data block (data.cmnu / dnet.cmnu) is loaded at
    // 0x804E0000. Control Code Menu (CodeMenu.asm:604-608) checks the same
    // root pointer before running.
    inline bool codeMenuLoaded()
    {
        return wordAt(reinterpret_cast<const void*>(kCodeMenuBase), kCodeMenuRootOffset) == kCodeMenuRootAddr;
    }

    // True while P+'s Code Menu is open (state 4 at 0x804E0034: game paused
    // via gfTaskScheduler+0xB, pads masked, Print Code Menu owns the frame).
    inline bool codeMenuOpen()
    {
        return codeMenuLoaded()
               && wordAt(reinterpret_cast<const void*>(kCodeMenuBase), kCodeMenuStateOffset) == kCodeMenuStateOpen;
    }
}
}
