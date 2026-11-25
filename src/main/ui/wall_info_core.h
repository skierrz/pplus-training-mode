#pragma once

#include <cstdio>
#include <types.h>

// Pure formatting helpers for P+ 3.2.1 Can Walljump (IC-Basic 23189,
// LA-Basic[3..4], 20f cooldown IC-Basic 23069) and Wall Cling (IC-Basic 23190,
// Knuckles 0x2D WallClimb*, LA-Basic[11..12], 1/airtime via IC-Basic 23075).
// Compiled by main/ui/info_display.cpp and host unit tests.
namespace tm {
namespace ui {
    enum {
        kFtKindKnuckles = 0x2D,
        kParamCanWalljump = 23189,
        kParamCanWallCling = 23190,
        kWallJumpCountLa = 3,      // LA-Basic[3]: Int_Wall_Jump_Count
        kWallJumpCooldownLa = 4,   // LA-Basic[4]: Int_Wall_Jump_Past_Frame (counts down 20..0)
        kWallClingNoAttachLa = 11, // LA-Basic[11]: Int_No_Attach_Wall_Frame
        kWallClingCountLa = 12     // LA-Basic[12]: Int_Attach_Wall_Count
    };

    inline void formatCanWalljump(int canWalljumpConst, bool airborne, int wallJumpCount, int wallJumpCooldown,
                                  char* buf, u32 size)
    {
        (void)wallJumpCount;
        if (canWalljumpConst == 0)
        {
            snprintf(buf, size, "Can Walljump: No");
        }
        else if (airborne && wallJumpCooldown > 0)
        {
            snprintf(buf, size, "Can Walljump: CD %df", wallJumpCooldown);
        }
        else
        {
            snprintf(buf, size, "Can Walljump: Yes");
        }
    }

    inline void formatWallCling(int canWallClingConst, int ftKind, bool airborne, int attachCount,
                                int noAttachFrames, char* buf, u32 size)
    {
        if (ftKind == kFtKindKnuckles)
        {
            snprintf(buf, size, "Wall Cling: Climb");
        }
        else if (canWallClingConst == 0)
        {
            snprintf(buf, size, "Wall Cling: No");
        }
        else if (airborne && (attachCount > 0 || noAttachFrames > 0))
        {
            snprintf(buf, size, "Wall Cling: Used");
        }
        else
        {
            snprintf(buf, size, "Wall Cling: Yes");
        }
    }
}
}
