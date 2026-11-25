#pragma once

#include <types.h>

// Single-line lab menu option descriptions stored in the extra module so
// the match module carries zero description string bloat.
namespace tm {
namespace extra {
    void menuDescAttach();
    void menuDescDetach();

    // Returns a single-line ASCII description for (pageId, row), or for a
    // Category Rail entry when pageId == ui::pages::P_COUNT (row 0..9).
    const char* optionDesc(u8 pageId, u8 row);
}
}
