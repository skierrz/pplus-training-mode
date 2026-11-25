#pragma once

#include <types.h>

// A GameCube main stick (pad units) at P+'s scale, as the fighter's stick is
// made before Brawl's dead zone: IpHuman::update -> clampStick (0x8004819C,
// read in a P+ 3.2.1 memory dump). Beyond 100 units from the centre P+ puts
// it back on that circle and cuts it to whole units ("Directional Input
// Capped at Distance of 1" [Magus], DirectionalInput.asm:1-44), then times
// 0.010325 (:46-47; Brawl's is 0.0125), each axis clamped to -1..1. Brawl's
// 0.28 dead zone (applied next) is left out so raw pre-deadzone stick values
// remain visible. P+ scales by the hardware's square-root estimate;
// this uses an exact one, so a capped stick may differ by a unit (unchecked).
// Header only: both modules and the PC tests use it.
namespace tm {
namespace game {
    const float kStickScale = 0.010325f;
    const float kStickCap = 100.0f;

    inline float stickClamp(float v)
    {
        return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    }

    inline float stickRoot(float v)
    {
        float r = v > 1.0f ? v * 0.5f : 1.0f;
        for (int i = 0; i < 24; i++)
        {
            r = 0.5f * (r + v / r);
        }
        return r;
    }

    inline void mainStick(s8 px, s8 py, float* x, float* y)
    {
        float fx = px;
        float fy = py;
        float d = fx * fx + fy * fy;
        if (d > kStickCap * kStickCap)
        {
            float k = kStickCap / stickRoot(d);
            fx = static_cast<float>(static_cast<int>(fx * k));
            fy = static_cast<float>(static_cast<int>(fy * k));
        }
        *x = stickClamp(fx * kStickScale);
        *y = stickClamp(fy * kStickScale);
    }

    // The C-stick at P+'s scale: its analogue C-stick variables (instance
    // work floats 34-35; x is made relative to the fighter's facing there,
    // not here) are the C-stick times 0.011775 (0x3C40EBEE), clamped
    // to -1..1, no cap or dead zone ("Analog C-Stick, L R, & Light-Shield
    // Button Stored as Variables" [Magus, DukeItOut], ButtonPresses.asm:381-
    // 424): full at about 85 units.
    const float kCStickScale = 0.011775f;

    inline void cStick(s8 px, s8 py, float* x, float* y)
    {
        *x = stickClamp(px * kCStickScale);
        *y = stickClamp(py * kCStickScale);
    }
}
}
