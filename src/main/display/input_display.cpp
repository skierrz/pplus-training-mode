#include "main/display/input_display.h"

#include <gf/gf_pad_status.h>

namespace tm {
namespace display {
    struct Raw {
        u32 buttons;
        s8 sx, sy, cx, cy;
        u8 lTrig, rTrig;
        u8 type;
        u8 connected;
    };

    static Raw s_pads[kPortPads];

    void inputCapture(const gfPadStatus* pads)
    {
        for (int i = 0; i < kPortPads; i++)
        {
            const gfPadStatus& p = pads[i];
            Raw& r = s_pads[i];
            r.buttons = p.m_buttonsCurrentFrame.bits;
            r.sx = p.m_stickX;
            r.sy = p.m_stickY;
            r.cx = p.m_subStickX;
            r.cy = p.m_subStickY;
            r.lTrig = static_cast<u8>(p.m_lTriggerAnalog);
            r.rTrig = static_cast<u8>(p.m_rTriggerAnalog);
            r.type = static_cast<u8>(p.m_controllerType);
            r.connected = (p.m_error == gfPadError::NONE) ? 1 : 0;
        }
    }

    // Only the extra module calls these: kept from the linker's stripping.
#ifdef TM_MODULE_MAIN
#pragma force_active on
#endif
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

    bool inputViewerPad(int port, ViewerPad& out)
    {
        if (port < 0 || port >= kPortPads)
        {
            return false;
        }
        const Raw& r = s_pads[port];
        out.buttons = r.buttons;
        out.sx = r.sx;
        out.sy = r.sy;
        out.cx = r.cx;
        out.cy = r.cy;
        out.lTrig = r.lTrig;
        out.rTrig = r.rTrig;
        out.controllerType = r.type;
        out.connected = r.connected;
        return true;
    }
#ifdef TM_MODULE_MAIN
#pragma force_active reset
#endif

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
