#include "main/record/hud.h"

#include <cstdio>

#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/record/slots.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"

namespace tm {
namespace record {
    using namespace ui::style;

    // Bottom-centre seekbar geometry in 640x480 2D space inside the 5% safe area.
    static const float kBarCentreY = ui::layout::kRecBarCentreY;
    static const float kBarW = 280.0f;
    static const float kBarH = ui::layout::kRecBarHeight;
    static const float kTrackLeft = 232.0f;
    static const float kTrackW = 176.0f;
    static const float kTrackH = 4.0f;
    static const float kKnobW = 5.0f;
    static const float kKnobH = 14.0f;
    static const float kLeftNumberX = 204.0f;
    static const float kRightNumberX = 436.0f;
    static const float kNumberCap = 10.5f;
    static const float kNumberMaxW = 42.0f;

    // Neutral dark-slate seekbar palette.
    static const gfx::Rgba kBar = 0x141A24F4;
    static const gfx::Rgba kTrack = 0x334155E8;
    static const gfx::Rgba kKnob = 0x38BDF8FF;
    static const gfx::Rgba kWhite = 0xF1F5F9FF;
    static const gfx::Rgba kWarn = 0xFACC15FF; // >= 80% capacity
    static const gfx::Rgba kFull = 0xFF948AFF; // full
    static const gfx::Rgba kMessageBox = 0x10151CE0;

    enum { kSlotMessage = gfx::kTextSlots - 4 };

    static void number(float centre, const char* text, gfx::Rgba color)
    {
        float scale = scaleForCap(kNumberCap);
        float width = boldWidth(text, scale);
        float room = w(kNumberMaxW);
        float sx = scale;
        if (width > room && width > 0.0f)
        {
            sx = scale * room / width;
            width = room;
        }
        float y = textYForCap(kBarCentreY - kNumberCap * 0.5f, scale);
        boldText(cx(centre) - width * 0.5f, y, text, color, sx, scale);
    }

    void hudDraw(const HudInfo& info)
    {
        if (info.message != NULL)
        {
            gfx::textBox(kSlotMessage, 320.0f, ui::layout::kRecMessageY, info.message, kFull, kMessageBox, 0.55f, true);
        }
        if (info.mode == kHudHidden)
        {
            return;
        }
        float top = kBarCentreY - kBarH * 0.5f;
        gfx::fillRect(cx(320.0f - kBarW * 0.5f), top, w(kBarW), kBarH, kBar);
        gfx::fillRect(cx(kTrackLeft), kBarCentreY - kTrackH * 0.5f, w(kTrackW), kTrackH, kTrack);
        char left[8];
        char right[8];
        if (info.mode == kHudRecord || info.mode == kHudArmed || info.mode == kHudControl)
        {
            int frame = info.frame < kMaxFrames ? info.frame : kMaxFrames;
            gfx::Rgba color = frame >= kMaxFrames ? kFull : (frame * 5 >= kMaxFrames * 4 ? kWarn : kWhite);
            sprintf(left, "%d", frame);
            number(kLeftNumberX, left, info.mode == kHudRecord ? color : kWhite);
            const char* tag = info.mode == kHudArmed ? "Armed" : (info.mode == kHudRecord ? "REC" : "Ctrl");
            gfx::Rgba tagColor = info.mode == kHudArmed ? kWarn : (info.mode == kHudRecord ? kFull : kWhite);
            number(kRightNumberX, tag, tagColor);
        }
        else
        {
            int frame = info.frame;
            int end = info.end;
            if (frame >= end)
            {
                end = frame;
            }
            sprintf(left, "%d", frame);
            number(kLeftNumberX, left, kWhite);
            if (info.random)
            {
                number(kRightNumberX, "?", kWhite);
            }
            else
            {
                sprintf(right, "%d", end);
                number(kRightNumberX, right, kWhite);
                float at = end > 0 ? static_cast<float>(frame) / static_cast<float>(end) : 0.0f;
                float knob = kTrackLeft + at * kTrackW - kKnobW * 0.5f;
                gfx::fillRect(cx(knob), kBarCentreY - kKnobH * 0.5f, w(kKnobW), kKnobH, kKnob);
            }
        }
    }
}
}
