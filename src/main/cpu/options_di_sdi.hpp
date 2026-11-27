// DI / SDI / ASDI / Survival flight calculations for CPU Options (Brain).
// Factored domain section included by options.cpp so -ipa file optimizes Brain as one TU.
#ifndef TM_OPTIONS_CPP_INTERNAL
#error "Include from main/cpu/options.cpp only"
#endif



namespace tm {
namespace cpu {
    // Survival: the full turn (+1 counter-clockwise, -1 clockwise) whose
    // flight, followed as the DI Display does, keeps farther from the blast
    // zones until hitstun ends; on a tie or with no blast zones, up.
    inline float Brain::survivalTurn(float kx, float ky, float len) const
    {
        float up = kx >= 0.0f ? 1.0f : -1.0f;
        void* camera = display::cameraController();
        if (camera == NULL)
        {
            return up;
        }
        const float* z = reinterpret_cast<const float*>(reinterpret_cast<u32>(camera) + 0x158);
        // The DI angle's sine and cosine (series to the 5th power: within
        // 1e-6 at 18 degrees), and the knockback decay (parameter 3294).
        float a = param(kParamDiAngle, 0.31416f), a2 = a * a;
        Flight fl = {z, -gravity, 3.0f, a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f)),
                     1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f), param(kParamDecay, 0.051f)};
        float gy = 0.0f;
        fallSpeeds(f, fl.accel, fl.maxFall, gy);
        return flight(fl, kx, ky, len, -up) > flight(fl, kx, ky, len, up) + 0.01f ? -up : up;
    }

    // Smallest blast zone distance (negative past one) along the flight of
    // launch (kx, ky) turned by `turn`.
    float Brain::flight(const Flight& fl, float kx, float ky, float len, float turn) const
    {
        const float* z = fl.zones;
        float t = turn * fl.sin;
        float dx = (kx * fl.cos - ky * t) / len, dy = (ky * fl.cos + kx * t) / len;
        float x = v.x, y = v.y, g = 0.0f, m = 100000.0f;
        for (int i = 0; i < v.hitstun && i < kMaxFlight; i++)
        {
            len = len > fl.decay ? len - fl.decay : 0.0f;
            g = g + fl.accel < -fl.maxFall ? -fl.maxFall : g + fl.accel;
            x += dx * len;
            y += dy * len + g;
            m = lesser(lesser(m, lesser(z[1] - x, x - z[0])), lesser(z[2] - y, y - z[3]));
        }
        return m;
    }

    // Trajectory DI from the launch vector (read from knockback energy
    // before DI is applied) for all 9 Tdi modes.
    void Brain::relativeDi()
    {
        if (diKind == kTdiNone)
        {
            tdiX = tdiY = 0.0f;
            return;
        }
        float kx = 0.0f, ky = 0.0f;
        if (!knockback(f, kx, ky) || (kx == 0.0f && ky == 0.0f))
        {
            kx = -toward;
            ky = 1.0f;
        }
        float len = length(kx, ky);
        float ux = kx / len, uy = ky / len;
        float st = diKind == kTdiSurvival ? survivalTurn(kx, ky, len) : 1.0f;
        const CpuTables& t = main::settings().cpuTables;
        computeTdiStick(diKind, ux, uy, toward, st, diRoll,
                        game::i2fScale(t.customDiX, 0.01f),
                        game::i2fScale(t.customDiY, 0.01f), tdiX, tdiY);
    }

    bool Brain::dirStick(int code, float away, float& x, float& y)
    {
        if (static_cast<u32>(code) >= 6u)
        {
            return false;
        }
        static const s8 kVec[6][2] = {{2, 0}, {-2, 0}, {-1, 0}, {1, 0}, {0, 1}, {0, -1}};
        int vx = kVec[code][0];
        x = vx == 2 ? away : (vx == -2 ? -away : game::i2f(vx));
        y = game::i2f(kVec[code][1]);
        return true;
    }

    inline void Brain::chooseDi(const Owner&)
    {
        const CpuOptions& c = opts();
        int kind = c.tdi;
        if (kind == kTdiRandom)
        {
            kind = resolveRandomTdi(cpuRnd());
        }
        float away = -toward;
        diKind = kind;
        diRoll = cpuRnd();
        relativeDi();
        // ASDI enum: 1=Away, 2=Towards, 3=Left, 4=Right, 5=Up, 6=Down
        haveAsdi = dirStick(c.asdi - kAsdiAway, away, asdiX, asdiY);
        const float d = 0.7071f;
        static const float kDirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {d, d}, {-d, d}, {d, -d}, {-d, -d}};
        int sdi = c.sdiDir;
        if (sdi == kSdiRandom)
        {
            int i = v.air ? cpuRndBelow(8) : cpuRndBelow(2);
            sdiX = kDirs[i][0];
            sdiY = kDirs[i][1];
            return;
        }
        if (sdi == kSdiAuto)
        {
            sdiX = tdiX;
            sdiY = tdiY;
            return;
        }
        sdiX = sdiY = 0.0f;
        // SDI enum: 2=Away, 3=Towards, 4=Left, 5=Right, 6=Up, 7=Down
        dirStick(sdi - kSdiAway, away, sdiX, sdiY);
    }

    // Smash DI: single-stick pushes every other hitlag frame; Dual-Stick
    // SDI alternates primary and secondary vectors on every hitlag frame
    // (hitlag > 1) up to sdiAmount. The last hitlag frame gets ASDI with
    // holdStill, the next gets trajectory DI:
    // docs/internals.md, "When P+ reads ASDI and trajectory DI".
    inline void Brain::hitlag(Frame& out)
    {
        const CpuOptions& c = opts();
        // Read again each hitlag frame: the launch speed is there before
        // DI, and smash DI moves the fighter (Survival's lines).
        relativeDi();
        HitlagSdiInput in;
        in.dualStick = c.dualStickSdi != 0;
        in.grounded = !v.air;
        in.hitlag = v.hitlag;
        in.hitlagFrame = hitlagFrame;
        in.sdiDone = sdiDone;
        in.sdiAmount = c.sdiAmount;
        in.sdiX = sdiX;
        in.sdiY = sdiY;
        in.haveAsdi = haveAsdi;
        in.asdiX = asdiX;
        in.asdiY = asdiY;
        in.tdiX = tdiX;
        in.tdiY = tdiY;
        in.awayDir = -toward;
        HitlagSdiOutput res = stepHitlagSdi(in);
        out.x = res.x;
        out.y = res.y;
        if (res.holdStill)
        {
            holdStill(slot);
        }
        if (res.consumedSdi)
        {
            sdiDone++;
        }
        hitlagFrame++;
    }
}
}
