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
        const float cap = 7.5f;
        float scale = style::scaleForCap(cap);
        float width = style::boldWidth(kText, scale);
        float padX = style::w(2.0f);
        float x = layout::kWatermarkRight - padX - width;
        float y = style::textYForCap(layout::kWatermarkCapTop, scale);
        gfx::fillRect(x - padX, layout::kWatermarkCapTop - 1.0f, width + 2.0f * padX, cap + 2.0f, kBack);
        style::boldText(x + 1.0f, y + 1.0f, kText, kShadow, scale, scale);
        style::boldText(x, y, kText, 0xDCE6EEFF, scale, scale);
    }
}
}
