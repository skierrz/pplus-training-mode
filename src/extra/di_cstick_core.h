#pragma once

#include <types.h>

#include "extra/net_alloc.h"
#include "shared/game/stick.h"

// Pure P+ 3.2.1 C-stick binding lookup and hitlag dual-stick DI/SDI/ASDI/TDI
// preview core. Header-only so both extra/di_display.cpp and PC host tests use
// the exact same rules.
//
// Sources (Project+ 3.2.1):
//  - ipPadConfig port map +0xB5 + playerNo, GC C-stick +contNo*0x0C+0x08,
//    Classic C-stick +0x4D+(contNo-4)*0x21, 6-byte C-stick state +0x15A+contNo*6
//    (0x8004A468, ButtonPresses.asm:10-17).
//  - fn_8004897C (0x8004897C, CStick.asm:149-293, DirectionalInput.asm:49):
//    active iff cx*cx + cy*cy > 2500; 1-frame main-stick override on fresh push:
//    Smash (5) / Smash+Charge (0xB): 1.0x normalized * 100;
//    Tilt (0xC): 0.50234377f * normalized * 100;
//    Attack (0): 0.32f * normalized * 100;
//    Special (1): raw C-stick; Taunt (0xA) / Jump / Shield / Grab: no override.
//  - C-stick ASDI (LA-Float[34..35]): raw C-stick * 0.011775f every frame,
//    independent of C-stick mode (ButtonPresses.asm:381-424,
//    DirectionalInfluence.asm:52-68).
//  - SDI trigger: soControllerImpl::update flick axis threshold 0.35f
//    (0x80764FC0, 0x80AD7528) and execStop threshold 0.7f (0x80876B00-6BC0).
namespace tm {
namespace extra {
    enum CStickMode {
        kCStickAttack = 0x0,
        kCStickSpecial = 0x1,
        kCStickJump = 0x2,
        kCStickShield = 0x3,
        kCStickGrab = 0x4,
        kCStickSmash = 0x5,
        kCStickTaunt = 0xA,
        kCStickSmashCharge = 0xB,
        kCStickTilt = 0xC,
        kCStickNone = 0xFF
    };

    enum PadKind {
        kPadGcc = 0,
        kPadClassic = 1,
        kPadWiimote = 2,
        kPadNunchuk = 3
    };

    enum {
        kPadConfigPorts = 0xB5,
        kPadConfigGcStride = 0x0C,
        kPadConfigGcCStick = 0x08,
        kPadConfigCcBase = 0x4D,
        kPadConfigCcStride = 0x21,
        kPadConfigStateBase = 0x15A,
        kPadConfigStateStride = 6,
        kCStickActiveSq = 2500
    };

    const float kSdiFlickAxis = 0.35f;
    const float kCStickUnitScale = 100.0f;
    const float kCStickTiltScale = 0.50234377f; // 0x3F00999A (CStick.asm:275-278)
    const float kCStickAttackScale = 0.32f;     // 0x3EA3D70A (0x805A181C)
    const float kMainDeadZone = 0.28f;

    inline u8 cstickModeFromConfig(const u8* cfg, int contNo, u8 controllerType)
    {
        if (cfg == NULL || contNo < 0 || contNo >= 8)
        {
            return kCStickNone;
        }
        if (controllerType == kPadGcc && contNo < 4)
        {
            return cfg[contNo * kPadConfigGcStride + kPadConfigGcCStick];
        }
        if (controllerType == kPadClassic && contNo >= 4)
        {
            return cfg[kPadConfigCcBase + (contNo - 4) * kPadConfigCcStride];
        }
        return kCStickNone;
    }

    inline bool cstickWasActiveFromConfig(const u8* cfg, int contNo, u8 mode)
    {
        if (cfg == NULL || contNo < 0 || contNo >= 8)
        {
            return false;
        }
        const u8* state = cfg + kPadConfigStateBase + contNo * kPadConfigStateStride;
        if (mode == kCStickSmash)
        {
            // fn_8004897C @ 0x80048B64-0x80048B90: byte 3 holds the smash hold
            // count (0 after sq < 2500, >= 1 while held); triggers only when
            // incremented value < 2 (i.e. prior byte 3 == 0).
            return state[3] != 0;
        }
        // fn_8004897C @ 0x800489E8-0x80048A28: byte 0 shifts left each active
        // frame and ORs bit 0 when sq > 2500; r22 == 1 iff prior bit 0 == 0.
        return (state[0] & 1u) != 0;
    }

    inline bool cstickOverridesMainStick(u8 mode)
    {
        return mode == kCStickSmash || mode == kCStickSmashCharge || mode == kCStickTilt || mode == kCStickAttack
               || mode == kCStickSpecial;
    }

    inline bool cstickActiveRaw(s8 cx, s8 cy)
    {
        int x = cx;
        int y = cy;
        return x * x + y * y > kCStickActiveSq;
    }

    inline bool cstickFreshPush(u8 mode, s8 cx, s8 cy, bool wasActive)
    {
        return cstickOverridesMainStick(mode) && cstickActiveRaw(cx, cy) && !wasActive;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    inline float mainDeadZone(float v)
    {
        return (v >= -kMainDeadZone && v <= kMainDeadZone) ? 0.0f : v;
    }

    inline bool stickPushed(float x, float y, float threshold)
    {
        return (x != 0.0f || y != 0.0f) && x * x + y * y >= threshold * threshold;
    }

    inline bool sdiTriggered(float prevX, float prevY, float curX, float curY, float threshold)
    {
        bool flickX = (curX >= kSdiFlickAxis && prevX < kSdiFlickAxis)
                      || (curX <= -kSdiFlickAxis && prevX > -kSdiFlickAxis);
        bool flickY = (curY >= kSdiFlickAxis && prevY < kSdiFlickAxis)
                      || (curY <= -kSdiFlickAxis && prevY > -kSdiFlickAxis);
        return (flickX || flickY) && stickPushed(curX, curY, threshold);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    inline s8 clampS8(int v)
    {
        if (v > 127)
        {
            return 127;
        }
        if (v < -128)
        {
            return -128;
        }
        return static_cast<s8>(v);
    }

    inline void effectiveMainStickForFrame(u8 mode, s8 rawMainX, s8 rawMainY, s8 rawCx, s8 rawCy, bool freshPush,
                                           float& outX, float& outY)
    {
        s8 effX = rawMainX;
        s8 effY = rawMainY;
        if (freshPush && cstickOverridesMainStick(mode) && cstickActiveRaw(rawCx, rawCy))
        {
            if (mode == kCStickSpecial)
            {
                effX = rawCx;
                effY = rawCy;
            }
            else
            {
                float fx = i2f(rawCx);
                float fy = i2f(rawCy);
                float len = game::stickRoot(fx * fx + fy * fy);
                float sc = (mode == kCStickTilt) ? kCStickTiltScale
                                                 : (mode == kCStickAttack ? kCStickAttackScale : 1.0f);
                float mul = len > 0.0f ? (kCStickUnitScale * sc / len) : 0.0f;
                effX = clampS8(f2i(fx * mul));
                effY = clampS8(f2i(fy * mul));
            }
        }
        game::mainStick(effX, effY, &outX, &outY);
        outX = mainDeadZone(outX);
        outY = mainDeadZone(outY);
    }

    struct HitlagPreviewInput {
        int hitlag;
        u8 mode;
        bool hasHumanPad;
        s8 rawMainX, rawMainY;
        s8 rawCx, rawCy;
        bool cstickWasActive;
        float prevMainX, prevMainY;
        float fallbackMainX, fallbackMainY;
        float threshold;
    };

    struct HitlagPreviewResult {
        int sdiCount;
        float sdiX[2];
        float sdiY[2];
        bool hasAsdi;
        float asdiX, asdiY;
        float tdiX, tdiY;
    };

    inline HitlagPreviewResult previewHitlagFrame(const HitlagPreviewInput& in)
    {
        HitlagPreviewResult res;
        res.sdiCount = 0;
        res.sdiX[0] = res.sdiY[0] = res.sdiX[1] = res.sdiY[1] = 0.0f;
        res.hasAsdi = false;
        res.asdiX = res.asdiY = 0.0f;
        res.tdiX = res.tdiY = 0.0f;

        float mainX = in.fallbackMainX;
        float mainY = in.fallbackMainY;
        float asdiCx = 0.0f;
        float asdiCy = 0.0f;
        if (in.hasHumanPad)
        {
            game::mainStick(in.rawMainX, in.rawMainY, &mainX, &mainY);
            mainX = mainDeadZone(mainX);
            mainY = mainDeadZone(mainY);
            game::cStick(in.rawCx, in.rawCy, &asdiCx, &asdiCy);
        }

        bool freshNext = in.hasHumanPad && cstickFreshPush(in.mode, in.rawCx, in.rawCy, in.cstickWasActive);
        float nextMainX = mainX;
        float nextMainY = mainY;
        if (freshNext)
        {
            effectiveMainStickForFrame(in.mode, in.rawMainX, in.rawMainY, in.rawCx, in.rawCy, true, nextMainX,
                                       nextMainY);
        }

        float ax = in.prevMainX;
        float ay = in.prevMainY;
        if (in.hitlag <= 1)
        {
            // Next active frame is the launch frame: no more SDI frames remain,
            // and a fresh C-stick push on launch overrides launch-frame TDI.
            res.tdiX = nextMainX;
            res.tdiY = nextMainY;
        }
        else
        {
            // Next hitlag frame uses nextMain (with any 1-frame C-stick override).
            if (sdiTriggered(in.prevMainX, in.prevMainY, nextMainX, nextMainY, in.threshold))
            {
                res.sdiX[res.sdiCount] = nextMainX;
                res.sdiY[res.sdiCount] = nextMainY;
                res.sdiCount++;
            }
            float curX = nextMainX;
            float curY = nextMainY;
            if (in.hitlag >= 3)
            {
                // Following hitlag frame: any held C-stick no longer overrides
                // main stick, so main stick returns and can trigger a second SDI.
                if (sdiTriggered(curX, curY, mainX, mainY, in.threshold))
                {
                    res.sdiX[res.sdiCount] = mainX;
                    res.sdiY[res.sdiCount] = mainY;
                    res.sdiCount++;
                }
                curX = mainX;
                curY = mainY;
            }
            ax = curX;
            ay = curY;
            res.tdiX = mainX;
            res.tdiY = mainY;
        }

        // C-stick ASDI (LA-Float[34..35]) is always raw and mode-independent.
        if (stickPushed(asdiCx, asdiCy, in.threshold))
        {
            ax = asdiCx;
            ay = asdiCy;
        }
        res.hasAsdi = stickPushed(ax, ay, in.threshold);
        res.asdiX = ax;
        res.asdiY = ay;
        return res;
    }
}
}
