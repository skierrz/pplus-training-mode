#pragma once

#include <types.h>

// Minimal 2D overlay drawing on top of the finished frame. Coordinates are
// the game's 2D space: 640x480, origin top left, y down.
namespace tm {
namespace gfx {
    typedef u32 Rgba; // 0xRRGGBBAA

    // Bracket every overlay pass: begin() selects the game's 2D camera,
    // end() puts back the main camera.
    void begin();
    void end();

    // The GX state the shapes below are drawn with, the camera's view matrix
    // included; each calls it. Text calls it before its own setup, which keeps
    // the position matrix, culling and alpha test.
    void prepare();

    // Inside a 2D pass, prepare() and text keep the GX state they set up
    // for the next shape or text of the same kind. Code that sets GX state
    // of its own in a 2D pass calls forgetState() after it.
    bool textStateKept();
    void keepTextState();
    void forgetState();

    // Horizontal squeeze for text glyphs: 1 for 4:3; 0.75 keeps text
    // unstretched in 16:9 (the display widens the 640x480 picture). Positions
    // and shapes are not squeezed.
    void setXScale(float scale);
    float xScale();

    // The game's own screen setting (Options > Screen, gmGlobalRecord's
    // m_isWidescreen), which the HUD layout follows (ui/hud_layout.h). Sets
    // the squeeze too: 0.75 on 16:9, 1 on 4:3. setXScale() sets it to
    // `scale < 1`.
    void setWidescreen(bool wide);
    bool widescreen();

    void fillRect(float x, float y, float width, float height, Rgba color);
    // Vertical two-stop gradient rectangle (top colour on upper edge, bot on lower edge).
    void fillGradV(float x, float y, float width, float height, Rgba top, Rgba bot);
    // A one-pixel-wide outline just inside the rectangle.
    void frameRect(float x, float y, float width, float height, Rgba color);
    // A small triangle pointing up (dir < 0) or down (dir > 0), centred on x.
    void arrow(float x, float y, float size, int dir, Rgba color);
    // A filled triangle in 2D space.
    void fillTri(float x0, float y0, float x1, float y1, float x2, float y2, Rgba color);
    // Lines in the current camera (2D, or the main camera for the stage):
    // `count` vertex() calls follow. `primitive` is GX_LINES or
    // GX_LINESTRIP, `width` in sixths of a pixel.
    void lineBegin(int primitive, int count, u8 width);
    // One vertex of a shape begun with GXBegin(..., GX_VTXFMT1, n) after
    // prepare() (or of lineBegin's lines).
    void vertex(float x, float y, Rgba color);
    // A filled ellipse centred on (x, y), drawn as a 24-segment fan.
    void fillEllipse(float x, float y, float rx, float ry, Rgba color);
}
}
