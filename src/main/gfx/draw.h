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

#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    void x_prepare();
    void x_forgetState();
    float x_xScale();
    bool x_widescreen();
    void x_fillRect(float x, float y, float width, float height, Rgba color);
    void x_fillGradV(float x, float y, float width, float height, Rgba top, Rgba bot);
    void x_frameRect(float x, float y, float width, float height, Rgba color);
    void x_fillTri(float x0, float y0, float x1, float y1, float x2, float y2, Rgba color);
    void x_lineBegin(int primitive, int count, u8 width);
    void x_vertex(float x, float y, Rgba color);
    void x_fillEllipse(float x, float y, float rx, float ry, Rgba color);
#endif

    // The GX state the shapes below are drawn with, the camera's view matrix
    // included; each calls it. Text calls it before its own setup, which keeps
    // the position matrix, culling and alpha test.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    static inline void prepare() { x_prepare(); }
#else
    void prepare();
#endif

    // Inside a 2D pass, prepare() and text keep the GX state they set up
    // for the next shape or text of the same kind. Code that sets GX state
    // of its own in a 2D pass calls forgetState() after it.
    bool textStateKept();
    void keepTextState();
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    static inline void forgetState() { x_forgetState(); }
#else
    void forgetState();
#endif

    // Horizontal squeeze for text glyphs: 1 for 4:3; 0.75 keeps text
    // unstretched in 16:9 (the display widens the 640x480 picture). Positions
    // and shapes are not squeezed.
    void setXScale(float scale);
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    static inline float xScale() { return x_xScale(); }
#else
    float xScale();
#endif

    // The game's own screen setting (Options > Screen, gmGlobalRecord's
    // m_isWidescreen), which the HUD layout follows (ui/hud_layout.h). Sets
    // the squeeze too: 0.75 on 16:9, 1 on 4:3. setXScale() sets it to
    // `scale < 1`.
    void setWidescreen(bool wide);
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__) && !defined(TM_EXTRA_THUNK_IMPL)
    static inline bool widescreen() { return x_widescreen(); }
    static inline void fillRect(float x, float y, float width, float height, Rgba color) { x_fillRect(x, y, width, height, color); }
    static inline void fillGradV(float x, float y, float width, float height, Rgba top, Rgba bot) { x_fillGradV(x, y, width, height, top, bot); }
    static inline void frameRect(float x, float y, float width, float height, Rgba color) { x_frameRect(x, y, width, height, color); }
    void arrow(float x, float y, float size, int dir, Rgba color);
    static inline void fillTri(float x0, float y0, float x1, float y1, float x2, float y2, Rgba color) { x_fillTri(x0, y0, x1, y1, x2, y2, color); }
    static inline void lineBegin(int primitive, int count, u8 width) { x_lineBegin(primitive, count, width); }
    static inline void vertex(float x, float y, Rgba color) { x_vertex(x, y, color); }
    static inline void fillEllipse(float x, float y, float rx, float ry, Rgba color) { x_fillEllipse(x, y, rx, ry, color); }
#else
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

    // Hardware EFB scissor clip in 640x480 2D space, intersected with the
    // active scissor rect so nested list-viewport and highlight-lens clips
    // compose cleanly across 480i, 480p, PAL, and high-IR rendering.
    // Returns false (leaving GX scissor untouched) if the clipped rect is empty.
    struct ScissorState {
        u32 x, y, w, h;
    };
    bool pushScissor(float x0, float y0, float x1, float y1, ScissorState& saved);
    void popScissor(const ScissorState& saved);
#endif
}
}
