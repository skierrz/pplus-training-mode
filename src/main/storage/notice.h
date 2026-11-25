#pragma once

#include "main/gfx/draw.h"

// One line of text centred above the damage row (ui::layout::kNoticeY), or in
// the strip under the menu panel while a lab or drill menu is open, for the SD
// card's outcomes (settings not saved, a recording exported). Shown for a few
// seconds of rendered frames, hidden while Brawl's pause menu is open without
// the lab menu.
namespace tm {
namespace storage {
    static const gfx::Rgba kNoticeOk = 0x8DFF6EFF;
    static const gfx::Rgba kNoticeWarn = 0xFFF000FF;
    static const gfx::Rgba kNoticeError = 0xFFA2BAFF;

    void notice(gfx::Rgba color, const char* format, ...);
    void noticeClear();
    bool noticeActive();
    // Inside a gfx::begin()/end() pass; counts the notice down.
    void noticeDraw(bool menuOpen = false);
}
}
