#include "main/ui/watermark.h"

#include "main/gfx/text.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"
#include "shared/version.h"

namespace tm {
namespace ui {
    static const char kText[] = TM_VERSION_MARK;
    static float s_width = 0.0f;
    static const gfx::Rgba kShadow = 0x000000C8;

    void drawWatermark()
    {
        float scale = style::scaleForCap(style::kWatermarkCap);
        if (s_width <= 0.0f)
        {
            s_width = style::boldWidth(kText, scale);
        }
        float x = style::rx(layout::kWatermarkRight) - s_width;
        float y = style::textYForCap(layout::kWatermarkCapTop, scale);
        style::boldText(x + 1.0f, y + 1.0f, kText, kShadow, scale, scale);
        style::boldText(x, y, kText, style::kText, scale, scale);
    }
}
}
