#include "main/display/input_display.h"

#include <gf/gf_pad_status.h>

namespace tm {
namespace display {
    struct Raw {
        s8 sx, sy, cx, cy;
        u8 type;
    };

    static Raw s_pads[kPortPads];

    void inputCapture(const gfPadStatus* pads)
    {
        for (int i = 0; i < kPortPads; i++)
        {
            const gfPadStatus& p = pads[i];
            Raw& r = s_pads[i];
            r.sx = p.m_stickX;
            r.sy = p.m_stickY;
            r.cx = p.m_subStickX;
            r.cy = p.m_subStickY;
            r.type = static_cast<u8>(p.m_controllerType);
        }
    }

    // Only the extra module calls this: kept from the linker's stripping.
#pragma force_active on
    bool inputStickWithType(int port, s8* out, u8* controllerType)
    {
        if (port < 0 || port >= kPortPads)
        {
            return false;
        }
        const Raw& r = s_pads[port];
        out[0] = r.sx;
        out[1] = r.sy;
        out[2] = r.cx;
        out[3] = r.cy;
        if (controllerType != NULL)
        {
            *controllerType = r.type;
        }
        return true;
    }
#pragma force_active reset

    bool inputStick(int port, s8* out)
    {
        return inputStickWithType(port, out, NULL);
    }

    const PortPad* inputPortPad(int)
    {
        return NULL;
    }

    void inputFighterPad(u32, const gfPadStatus*)
    {
    }
}
}
