#include "main/ui/watermark.h"

#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"
#include "shared/version.h"

namespace tm {
namespace ui {
    static const char kText[] = TM_VERSION_MARK;
    static const gfx::Rgba kBack = 0x060E14B8;
    static const gfx::Rgba kShadow = 0x000000E0;

    void drawWatermark()
    {
        // Keep at kTypeMicro (8.0 px CRT cap floor) inside y <= 456.0 (5% CRT safe,
        // including 1 px shadow + descender room) while clearing actionLog (y <= 438).
        const float cap = style::kTypeMicro;
        const float capTop = layout::kWatermarkCapTop - 1.0f;
        float scale = style::scaleForCap(cap);
        float width = style::boldWidth(kText, scale);
        float padX = style::w(style::kFibHair);
        float x = layout::kWatermarkRight - padX - width;
        float y = style::textYForCap(capTop, scale);
        gfx::fillRect(x - padX, capTop - 1.0f, width + 2.0f * padX, cap + 2.0f, kBack);
        style::boldText(x + 1.0f, y + 1.0f, kText, kShadow, scale, scale);
        style::boldText(x, y, kText, 0xF2F7FBFF, scale, scale);
    }
}
}
