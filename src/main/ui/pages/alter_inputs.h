#pragma once

#include <types.h>

#include "main/ui/menu.h"

// Alter Inputs (Slot Management > Modify Inputs). The page and its rows are
// here in the match module; the code that reads and writes the slot behind
// them is in the extra module (extra/lab_tools.cpp), reached through the
// extra hooks (main/extra.h). The rows' values are this copy of one frame
// of the managed slot, kept in the menu's scratch bytes (ui/menu.h).
namespace tm {
namespace ui {
namespace pages {
    enum {
        kAlterAxes = 6,   // stick x, y, C-stick x, y, analog L, R
        kAlterButtons = 9 // attack, special, jump, shield, smash, C-stick, taunts up, side, down
    };

    struct AlterCopy {
        u16 frame; // 1-based: frame n is the run's step n - 1
        s16 axis[kAlterAxes];
        u8 button[kAlterButtons];
    };
    TM_STATIC_ASSERT(sizeof(AlterCopy) <= kScratchBytes, tm_alter_copy_fits);

    // The page's Frame row: its range follows the slot (NULL when detached).
    Option* alterFrameRow();
}
}
}
