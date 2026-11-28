#pragma once

#include <types.h>

// What the mod drew, kept instead of drawn, for the tests of where text
// lands (tests/host/tests/test_text_fit.cpp).
//
// The mod's own text code (src/main/gfx/text.cpp) is compiled as it is. It
// prints through the game's character writer, ms::CharWriter; the stand-in
// for that class (include/ms/ms_char_writer.h) moves its cursor as the
// game's does, by the font's advance for the character times the horizontal
// scale, with the advances of the game's own font
// (tests/host/data/font_advances.h), and reports every character here. So a
// width the mod measures on the PC is the width it measures on the console.
//
// Rectangles (src/main/gfx/draw.cpp is not compiled: it writes to the
// graphics processor) are kept as given.
//
// Before every test: nothing drawn, the font usable, the screen 4:3.
namespace host {
namespace canvas {
    // One run of text: the characters printed after one placing of the
    // cursor.
    struct Run {
        float x;      // where its first character starts
        float y;      // the y given to gfx::text (the line's top)
        float right;  // where the cursor ended
        float scaleX; // the horizontal scale, widescreen factor included
        float scaleY;
        u32 color;
        int unknown;  // characters the font has no glyph for
        bool displayFont;
        char text[160];
    };

    struct Rect {
        float x, y, width, height;
        u32 color;
        bool frame; // an outline (gfx::frameRect), else a filled one
    };

    void reset();
    // Forgets what was drawn.
    void clear();
    // Whether the game's font is there (the mod draws no text without it).
    void setFont(bool usable);

    // Text runs in drawing order. Runs the mod printed only to measure
    // them (off screen, see gfx::textWidth) are not listed.
    int runs();
    const Run& run(int index);
    int rects();
    const Rect& rect(int index);
    int scissorPushes();
    int scissorDepth();

    // The box around everything drawn since clear(): rectangles, text runs
    // (from their capitals' top to the descenders' bottom, by style.h's
    // measures of the font) and the other shapes (lines, triangles, arrows,
    // ellipses), which are not drawn but kept as the points they cover.
    // `any` is false while nothing was drawn.
    struct Bounds {
        float x0, y0, x1, y1;
        bool any;
    };
    Bounds bounds();

    // The width of `text` at horizontal scale `scale` (which must hold the
    // widescreen factor already), straight from the font's advances.
    float width(const char* text, float scale);
    // The advance of one character at scale 1; `known` false if the font
    // has no glyph for it.
    float advance(unsigned char c, bool& known);
    // Advance for Brawl's 32x32 chunky display font (0x80497DA4).
    float advanceDisplay(unsigned char c, bool& known);

    // For the stand-in character writer.
    void begin(float x, float y, float scaleX, float scaleY, u32 color, bool displayFont = false);
    void put(unsigned char c, float advance, bool known, float cursor);
}
}
