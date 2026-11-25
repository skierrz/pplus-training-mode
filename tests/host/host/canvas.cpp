#include "host/canvas.h"

#include "data/font_advances.h"
#include "host/mem.h"
#include "main/gfx/draw.h"
#include "main/ui/style.h"

namespace host {
namespace canvas {
    enum { kMaxRuns = 1024, kMaxRects = 1024 };

    // Where src/main/gfx/text.cpp looks for the font: the word at +0x10 of
    // the game's font object points at the font's resource once it is
    // loaded.
    static const u32 kFontResourceWord = 0x80497E44 + 0x10;
    static const u32 kSomeResource = 0x80FD6540;
    static const u32 kDisplayFontResourceWord = 0x80497DA4 + 0x10;
    static const u32 kSomeDisplayResource = 0x81009580;
    // gfx::textWidth prints at -2000; nothing is drawn that far out.
    static const float kOffScreen = -1000.0f;

    static Run s_runs[kMaxRuns];
    static int s_runCount = 0;
    static bool s_open = false; // the last run still takes characters
    static Rect s_rects[kMaxRects];
    static int s_rectCount = 0;
    static float s_xScale = 1.0f;
    static bool s_wide = false;
    // Shapes other than rectangles and text: the box of their points.
    static Bounds s_shapes = {0.0f, 0.0f, 0.0f, 0.0f, false};

    // For the stand-ins of the mod's drawing functions at the end of this file.
    float& xScaleValue();
    bool& wideValue();
    void keepPoint(float x, float y);
    void keepRect(float x, float y, float width, float height, u32 color, bool frame);

    float& xScaleValue()
    {
        return s_xScale;
    }

    bool& wideValue()
    {
        return s_wide;
    }

    static void grow(Bounds& b, float x, float y)
    {
        if (!b.any)
        {
            b.x0 = b.x1 = x;
            b.y0 = b.y1 = y;
            b.any = true;
            return;
        }
        b.x0 = x < b.x0 ? x : b.x0;
        b.x1 = x > b.x1 ? x : b.x1;
        b.y0 = y < b.y0 ? y : b.y0;
        b.y1 = y > b.y1 ? y : b.y1;
    }

    void keepPoint(float x, float y)
    {
        grow(s_shapes, x, y);
    }

    void clear()
    {
        s_runCount = 0;
        s_rectCount = 0;
        s_open = false;
        s_shapes.any = false;
    }

    void setFont(bool usable)
    {
        *reinterpret_cast<u32*>(kFontResourceWord) = usable ? kSomeResource : 0;
        *reinterpret_cast<u32*>(kDisplayFontResourceWord) = usable ? kSomeDisplayResource : 0;
    }

    void reset()
    {
        clear();
        s_xScale = 1.0f;
        s_wide = false;
        setFont(true);
    }

    int runs()
    {
        return s_runCount;
    }

    const Run& run(int index)
    {
        return s_runs[index];
    }

    int rects()
    {
        return s_rectCount;
    }

    const Rect& rect(int index)
    {
        return s_rects[index];
    }

    Bounds bounds()
    {
        using namespace tm::ui::style;
        Bounds b = s_shapes;
        for (int i = 0; i < s_rectCount; i++)
        {
            const Rect& r = s_rects[i];
            grow(b, r.x, r.y);
            grow(b, r.x + r.width, r.y + r.height);
        }
        for (int i = 0; i < s_runCount; i++)
        {
            const Run& r = s_runs[i];
            if (r.text[0] == '\0')
            {
                continue;
            }
            float capOff = r.displayFont ? 2.0f : kCapOffset;
            float capH = r.displayFont ? 27.0f : kCapHeight;
            float desc = capH * 0.2f;
            grow(b, r.x, r.y + capOff * r.scaleY);
            grow(b, r.right, r.y + (capOff + capH + desc) * r.scaleY);
        }
        return b;
    }

    float advance(unsigned char c, bool& known)
    {
        known = c >= font::kFirst && c <= font::kLast;
        return static_cast<float>(known ? font::kAdvance[c - font::kFirst] : font::kAlternate);
    }

    float advanceDisplay(unsigned char c, bool& known)
    {
        if (c >= font::kFirst && c <= font::kLast)
        {
            signed char a = font::kDisplayAdvance[c - font::kFirst];
            known = a >= 0;
            return static_cast<float>(known ? a : font::kDisplayAlternate);
        }
        known = false;
        return static_cast<float>(font::kDisplayAlternate);
    }

    float width(const char* text, float scale)
    {
        // Added up as the game's writer does: one single-precision
        // product per character, added to the cursor.
        float cursor = 0.0f;
        for (; *text != '\0'; text++)
        {
            bool known;
            cursor += advance(static_cast<unsigned char>(*text), known) * scale;
        }
        return cursor;
    }

    void begin(float x, float y, float scaleX, float scaleY, u32 color, bool displayFont)
    {
        s_open = false;
        if (x < kOffScreen || s_runCount >= kMaxRuns)
        {
            return;
        }
        Run& r = s_runs[s_runCount++];
        r.x = r.right = x;
        r.y = y;
        r.scaleX = scaleX;
        r.scaleY = scaleY;
        r.color = color;
        r.unknown = 0;
        r.displayFont = displayFont;
        r.text[0] = '\0';
        s_open = true;
    }

    void put(unsigned char c, float, bool known, float cursor)
    {
        if (!s_open)
        {
            return;
        }
        Run& r = s_runs[s_runCount - 1];
        size_t used = strlen(r.text);
        if (used + 1 < sizeof(r.text))
        {
            r.text[used] = static_cast<char>(c);
            r.text[used + 1] = '\0';
        }
        r.unknown += known ? 0 : 1;
        r.right = cursor;
    }

    static void keep(float x, float y, float width, float height, u32 color, bool frame)
    {
        if (s_rectCount < kMaxRects)
        {
            Rect& r = s_rects[s_rectCount++];
            r.x = x;
            r.y = y;
            r.width = width;
            r.height = height;
            r.color = color;
            r.frame = frame;
        }
    }
    void keepRect(float x, float y, float width, float height, u32 color, bool frame)
    {
        keep(x, y, width, height, color, frame);
    }
}
}

// src/main/gfx/draw.cpp: rectangles are kept, the other shapes are not
// drawn (their points count in bounds()).
namespace tm {
namespace gfx {
    void begin()
    {
    }
    void end()
    {
    }
    void prepare()
    {
    }
    bool textStateKept()
    {
        return false;
    }
    void keepTextState()
    {
    }
    void forgetState()
    {
    }
    void setXScale(float scale)
    {
        host::canvas::xScaleValue() = scale;
        host::canvas::wideValue() = scale < 1.0f;
    }
    float xScale()
    {
        return host::canvas::xScaleValue();
    }
    void setWidescreen(bool wide)
    {
        host::canvas::wideValue() = wide;
        host::canvas::xScaleValue() = wide ? 0.75f : 1.0f;
    }
    bool widescreen()
    {
        return host::canvas::wideValue();
    }
    void fillRect(float x, float y, float width, float height, Rgba color)
    {
        host::canvas::keepRect(x, y, width, height, color, false);
    }
    void fillGradV(float x, float y, float width, float height, Rgba top, Rgba)
    {
        host::canvas::keepRect(x, y, width, height, top, false);
    }
    void frameRect(float x, float y, float width, float height, Rgba color)
    {
        host::canvas::keepRect(x, y, width, height, color, true);
    }
    // The other shapes: only the points they cover are kept.
    void arrow(float x, float y, float size, int dir, Rgba)
    {
        // As draw.cpp: from y to y + size, `size` either side of x.
        (void)dir;
        host::canvas::keepPoint(x - size, y);
        host::canvas::keepPoint(x + size, y + size);
    }
    void fillTri(float x0, float y0, float x1, float y1, float x2, float y2, Rgba)
    {
        host::canvas::keepPoint(x0, y0);
        host::canvas::keepPoint(x1, y1);
        host::canvas::keepPoint(x2, y2);
    }
    void lineBegin(int, int, u8)
    {
    }
    void vertex(float x, float y, Rgba)
    {
        host::canvas::keepPoint(x, y);
    }
    void fillEllipse(float x, float y, float rx, float ry, Rgba)
    {
        host::canvas::keepPoint(x - rx, y - ry);
        host::canvas::keepPoint(x + rx, y + ry);
    }
}
}
