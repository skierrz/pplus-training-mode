#include "extra/hud_draw.h"

#include "main/gfx/text.h"

namespace tm {
namespace extra {
    static const float kCosQ[5] = {1.0f, 0.92388f, 0.70711f, 0.38268f, 0.0f};

    void drawShadowText(float x, float y, const char* text, gfx::Rgba color, float scale)
    {
        gfx::text(x + 0.8f, y + 0.8f, text, 0x000000C8u, scale);
        gfx::text(x, y, text, color, scale);
    }

    void drawHudCard(float x, float y, float w, float h, float xs, gfx::Rgba accent, gfx::Rgba sheen)
    {
        float px = 1.5f * xs;
        float innerW = w - 2.0f * px;
        gfx::fillRect(x, y, w, h, 0x050C12E4u);
        gfx::fillRect(x + px, y + 1.5f, innerW, h - 3.0f, 0x3A5868D8u);
        gfx::fillGradV(x + 2.0f * px, y + 3.0f, w - 4.0f * px, h - 6.0f, 0x132632DCu, 0x09141CE0u);
        gfx::fillRect(x + px, y + 1.5f, innerW, 2.5f, accent);
        if (sheen != 0u)
        {
            gfx::fillRect(x + px, y + 1.5f, innerW, 1.0f, sheen);
        }
    }

    void unitPoint16(int idx, float& ox, float& oy)
    {
        int i = idx & 15;
        int q = i >> 2;
        int k = i & 3;
        float a = kCosQ[k];
        float b = kCosQ[4 - k];
        if (q == 0)
        {
            ox = a;
            oy = b;
        }
        else if (q == 1)
        {
            ox = -b;
            oy = a;
        }
        else if (q == 2)
        {
            ox = -a;
            oy = -b;
        }
        else
        {
            ox = b;
            oy = -a;
        }
    }
}
}
