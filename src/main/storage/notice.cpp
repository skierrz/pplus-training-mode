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
        const float scale = ui::style::scaleForCap(ui::style::kTypeMicro);
        float lineH = gfx::lineHeight(scale);
        if (lineH > 0.0f)
        {
            float y = menuOpen ? ui::layout::menuNoticeY(ui::style::kLabBottom, lineH) : ui::layout::kNoticeY;
            float cx = 320.0f;
            if (menuOpen)
            {
                float halfBox = gfx::textWidth(s_text, scale) * 0.5f + 6.0f * gfx::xScale();
                // Keep long menu-strip notices clear of the bottom-right watermark slot on 4:3.
                const float kMaxRight = 476.0f;
                if (cx + halfBox > kMaxRight)
                {
                    cx = kMaxRight - halfBox;
                    float minCx = ui::layout::kSafeMarginX + halfBox;
                    if (cx < minCx)
                    {
                        cx = minCx;
                    }
                }
            }
            gfx::textBox(kSlot, cx, y, s_text, s_color, kBox, scale, true);
        }
    }
}
}
