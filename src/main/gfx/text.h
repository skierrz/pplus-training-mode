#pragma once

#include "main/gfx/draw.h"

// Text in the game's resident system font, drawn inside a gfx::begin()/end() pass.
namespace tm {
namespace gfx {
    // Draws `text` (ASCII) with its top-left corner at x,y. Returns the x
    // just past the last character, or x if the font is not usable yet.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    float x_text(float x, float y, const char* text, Rgba color, float scale);
    float x_text2(float x, float y, const char* text, Rgba color, float scaleX, float scaleY);
    float x_textWidth(const char* text, float scale);
    float x_lineHeight(float scale);
    float x_displayText(float x, float y, const char* text, Rgba topColor, Rgba botColor, float scaleX, float scaleY,
                        float edgeWidth, Rgba edgeColor);
    float x_displayTextWidth(const char* text, float scale);
#endif
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    static inline float text(float x, float y, const char* text, Rgba color, float scale) { return x_text(x, y, text, color, scale); }
    static inline float text(float x, float y, const char* text, Rgba color, float scaleX, float scaleY) { return x_text2(x, y, text, color, scaleX, scaleY); }
    static inline float textWidth(const char* text, float scale) { return x_textWidth(text, scale); }
    static inline float lineHeight(float scale) { return x_lineHeight(scale); }
#else
    float text(float x, float y, const char* text, Rgba color, float scale);
    // The same with separate x and y scales (before the widescreen squeeze).
    float text(float x, float y, const char* text, Rgba color, float scaleX, float scaleY);
    // The width `text` would take at `scale`, measured by printing it
    // transparent off screen; 0 while the font is not usable.
    float textWidth(const char* text, float scale);

    // Height of one line of text at `scale`; 0 while the font is not usable.
    float lineHeight(float scale);
#endif

    // Brawl's chunky 32x32 resident display font (0x80497DA4 -> 0x81009580).
    // Capitals span 27 units starting 2 units below y (ratio 1.8x vs system
    // font's 15-unit caps). Characters outside its 79-char ASCII CMAP (such as
    // parentheses or angle brackets) automatically fall back to the system font
    // at matching cap height. Falls back cleanly to system font if unloaded.
    bool displayFontLoaded();
    bool displayHasChar(u8 c);
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    static inline float displayText(float x, float y, const char* text, Rgba topColor, Rgba botColor, float scaleX,
                                    float scaleY, float edgeWidth = 0.0f, Rgba edgeColor = 0)
    {
        return x_displayText(x, y, text, topColor, botColor, scaleX, scaleY, edgeWidth, edgeColor);
    }
    float displayText(float x, float y, const char* text, Rgba color, float scale);
    static inline float displayTextWidth(const char* text, float scale) { return x_displayTextWidth(text, scale); }
#else
    float displayText(float x, float y, const char* text, Rgba topColor, Rgba botColor, float scaleX, float scaleY,
                      float edgeWidth = 0.0f, Rgba edgeColor = 0);
    float displayText(float x, float y, const char* text, Rgba color, float scale);
    float displayTextWidth(const char* text, float scale);
#endif

    // Right- or centre-aligned text. ms::CharWriter has no measuring call
    // (ms_text_writer_base.h's CalcStringWidth is not used), so the width is
    // that of the last draw in the same `slot` (a changed text is drawn once
    // at its old width). Slots are 0..kTextSlots-1.
    enum { kTextSlots = 48 };
    void textRight(int slot, float right, float y, const char* text, Rgba color, float scale);
    void textCentered(int slot, float centre, float y, const char* text, Rgba color, float scale);
    // Text on a box that fits it (the box follows the text's width one frame
    // late, as above). `centre` true centres the box on x.
    void textBox(int slot, float x, float y, const char* text, Rgba color, Rgba box, float scale, bool centre);
    // Forget every remembered width (e.g. when a different page is shown).
    void forgetWidths();
    // Optional glyph cache lifecycle (Heaps::Network, fail-closed to CharWriter::Print).
    void textAttach();
    void textDetach();

#ifdef TM_PERF
    void perfTextCalibrate(u32* ticks);
#endif
}
}
