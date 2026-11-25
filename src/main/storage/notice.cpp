#include "main/storage/notice.h"

#include <cstdarg>
#include <cstdio>

#include "main/gfx/text.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"
#include "shared/log.h"

namespace tm {
namespace storage {
    enum {
        kFrames = 240,
        kSlot = gfx::kTextSlots - 6
    };
    static const gfx::Rgba kBox = 0x000000C0;

    static char s_text[64];
    static gfx::Rgba s_color = kNoticeOk;
    static int s_timer = 0;

    void notice(gfx::Rgba color, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(s_text, sizeof(s_text), format, args);
        va_end(args);
        s_color = color;
        s_timer = kFrames;
        TM_LOG("storage: notice: %s\n", s_text);
    }

    void noticeClear()
    {
        s_timer = 0;
    }

    bool noticeActive()
    {
        return s_timer > 0;
    }

    void noticeDraw(bool menuOpen)
    {
        if (s_timer <= 0)
        {
            return;
        }
        s_timer--;
        float lineH = gfx::lineHeight(0.5f);
        if (lineH > 0.0f)
        {
            float y = menuOpen ? ui::layout::menuNoticeY(ui::style::kLabBottom, lineH) : ui::layout::kNoticeY;
            gfx::textBox(kSlot, 320.0f, y, s_text, s_color, kBox, 0.5f, true);
        }
    }
}
}
