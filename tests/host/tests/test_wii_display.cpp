// Slow-mo button latching across all 8 pad ports in src/main/display/display.cpp:
// Game Speed < 1 must latch short presses on Wii ports 4-7 just like GC ports 0-3.
#define TM_MODULE_MAIN 1

#include <gf/gf_application.h>
#include <gf/gf_pad_status.h>
#include <string.h>

#include "host/fakes.h"
#include "host/test.h"
#include "main/display/debug_draw.h"
#include "main/display/display.h"
#include "main/display/input_display.h"
#include "main/extra.h"
#include "main/game/fighters.h"
#include "main/game/overlays.h"
#include "main/lab.h"
#include "main/settings.h"
#include "shared/game/mem.h"
#include "shared/game/pad_map.h"
#include "shared/interface.h"
#include "shared/log.h"

namespace tm {
namespace display {
    void inputCapture(const gfPadStatus*) {}
    void debugDrawCheck() {}
    bool boxesAvailable() { return false; }
    bool collisionAvailable() { return false; }
    void debugDrawBegin() {}
    void debugDrawEnd() {}
    void drawBoxes(Fighter*) {}
    void drawEcb(Fighter*) {}
    void drawStageCollision() {}
}
}

#include "main/display/display.cpp"

using namespace tm;

TEST(wii_display, slow_mo_latches_short_press_on_wii_port_4)
{
    gfPadStatus pads[iface::kPadCount];
    memset(pads, 0, sizeof(pads));
    for (int i = 0; i < iface::kPadCount; i++)
    {
        pads[i].m_error = static_cast<s8>(i == 4 ? gfPadError::NONE : gfPadError::NO_CONTROLLER);
        pads[i].m_controllerType = static_cast<gfPadType::PadType>(i < 4 ? gfPadType::GCC : gfPadType::WII_CLASSIC);
    }

    // Set frame rate to 30 fps (slow-mo: < 60).
    *reinterpret_cast<u16*>(g_gfApplication->raw + 0xF8) = 30;
    display::attach();

    // Pass 1: neutral.
    display::systemPads(pads);
    CHECK_EQ(0u, pads[4].m_buttonsPressedThisFrame.bits);

    // Pass 2: 1-pass press of A on Wii port 5 (index 4).
    pads[4].m_buttonsCurrentFrame.bits = game::kPadA;
    pads[4].m_buttonsPressedThisFrame.bits = game::kPadA;
    pads[4].m_buttonsPressedThisFrame2.bits = game::kPadA;
    display::systemPads(pads);
    CHECK_EQ(static_cast<u32>(game::kPadA), pads[4].m_buttonsPressedThisFrame.bits);

    // Pass 3 (before any game step): button already released, must STAY latched!
    pads[4].m_buttonsCurrentFrame.bits = 0;
    pads[4].m_buttonsPressedThisFrame.bits = 0;
    pads[4].m_buttonsPressedThisFrame2.bits = 0;
    display::systemPads(pads);
    CHECK_EQ(static_cast<u32>(game::kPadA), pads[4].m_buttonsPressedThisFrame.bits);
    CHECK_EQ(static_cast<u32>(game::kPadA), pads[4].m_buttonsPressedThisFrame2.bits);

    // Game step runs (capturePads clears pending), next pass has 0.
    display::capturePads(pads);
    pads[4].m_buttonsPressedThisFrame.bits = 0;
    pads[4].m_buttonsPressedThisFrame2.bits = 0;
    display::systemPads(pads);
    CHECK_EQ(0u, pads[4].m_buttonsPressedThisFrame.bits);
    display::detach();
}
