#include "main/gfx/draw.h"

#include <GX/GXGeometry.h>
#include <GX/GXHardware.h>
#include <GX/GXTransform.h>
#include <gf/gf_camera.h>
#include <gf/gf_draw.h>

#include "shared/perf.h"

namespace tm {
namespace gfx {
    enum { kMainCamera = 0, k2dCamera = 4 };

    static float s_xScale = 1.0f;
    static bool s_wide = false;
    // The camera shapes and text are drawn in: the 2D one between begin()
    // and end(), else the main one (the stage's space).
    static int s_camera = kMainCamera;
    // The GX state left by our last draw in a 2D pass, where only our own
    // code draws between begin() and end(); shapes after shapes and text
    // after text don't set it again. Outside a 2D pass the game's and
    // Project+'s routines draw between ours, so it is always set.
    enum State { kStateNone, kStateShape, kStateText };
    static State s_state = kStateNone;

    bool textStateKept()
    {
        return s_state == kStateText;
    }

    void keepTextState()
    {
        s_state = s_camera == k2dCamera ? kStateText : kStateNone;
    }

    // Called by the extra module (the TDI editor's textured knob).
#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    void forgetState()
    {
        s_state = kStateNone;
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

    void begin()
    {
        // Applies camera 4 of gfCameraManager, the game's 2D camera.
        gfDrawSetupCoord2D();
        s_camera = k2dCamera;
        s_state = kStateNone;
    }

    void end()
    {
        // Camera 0 is the main camera. TEV, blend, vertex-format and depth are
        // left as the game's 2D helpers leave them: the game sets them again
        // for all it draws, as it must after P+'s Code Menu, which draws text
        // here every frame it is open.
        s_camera = kMainCamera;
        s_state = kStateNone;
        gfCameraManager::getManager()->m_cameras[kMainCamera].setGX();
    }

    void setXScale(float scale)
    {
        s_xScale = scale;
        s_wide = scale < 1.0f;
    }

    float xScale()
    {
        return s_xScale;
    }

    void setWidescreen(bool wide)
    {
        s_wide = wide;
        s_xScale = wide ? 0.75f : 1.0f;
    }

    bool widescreen()
    {
        return s_wide;
    }

    // Called by the extra module (DI Display): kept here even
    // where every call in this module is inlined.
#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    void vertex(float x, float y, Rgba color)
    {
        WGPIPE.f = x;
        WGPIPE.f = y;
        WGPIPE.f = 0.0f;
        WGPIPE.ui = color;
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

    // Everything a shape needs, set before each one: vertex format 1 = position
    // (xyz f32) + colour (rgba8), one TEV stage passing the vertex colour,
    // alpha blending, no depth or alpha test (the game's helper, which also
    // selects position matrix 0), no culling, and the camera's view matrix
    // (gfCamera's first member, as setGX() loads it) in position matrix 0: the
    // helper does not load it and P+'s capsule renderer leaves each capsule's
    // there. Projection and viewport are the camera's (begin(), end() or
    // setGX()).
    void prepare()
    {
#ifdef TM_MODULE_MAIN
        TM_PERF_COUNT(kCountRects, 1);
#endif
        if (s_state == kStateShape && !TM_PERF_LEGACY())
        {
            return;
        }
        gfDrawSetVtxPosColorPrimEnvironment();
        GXSetCullMode(GX_CULL_NONE);
        GXLoadPosMtxImm(*reinterpret_cast<const Mtx*>(&gfCameraManager::getManager()->m_cameras[s_camera]),
                        GX_PNMTX0);
        s_state = s_camera == k2dCamera ? kStateShape : kStateNone;
    }

    // Only the extra module's DI Display draws lines: kept although unused
    // here.
#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    void lineBegin(int primitive, int count, u8 width)
    {
        prepare();
        GXSetLineWidth(width, 0);
        GXBegin(static_cast<GXPrimitive>(primitive), GX_VTXFMT1, static_cast<u16>(count));
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

    static inline void quad(float x, float y, float w, float h, Rgba color)
    {
        vertex(x, y, color);
        vertex(x + w, y, color);
        vertex(x + w, y + h, color);
        vertex(x, y + h, color);
    }

    void fillRect(float x, float y, float width, float height, Rgba color)
    {
        prepare();
        GXBegin(GX_QUADS, GX_VTXFMT1, 4);
        quad(x, y, width, height, color);
    }

#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    void fillGradV(float x, float y, float width, float height, Rgba top, Rgba bot)
    {
        prepare();
        GXBegin(GX_QUADS, GX_VTXFMT1, 4);
        vertex(x, y, top);
        vertex(x + width, y, top);
        vertex(x + width, y + height, bot);
        vertex(x, y + height, bot);
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
    void frameRect(float x, float y, float width, float height, Rgba color)
    {
        if (TM_PERF_LEGACY())
        {
            fillRect(x, y, width, 1.0f, color);
            fillRect(x, y + height - 1.0f, width, 1.0f, color);
            fillRect(x, y, 1.0f, height, color);
            fillRect(x + width - 1.0f, y, 1.0f, height, color);
            return;
        }
        prepare();
        GXBegin(GX_QUADS, GX_VTXFMT1, 16);
        quad(x, y, width, 1.0f, color);
        quad(x, y + height - 1.0f, width, 1.0f, color);
        quad(x, y, 1.0f, height, color);
        quad(x + width - 1.0f, y, 1.0f, height, color);
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

    void arrow(float x, float y, float size, int dir, Rgba color)
    {
        float tip = dir < 0 ? y : y + size;
        float base = dir < 0 ? y + size : y;
        prepare();
        GXBegin(GX_TRIANGLES, GX_VTXFMT1, 3);
        vertex(x, tip, color);
        vertex(x + size, base, color);
        vertex(x - size, base, color);
    }

    void fillTri(float x0, float y0, float x1, float y1, float x2, float y2, Rgba color)
    {
        prepare();
        GXBegin(GX_TRIANGLES, GX_VTXFMT1, 3);
        vertex(x0, y0, color);
        vertex(x1, y1, color);
        vertex(x2, y2, color);
    }

    static const float kCircle[24][2] = {
        {1.00000f, 0.00000f},   {0.96593f, 0.25882f},   {0.86603f, 0.50000f},   {0.70711f, 0.70711f},
        {0.50000f, 0.86603f},   {0.25882f, 0.96593f},   {0.00000f, 1.00000f},   {-0.25882f, 0.96593f},
        {-0.50000f, 0.86603f},  {-0.70711f, 0.70711f},  {-0.86603f, 0.50000f},  {-0.96593f, 0.25882f},
        {-1.00000f, 0.00000f},  {-0.96593f, -0.25882f}, {-0.86603f, -0.50000f}, {-0.70711f, -0.70711f},
        {-0.50000f, -0.86603f}, {-0.25882f, -0.96593f}, {0.00000f, -1.00000f},  {0.25882f, -0.96593f},
        {0.50000f, -0.86603f},  {0.70711f, -0.70711f},  {0.86603f, -0.50000f},  {0.96593f, -0.25882f}};

    void fillEllipse(float x, float y, float rx, float ry, Rgba color)
    {
        prepare();
        GXBegin(GX_TRIANGLEFAN, GX_VTXFMT1, 26);
        vertex(x, y, color);
        for (int i = 0; i <= 24; i++)
        {
            const float* c = kCircle[i % 24];
            vertex(x + c[0] * rx, y + c[1] * ry, color);
        }
    }
}
}
