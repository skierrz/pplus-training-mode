// Local call-site thunks for the extra module's most frequent calls into the
// main module (module 21582). Collapses hundreds of 8-byte kept REL relocation
// entries in TrainingModeExtra.rel down to one entry per target function.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)

#define TM_EXTRA_THUNK_IMPL 1
#include "main/cpu/view.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/motion.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/lab.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "main/ui/menu.h"
#include "shared/game/frame.h"

namespace tm {
namespace main {
    Settings& x_settings()
    {
        return settings();
    }
}

namespace gfx {
    void x_prepare()
    {
        prepare();
    }
    void x_forgetState()
    {
        forgetState();
    }
    float x_xScale()
    {
        return xScale();
    }
    bool x_widescreen()
    {
        return widescreen();
    }
    void x_fillRect(float x, float y, float width, float height, Rgba color)
    {
        fillRect(x, y, width, height, color);
    }
    void x_fillGradV(float x, float y, float width, float height, Rgba top, Rgba bot)
    {
        fillGradV(x, y, width, height, top, bot);
    }
    void x_frameRect(float x, float y, float width, float height, Rgba color)
    {
        frameRect(x, y, width, height, color);
    }
    void x_fillTri(float x0, float y0, float x1, float y1, float x2, float y2, Rgba color)
    {
        fillTri(x0, y0, x1, y1, x2, y2, color);
    }
    void x_lineBegin(int primitive, int count, u8 width)
    {
        lineBegin(primitive, count, width);
    }
    void x_vertex(float x, float y, Rgba color)
    {
        vertex(x, y, color);
    }
    void x_fillEllipse(float x, float y, float rx, float ry, Rgba color)
    {
        fillEllipse(x, y, rx, ry, color);
    }
}

namespace game {
    int x_listFighters(FighterRef* out, int max)
    {
        return listFighters(out, max);
    }
    bool x_findFighter(bool cpu, FighterRef& out)
    {
        return findFighter(cpu, out);
    }
    bool x_readState(const FighterRef& ref, FighterState& out)
    {
        return readState(ref, out);
    }
    const soModuleEnumeration* x_fighterModules(Fighter* fighter)
    {
        return fighterModules(fighter);
    }
    int x_fighterKind(int entryId, bool partner)
    {
        return fighterKind(entryId, partner);
    }
    int x_fighterStatus(const soModuleEnumeration* m)
    {
        return fighterStatus(m);
    }
    bool x_outOfPlay(int status, bool platform)
    {
        return outOfPlay(status, platform);
    }
    s32 x_trackerFrame()
    {
        return trackerFrame();
    }
    const Tracked* x_tracked(int i)
    {
        return tracked(i);
    }
    bool x_readBody(Fighter* fighter, Body& out)
    {
        return readBody(fighter, out);
    }
    s32 x_instanceInt(const soModuleEnumeration* m, u32 index)
    {
        return instanceInt(m, index);
    }
    bool x_readMotion(Fighter* fighter, MotionInfo& out)
    {
        return readMotion(fighter, out);
    }
    u32 x_frameCounter()
    {
        return frameCounter();
    }
}

namespace ui {
    u8* x_menuScratch()
    {
        return menuScratch();
    }

    namespace layout {
        Box x_topBand()
        {
            return topBand();
        }
    }
}

namespace lab {
    bool x_menuOpen()
    {
        return menuOpen();
    }
    void x_applyPercent(int side)
    {
        applyPercent(side);
    }
}

namespace cpu {
    bool x_canAct(const View& v)
    {
        return canAct(v);
    }
}
}

#endif
