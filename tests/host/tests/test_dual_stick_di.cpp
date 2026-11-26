// Host unit tests for P+ 3.2.1 Dual-Stick DI, C-Stick binding accuracy
// (src/extra/di_cstick_core.h), CPU Dual-Stick SDI (src/main/cpu/sdi_logic.h),
// and CpuOptions::dualStickSdi layout/storage persistence.
#include <math.h>
#include <string.h>

#include "extra/di_cstick_core.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/cpu/cpu_settings.h"
#include "main/cpu/sdi_logic.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/pages/pages.h"

using namespace tm;

namespace {
    const float kThresh = 0.7f;

    extra::HitlagPreviewInput makeHumanInput(int hitlag, u8 mode, s8 mx, s8 my, s8 cx, s8 cy, bool cstickWasActive,
                                             float prevX = 0.0f, float prevY = 0.0f)
    {
        extra::HitlagPreviewInput in;
        in.hitlag = hitlag;
        in.mode = mode;
        in.hasHumanPad = true;
        in.rawMainX = mx;
        in.rawMainY = my;
        in.rawCx = cx;
        in.rawCy = cy;
        in.cstickWasActive = cstickWasActive;
        in.prevMainX = prevX;
        in.prevMainY = prevY;
        in.fallbackMainX = 0.0f;
        in.fallbackMainY = 0.0f;
        in.threshold = kThresh;
        return in;
    }

    void bootStorage()
    {
        storage::detach();
        host::log::clear();
        u8* block = host::fakes::block();
        memset(block, 0, iface::kSettingsBlockSize);
        main::loadSettings(block);
        storage::attach(block);
    }
}

// ---- ipPadConfig C-stick binding lookup & state bytes -----------------------

TEST(dual_stick_di, ip_pad_config_lookup_covers_gc_classic_wiimote_and_bounds)
{
    u8 cfg[0x1A0];
    memset(cfg, 0, sizeof(cfg));

    // GC ports 0..3: offset contNo * 0x0C + 0x08.
    cfg[0 * 0x0C + 0x08] = extra::kCStickSmash;
    cfg[1 * 0x0C + 0x08] = extra::kCStickTilt;
    cfg[2 * 0x0C + 0x08] = extra::kCStickAttack;
    cfg[3 * 0x0C + 0x08] = extra::kCStickTaunt;

    // Classic ports 4..7: offset 0x4D + (contNo - 4) * 0x21.
    cfg[0x4D + 0 * 0x21] = extra::kCStickSmashCharge;
    cfg[0x4D + 1 * 0x21] = extra::kCStickSpecial;
    cfg[0x4D + 2 * 0x21] = extra::kCStickTilt;
    cfg[0x4D + 3 * 0x21] = extra::kCStickSmash;

    CHECK_EQ(extra::kCStickSmash, extra::cstickModeFromConfig(cfg, 0, extra::kPadGcc));
    CHECK_EQ(extra::kCStickTilt, extra::cstickModeFromConfig(cfg, 1, extra::kPadGcc));
    CHECK_EQ(extra::kCStickAttack, extra::cstickModeFromConfig(cfg, 2, extra::kPadGcc));
    CHECK_EQ(extra::kCStickTaunt, extra::cstickModeFromConfig(cfg, 3, extra::kPadGcc));

    CHECK_EQ(extra::kCStickSmashCharge, extra::cstickModeFromConfig(cfg, 4, extra::kPadClassic));
    CHECK_EQ(extra::kCStickSpecial, extra::cstickModeFromConfig(cfg, 5, extra::kPadClassic));
    CHECK_EQ(extra::kCStickTilt, extra::cstickModeFromConfig(cfg, 6, extra::kPadClassic));
    CHECK_EQ(extra::kCStickSmash, extra::cstickModeFromConfig(cfg, 7, extra::kPadClassic));

    // Wiimote and Nunchuk do not call fn_8004897C; mismatched port/type returns None.
    CHECK_EQ(extra::kCStickNone, extra::cstickModeFromConfig(cfg, 4, extra::kPadWiimote));
    CHECK_EQ(extra::kCStickNone, extra::cstickModeFromConfig(cfg, 4, extra::kPadNunchuk));
    CHECK_EQ(extra::kCStickNone, extra::cstickModeFromConfig(cfg, 0, extra::kPadClassic));
    CHECK_EQ(extra::kCStickNone, extra::cstickModeFromConfig(cfg, 4, extra::kPadGcc));
    CHECK_EQ(extra::kCStickNone, extra::cstickModeFromConfig(cfg, -1, extra::kPadGcc));
    CHECK_EQ(extra::kCStickNone, extra::cstickModeFromConfig(cfg, 8, extra::kPadClassic));
    CHECK_EQ(extra::kCStickNone, extra::cstickModeFromConfig(NULL, 0, extra::kPadGcc));

    // 6-byte C-stick state at +0x15A + contNo * 6:
    // Smash uses byte 3 (!= 0), r22 modes use byte 0 bit 0.
    cfg[0x15A + 0 * 6 + 3] = 1;
    CHECK(extra::cstickWasActiveFromConfig(cfg, 0, extra::kCStickSmash));
    CHECK(!extra::cstickWasActiveFromConfig(cfg, 0, extra::kCStickTilt));

    cfg[0x15A + 4 * 6 + 0] = 0x01;
    CHECK(extra::cstickWasActiveFromConfig(cfg, 4, extra::kCStickTilt));
    CHECK(extra::cstickWasActiveFromConfig(cfg, 4, extra::kCStickSmashCharge));
    CHECK(!extra::cstickWasActiveFromConfig(cfg, 4, extra::kCStickSmash));

    // Bit 1 alone (prior-prior frame) does not block r22 on next active frame.
    cfg[0x15A + 4 * 6 + 0] = 0x02;
    CHECK(!extra::cstickWasActiveFromConfig(cfg, 4, extra::kCStickTilt));
}

// ---- fn_8004897C fresh push threshold and per-mode stick scales -------------

TEST(dual_stick_di, cstick_fresh_push_and_all_mode_scales)
{
    // Exact boundary: cx*cx + cy*cy > 2500 (50*50 == 2500 does not trigger).
    CHECK(!extra::cstickFreshPush(extra::kCStickSmash, 50, 0, false));
    CHECK(extra::cstickFreshPush(extra::kCStickSmash, 51, 0, false));
    CHECK(!extra::cstickFreshPush(extra::kCStickSmash, 51, 0, true));
    CHECK(!extra::cstickFreshPush(extra::kCStickTaunt, 100, 0, false));
    CHECK(!extra::cstickFreshPush(extra::kCStickJump, 100, 0, false));
    CHECK(!extra::cstickFreshPush(extra::kCStickShield, 100, 0, false));
    CHECK(!extra::cstickFreshPush(extra::kCStickGrab, 100, 0, false));

    float x = 0.0f, y = 0.0f;

    // Smash (5) and Smash+Charge (0xB): normalized * 100 -> full 1.0 cardinal (>= 0.7).
    extra::effectiveMainStickForFrame(extra::kCStickSmash, -100, 0, 0, 60, true, x, y);
    CHECK_NEAR(0.0f, x, 0.001f);
    CHECK_NEAR(1.0f, y, 0.001f);
    CHECK(extra::stickPushed(x, y, kThresh));

    extra::effectiveMainStickForFrame(extra::kCStickSmashCharge, -100, 0, 70, 70, true, x, y);
    CHECK_NEAR(70.0f * game::kStickScale, x, 0.002f);
    CHECK_NEAR(70.0f * game::kStickScale, y, 0.002f);
    CHECK(extra::stickPushed(x, y, kThresh));

    // Tilt (0xC): 0.50234377 * 100 = 50 -> 50 * 0.010325 = 0.51625 (< 0.7, > 0.28).
    extra::effectiveMainStickForFrame(extra::kCStickTilt, 100, 0, 0, 100, true, x, y);
    CHECK_NEAR(0.0f, x, 0.001f);
    CHECK_NEAR(50.0f * game::kStickScale, y, 0.001f);
    CHECK(!extra::stickPushed(x, y, kThresh));

    // Attack (0): 0.32 * 100 = 32 -> 32 * 0.010325 = 0.3304 (< 0.7, < 0.35 flick, > 0.28).
    extra::effectiveMainStickForFrame(extra::kCStickAttack, 100, 0, 0, 100, true, x, y);
    CHECK_NEAR(0.0f, x, 0.001f);
    CHECK_NEAR(32.0f * game::kStickScale, y, 0.001f);
    CHECK(!extra::stickPushed(x, y, kThresh));

    // Attack (0) diagonal: 0.32 * 70.71 = 22 -> 0.22715 -> deadzoned to (0, 0).
    extra::effectiveMainStickForFrame(extra::kCStickAttack, 100, 0, 70, 70, true, x, y);
    CHECK_NEAR(0.0f, x, 0.001f);
    CHECK_NEAR(0.0f, y, 0.001f);

    // Special (1): raw C-stick passed straight through mainStick + deadZone.
    extra::effectiveMainStickForFrame(extra::kCStickSpecial, 0, 0, 60, 0, true, x, y);
    CHECK_NEAR(60.0f * game::kStickScale, x, 0.001f);
    CHECK(!extra::stickPushed(x, y, kThresh));

    extra::effectiveMainStickForFrame(extra::kCStickSpecial, 0, 0, 80, 0, true, x, y);
    CHECK_NEAR(80.0f * game::kStickScale, x, 0.001f);
    CHECK(extra::stickPushed(x, y, kThresh));

    // Taunt (0xA) and held C-stick: main stick is untouched.
    extra::effectiveMainStickForFrame(extra::kCStickTaunt, -100, 0, 0, 100, true, x, y);
    CHECK_NEAR(-1.0f, x, 0.001f);
    CHECK_NEAR(0.0f, y, 0.001f);

    extra::effectiveMainStickForFrame(extra::kCStickSmash, -100, 0, 0, 100, false, x, y);
    CHECK_NEAR(-1.0f, x, 0.001f);
    CHECK_NEAR(0.0f, y, 0.001f);
}

// ---- previewHitlagFrame across hitlag == 1 and hitlag > 1 -------------------

TEST(dual_stick_di, preview_launch_frame_cstick_tdi_override_and_asdi_independence)
{
    // hitlag == 1, fresh C-stick Up (0, 100), main stick Left (-100, 0):
    // Smash overrides launch-frame TDI to Up (0, 1), ASDI is Up (0, 1).
    extra::HitlagPreviewResult rSmash =
        extra::previewHitlagFrame(makeHumanInput(1, extra::kCStickSmash, -100, 0, 0, 100, false, -1.0f, 0.0f));
    CHECK_EQ(0, rSmash.sdiCount);
    CHECK(rSmash.hasAsdi);
    CHECK_NEAR(0.0f, rSmash.asdiX, 0.001f);
    CHECK_NEAR(1.0f, rSmash.asdiY, 0.001f);
    CHECK_NEAR(0.0f, rSmash.tdiX, 0.001f);
    CHECK_NEAR(1.0f, rSmash.tdiY, 0.001f);

    // Same push with C-stick ALREADY HELD: no TDI override (TDI stays Left),
    // but ASDI is still Up (mode- and hold-independent).
    extra::HitlagPreviewResult rHeld =
        extra::previewHitlagFrame(makeHumanInput(1, extra::kCStickSmash, -100, 0, 0, 100, true, -1.0f, 0.0f));
    CHECK_EQ(0, rHeld.sdiCount);
    CHECK(rHeld.hasAsdi);
    CHECK_NEAR(0.0f, rHeld.asdiX, 0.001f);
    CHECK_NEAR(1.0f, rHeld.asdiY, 0.001f);
    CHECK_NEAR(-1.0f, rHeld.tdiX, 0.001f);
    CHECK_NEAR(0.0f, rHeld.tdiY, 0.001f);

    // Tilt (0xC) fresh on launch frame: TDI overridden to ~0.516 Up; ASDI still 1.0 Up.
    extra::HitlagPreviewResult rTilt =
        extra::previewHitlagFrame(makeHumanInput(1, extra::kCStickTilt, -100, 0, 0, 100, false, -1.0f, 0.0f));
    CHECK(rTilt.hasAsdi);
    CHECK_NEAR(1.0f, rTilt.asdiY, 0.001f);
    CHECK_NEAR(0.0f, rTilt.tdiX, 0.001f);
    CHECK_NEAR(50.0f * game::kStickScale, rTilt.tdiY, 0.001f);

    // Attack (0) fresh on launch frame: TDI overridden to ~0.330 Up; ASDI still 1.0 Up.
    extra::HitlagPreviewResult rAtk =
        extra::previewHitlagFrame(makeHumanInput(1, extra::kCStickAttack, -100, 0, 0, 100, false, -1.0f, 0.0f));
    CHECK(rAtk.hasAsdi);
    CHECK_NEAR(1.0f, rAtk.asdiY, 0.001f);
    CHECK_NEAR(0.0f, rAtk.tdiX, 0.001f);
    CHECK_NEAR(32.0f * game::kStickScale, rAtk.tdiY, 0.001f);

    // Taunt (0xA) fresh on launch frame: NO TDI override (TDI stays Left),
    // ASDI still reads raw C-stick Up (1.0).
    extra::HitlagPreviewResult rTaunt =
        extra::previewHitlagFrame(makeHumanInput(1, extra::kCStickTaunt, -100, 0, 0, 100, false, -1.0f, 0.0f));
    CHECK(rTaunt.hasAsdi);
    CHECK_NEAR(1.0f, rTaunt.asdiY, 0.001f);
    CHECK_NEAR(-1.0f, rTaunt.tdiX, 0.001f);
    CHECK_NEAR(0.0f, rTaunt.tdiY, 0.001f);

    // Partial C-stick (55, 0): sq=3025 > 2500 triggers Smash TDI override (1.0 Right),
    // while raw C-stick ASDI = 55 * 0.011775 = 0.648 (< 0.7) falls back to prevMain Left.
    extra::HitlagPreviewResult rPartial =
        extra::previewHitlagFrame(makeHumanInput(1, extra::kCStickSmash, -100, 0, 55, 0, false, -1.0f, 0.0f));
    CHECK(rPartial.hasAsdi);
    CHECK_NEAR(-1.0f, rPartial.asdiX, 0.001f);
    CHECK_NEAR(1.0f, rPartial.tdiX, 0.001f);
}

TEST(dual_stick_di, preview_hitlag_dual_stick_sdi_pulses_per_cstick_mode)
{
    // Main stick already held Right (prevMain = (1, 0), rawMain = (100, 0)),
    // hitlag = 4, fresh C-stick Up (0, 100):
    //  - Smash / Smash+Charge: 2 SDI pulses (Up on push frame, then Right on return frame).
    for (int m = 0; m < 2; m++)
    {
        u8 mode = m == 0 ? extra::kCStickSmash : extra::kCStickSmashCharge;
        extra::HitlagPreviewResult r =
            extra::previewHitlagFrame(makeHumanInput(4, mode, 100, 0, 0, 100, false, 1.0f, 0.0f));
        CHECK_EQ(2, r.sdiCount);
        CHECK_NEAR(0.0f, r.sdiX[0], 0.001f);
        CHECK_NEAR(1.0f, r.sdiY[0], 0.001f);
        CHECK_NEAR(1.0f, r.sdiX[1], 0.001f);
        CHECK_NEAR(0.0f, r.sdiY[1], 0.001f);
        CHECK(r.hasAsdi);
        CHECK_NEAR(1.0f, r.asdiY, 0.001f);
        CHECK_NEAR(1.0f, r.tdiX, 0.001f);
    }

    //  - Tilt (0xC) Up while main held Right: 0 SDI on push frame (< 0.7),
    //    resets X to 0 (< 0.35), so return frame gives 1 SDI pulse Right!
    extra::HitlagPreviewResult rTiltUp =
        extra::previewHitlagFrame(makeHumanInput(4, extra::kCStickTilt, 100, 0, 0, 100, false, 1.0f, 0.0f));
    CHECK_EQ(1, rTiltUp.sdiCount);
    CHECK_NEAR(1.0f, rTiltUp.sdiX[0], 0.001f);
    CHECK_NEAR(0.0f, rTiltUp.sdiY[0], 0.001f);
    CHECK(rTiltUp.hasAsdi);
    CHECK_NEAR(1.0f, rTiltUp.asdiY, 0.001f);

    //  - Tilt (0xC) Right while main held Right: push frame X = 0.516 (>= 0.35),
    //    so X never drops below 0.35 -> 0 SDI pulses total!
    extra::HitlagPreviewResult rTiltSame =
        extra::previewHitlagFrame(makeHumanInput(4, extra::kCStickTilt, 100, 0, 100, 0, false, 1.0f, 0.0f));
    CHECK_EQ(0, rTiltSame.sdiCount);

    //  - Attack (0) Right while main held Right: push frame X = 0.330 (< 0.35),
    //    so return frame flicks X -> 1 SDI pulse Right!
    extra::HitlagPreviewResult rAtkSame =
        extra::previewHitlagFrame(makeHumanInput(4, extra::kCStickAttack, 100, 0, 100, 0, false, 1.0f, 0.0f));
    CHECK_EQ(1, rAtkSame.sdiCount);
    CHECK_NEAR(1.0f, rAtkSame.sdiX[0], 0.001f);

    //  - Taunt (0xA) Up while main held Right: no stick override, main unchanged -> 0 SDI,
    //    but ASDI is still Up (1.0).
    extra::HitlagPreviewResult rTaunt =
        extra::previewHitlagFrame(makeHumanInput(4, extra::kCStickTaunt, 100, 0, 0, 100, false, 1.0f, 0.0f));
    CHECK_EQ(0, rTaunt.sdiCount);
    CHECK(rTaunt.hasAsdi);
    CHECK_NEAR(1.0f, rTaunt.asdiY, 0.001f);
    CHECK_NEAR(1.0f, rTaunt.tdiX, 0.001f);

    //  - hitlag == 2 with fresh Smash C-stick Up: only 1 hitlag frame left, so
    //    1 SDI pulse (Up); launch frame uses main stick Right for TDI.
    extra::HitlagPreviewResult rLag2 =
        extra::previewHitlagFrame(makeHumanInput(2, extra::kCStickSmash, 100, 0, 0, 100, false, 1.0f, 0.0f));
    CHECK_EQ(1, rLag2.sdiCount);
    CHECK_NEAR(0.0f, rLag2.sdiX[0], 0.001f);
    CHECK_NEAR(1.0f, rLag2.sdiY[0], 0.001f);
    CHECK_NEAR(1.0f, rLag2.tdiX, 0.001f);
}

// ---- CPU Dual-Stick SDI (src/main/cpu/sdi_logic.h) --------------------------

TEST(dual_stick_di, cpu_dual_stick_secondary_vectors_flick_both_ways_and_avoid_down_when_grounded)
{
    const float d = 0.70710678f;
    static const float kPrims[][2] = {
        {1.0f, 0.0f},  {-1.0f, 0.0f}, {0.0f, 1.0f},  {0.0f, -1.0f},
        {d, d},        {-d, d},       {d, -d},       {-d, -d},
        {0.92f, 0.39f}, {-0.39f, 0.92f}, {0.85f, -0.52f}, {-0.60f, -0.80f}
    };
    for (u32 i = 0; i < sizeof(kPrims) / sizeof(kPrims[0]); i++)
    {
        float px = kPrims[i][0];
        float py = kPrims[i][1];
        for (int g = 0; g < 2; g++)
        {
            bool grounded = g != 0;
            float sx = 0.0f, sy = 0.0f;
            cpu::dualStickSecondaryVector(px, py, grounded, -1.0f, sx, sy);
            float mag = sqrtf(sx * sx + sy * sy);
            CHECK_NEAR(1.0f, mag, 0.01f);
            CHECK(mag >= cpu::kSdiThreshold);
            CHECK(cpu::sdiSector8(px, py) != cpu::sdiSector8(sx, sy));
            CHECK(px * sx + py * sy > 0.25f);
            // Both transitions must trigger P+'s 0.35f axis flick rule.
            CHECK(cpu::sdiFlicksInGame(px, py, sx, sy));
            CHECK(cpu::sdiFlicksInGame(sx, sy, px, py));
            if (grounded && fabs(py) < 0.01f)
            {
                CHECK(sy > 0.0f);
            }
        }
    }
    // Zero primary yields zero cleanly.
    float zx = 1.0f, zy = 1.0f;
    cpu::dualStickSecondaryVector(0.0f, 0.0f, true, 1.0f, zx, zy);
    CHECK_NEAR(0.0f, zx, 0.001f);
    CHECK_NEAR(0.0f, zy, 0.001f);
}

TEST(dual_stick_di, cpu_hitlag_cadence_single_vs_dual_stick)
{
    // Simulate a 6-frame hitlag window (hitlag = 6, 5, 4, 3, 2, 1) with sdiAmount = 7:
    //  - Single-stick: 3 SDI shifts (frames 0, 2, 4), neutral on frames 1, 3, ASDI + holdStill on frame 5.
    //  - Dual-stick:   5 consecutive SDI shifts (frames 0..4), ASDI + holdStill on frame 5.
    for (int dual = 0; dual < 2; dual++)
    {
        int sdiDone = 0;
        int realShifts = 0;
        float prevX = 0.0f, prevY = 0.0f;
        for (int f = 0; f < 6; f++)
        {
            cpu::HitlagSdiInput in;
            in.dualStick = dual != 0;
            in.grounded = true;
            in.hitlag = 6 - f;
            in.hitlagFrame = f;
            in.sdiDone = sdiDone;
            in.sdiAmount = 7;
            in.sdiX = -1.0f;
            in.sdiY = 0.0f;
            in.haveAsdi = true;
            in.asdiX = 0.0f;
            in.asdiY = 1.0f;
            in.tdiX = 0.0f;
            in.tdiY = -1.0f;
            in.awayDir = -1.0f;
            cpu::HitlagSdiOutput out = cpu::stepHitlagSdi(in);
            if (in.hitlag <= 1)
            {
                CHECK(out.holdStill);
                CHECK(!out.consumedSdi);
                CHECK_NEAR(0.0f, out.x, 0.001f);
                CHECK_NEAR(1.0f, out.y, 0.001f);
            }
            else
            {
                CHECK(!out.holdStill);
                if (out.consumedSdi)
                {
                    sdiDone++;
                }
                if (cpu::sdiFlicksInGame(prevX, prevY, out.x, out.y))
                {
                    realShifts++;
                }
                prevX = out.x;
                prevY = out.y;
            }
        }
        CHECK_EQ(dual ? 5 : 3, sdiDone);
        CHECK_EQ(dual ? 5 : 3, realShifts);
    }

    // sdiAmount cap respected in Dual-Stick mode (sdiAmount = 3 -> exactly 3 shifts).
    int sdiDone = 0;
    for (int f = 0; f < 6; f++)
    {
        cpu::HitlagSdiInput in = {true, false, 7 - f, f, sdiDone, 3, 0.0f, 1.0f, false, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f};
        cpu::HitlagSdiOutput out = cpu::stepHitlagSdi(in);
        if (out.consumedSdi)
        {
            sdiDone++;
        }
        if (f >= 3)
        {
            CHECK(!out.consumedSdi);
            CHECK_NEAR(0.0f, out.x, 0.001f);
            CHECK_NEAR(0.0f, out.y, 0.001f);
        }
    }
    CHECK_EQ(3, sdiDone);
}

// ---- Settings layout, CPU Options row, and storage persistence --------------

TEST(dual_stick_di, settings_layout_menu_row_and_storage_round_trip)
{
    CHECK_EQ(61u, sizeof(cpu::CpuOptions));
    CHECK_EQ(64u, sizeof(main::CpuSettings));
    CHECK_EQ(464u, sizeof(main::Settings));

    // Dual-Stick SDI lives on CPU > DI & SDI (P_CPU_DI, row 4); counters live on P_CPU_COUNTERS;
    // P+ Tech OSD lives on P_OSD_MENU (row 5).
    const ui::Page& diPage = ui::pages::kCpuDi;
    REQUIRE(diPage.count == 6);
    CHECK(strcmp(diPage.options[4].name, "Dual-Stick SDI") == 0);
    CHECK_EQ(ui::kKindToggle, diPage.options[4].kind);
    CHECK_EQ(0, diPage.options[4].def);
    CHECK(strcmp(diPage.options[5].name, "ASDI Direction") == 0);

    const ui::Page& cntPage = ui::pages::kCpuCounters;
    REQUIRE(cntPage.count == 12);
    CHECK(strcmp(cntPage.options[0].name, "Counter Mode") == 0);
    CHECK(strcmp(cntPage.options[4].name, "Shield Timing") == 0);
    CHECK(strcmp(cntPage.options[6].name, "Custom Pools") == 0);
    CHECK(strcmp(cntPage.options[8].name, "Reversal Slots") == 0);
    CHECK(strcmp(cntPage.options[9].name, "Adaptive CPU") == 0);
    CHECK(strcmp(cntPage.options[10].name, "Auto L-Cancel") == 0);
    CHECK(strcmp(cntPage.options[11].name, "SHFFL Timing") == 0);

    const ui::Page& osdPage = ui::pages::kOsdMenu;
    REQUIRE(osdPage.count == 26);
    CHECK(strcmp(osdPage.options[4].name, "Tech Sparkle") == 0);
    CHECK(strcmp(osdPage.options[5].name, "P+ Tech OSD") == 0);

    host::card::reset();
    bootStorage();
    CHECK_EQ(0, main::settings().cpu.options.dualStickSdi);
    main::settings().cpu.options.dualStickSdi = 1;
    storage::settingsMenuClosed();
    for (int i = 0; i < 4; i++)
    {
        storage::settingsUpdate();
    }
    storage::detach();

    bootStorage();
    CHECK_EQ(1, main::settings().cpu.options.dualStickSdi);
    storage::detach();

    // Older kAreaCpu truncated before dualStickSdi keeps default 0.
    const char* file = "settings-a.bin";
    REQUIRE(host::card::has(file));
    u8* raw = host::card::data(file);
    storage::SettingsHead* head = reinterpret_cast<storage::SettingsHead*>(raw + sizeof(storage::FileHeader));
    storage::AreaEntry* table =
        reinterpret_cast<storage::AreaEntry*>(raw + sizeof(storage::FileHeader) + sizeof(storage::SettingsHead));
    storage::AreaEntry* cpuArea = NULL;
    for (int i = 0; i < head->areaCount; i++)
    {
        if (table[i].id == storage::kAreaCpu)
        {
            cpuArea = &table[i];
            break;
        }
    }
    REQUIRE(cpuArea != NULL);
    cpuArea->used = offsetof(main::CpuSettings, options) + offsetof(cpu::CpuOptions, dualStickSdi);
    storage::FileHeader* fh = reinterpret_cast<storage::FileHeader*>(raw);
    fh->checksum = storage::checksum(raw + storage::kChecksumFrom, fh->size - storage::kChecksumFrom);

    bootStorage();
    CHECK_EQ(0, main::settings().cpu.options.dualStickSdi);
    storage::detach();
}
