#include "main/ui/watermark.h"

#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"
#include "shared/version.h"

namespace tm {
namespace ui {
    static const char kText[] = TM_VERSION_MARK;
    static const gfx::Rgba kOutline = 0x000000F8;

    void drawWatermark()
    {
        // Keep at kTypeMicro (8.0 px CRT cap floor) inside y <= 456.0 (5% CRT safe,
        // including 1 px outline + descender room) while clearing actionLog (y <= 438).
        const float cap = style::kTypeMicro;
        const float capTop = layout::kWatermarkCapTop - 1.0f;
        float scale = style::scaleForCap(cap);
        float width = style::boldWidth(kText, scale);
        float padX = style::w(style::kFibHair);
        float xs = gfx::xScale();
        float x = layout::kWatermarkRight - padX - width;
        float y = style::textYForCap(capTop, scale);
        style::boldText(x - xs, y, kText, kOutline, scale, scale);
        style::boldText(x + xs, y, kText, kOutline, scale, scale);
        style::boldText(x, y - 1.0f, kText, kOutline, scale, scale);
        style::boldText(x, y + 1.0f, kText, kOutline, scale, scale);
        style::boldText(x, y, kText, 0xF2F7FBFF, scale, scale);
    }
}
}
