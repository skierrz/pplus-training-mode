// Host unit tests for clean-room hitbox heatmap core (src/extra/hitbox_heatmap_core.h)
// and DisplaySettings heatmap field sanitization.
#include <string.h>

#include "extra/hitbox_heatmap_core.h"
#include "host/fakes.h"
#include "host/test.h"
#include "main/settings.h"

using namespace tm;

TEST(hitbox_heatmap, ring_buffer_wrap_around_and_expiry)
{
    static extra::HeatmapRing ring;
    ring.clear();
    CHECK_EQ(0u, static_cast<unsigned>(ring.count));
    CHECK_EQ(0u, static_cast<unsigned>(ring.head));

    extra::HeatmapEntry e;
    memset(&e, 0, sizeof(e));
    extra::identityMatrix34(e.scaleMatrix);
    e.radius = 4.5f;
    e.life = 3;

    // Push past capacity (kHeatmapRing + 25) and verify count stays capped at kHeatmapRing.
    const int total = extra::kHeatmapRing + 25;
    for (int i = 0; i < total; i++)
    {
        e.frameOffset = static_cast<u8>(i & 0xFF);
        e.pos1[0] = static_cast<float>(i);
        ring.push(e);
    }
    CHECK_EQ(static_cast<unsigned>(extra::kHeatmapRing), static_cast<unsigned>(ring.count));
    CHECK_EQ(static_cast<unsigned>(total % extra::kHeatmapRing), static_cast<unsigned>(ring.head));
    CHECK_EQ(static_cast<unsigned>(total), ring.totalPushed);

    // Slot 0 was overwritten by entry index kHeatmapRing (192).
    CHECK_NEAR(static_cast<double>(extra::kHeatmapRing), ring.entries[0].pos1[0], 0.001);

    // Tick life 3 -> 2 -> 1 -> 0: all entries expire cleanly.
    ring.tickAge();
    CHECK_EQ(static_cast<unsigned>(extra::kHeatmapRing), static_cast<unsigned>(ring.count));
    ring.tickAge();
    CHECK_EQ(static_cast<unsigned>(extra::kHeatmapRing), static_cast<unsigned>(ring.count));
    ring.tickAge();
    CHECK_EQ(0u, static_cast<unsigned>(ring.count));
}

TEST(hitbox_heatmap, color_bands_guard_zero_step_and_scale_opacity)
{
    // colorChangeFrame == 0 must clamp to 1 without dividing by zero.
    CHECK_EQ(1u, static_cast<unsigned>(extra::clampColorFrames(0)));
    CHECK_EQ(5u, static_cast<unsigned>(extra::clampColorFrames(5)));

    u32 c0 = extra::heatmapColor(0, 0, 200);
    CHECK_EQ(0xFF0000C8u, c0);
    u32 c1 = extra::heatmapColor(1, 0, 200);
    CHECK_EQ(0xFFFF00C8u, c1);
    u32 c4 = extra::heatmapColor(4, 0, 200);
    CHECK_EQ(0x0000FFC8u, c4);

    // With step = 5: frame 0 red, frame 5 yellow, frame 10 green, frame 15 cyan, frame 20+ blue.
    CHECK_EQ(0xFF0000FFu, extra::heatmapColor(0, 5, 255));
    CHECK_EQ(0xFFFF00FFu, extra::heatmapColor(5, 5, 255));
    CHECK_EQ(0x00FF00FFu, extra::heatmapColor(10, 5, 255));
    CHECK_EQ(0x00FFFFFFu, extra::heatmapColor(15, 5, 255));
    CHECK_EQ(0x0000FFFFu, extra::heatmapColor(20, 5, 255));
    CHECK_EQ(0x0000FFFFu, extra::heatmapColor(255, 5, 255));

    // Opacity percent to translucent per-capsule byte scaling (0..100 -> 0..kHeatmapMaxAlpha=120).
    CHECK_EQ(0u, static_cast<unsigned>(extra::opacityPercentToAlpha(0)));
    CHECK_EQ(24u, static_cast<unsigned>(extra::opacityPercentToAlpha(20)));
    CHECK_EQ(72u, static_cast<unsigned>(extra::opacityPercentToAlpha(60)));
    CHECK_EQ(120u, static_cast<unsigned>(extra::opacityPercentToAlpha(100)));
    CHECK_EQ(120u, static_cast<unsigned>(extra::opacityPercentToAlpha(200)));

    // Shade color halves RGB channels while keeping alpha intact.
    CHECK_EQ(0x7F3F1FC8u, extra::heatmapShadeColor(0xFF7F3FC8u));
}

TEST(hitbox_heatmap, z_plane_capsule_aabb_bounds)
{
    extra::HitboxBounds b;
    extra::boundsReset(b);
    CHECK_EQ(0u, static_cast<unsigned>(b.valid));

    // Capsule completely outside the Z=0 plane (|z| >= r on both ends, same side): ignored.
    const float far1[3] = {10.0f, 5.0f, 8.0f};
    const float far2[3] = {12.0f, 6.0f, 5.0f};
    CHECK(!extra::boundsAccumulateCapsule(b, 0.0f, 0.0f, far1, far2, 4.0f));
    CHECK_EQ(0u, static_cast<unsigned>(b.valid));

    // Zero/negative radius ignored.
    const float z0[3] = {5.0f, 5.0f, 0.0f};
    CHECK(!extra::boundsAccumulateCapsule(b, 0.0f, 0.0f, z0, z0, 0.0f));
    CHECK_EQ(0u, static_cast<unsigned>(b.valid));

    // Capsule intersecting Z=0 at both endpoints relative to fighter origin (10, 2).
    const float a1[3] = {15.0f, 6.0f, 0.0f};
    const float a2[3] = {20.0f, 10.0f, 1.0f};
    CHECK(extra::boundsAccumulateCapsule(b, 10.0f, 2.0f, a1, a2, 3.0f));
    CHECK_EQ(1u, static_cast<unsigned>(b.valid));
    // World X: [15-3, 20+3] = [12, 23]; relative to originX=10 -> [2, 13], width=11.
    // World Y: [6-3, 10+3] = [3, 13]; relative to originY=2 -> [1, 11], height=10.
    CHECK_NEAR(12.0, b.worldXMin, 0.001);
    CHECK_NEAR(23.0, b.worldXMax, 0.001);
    CHECK_NEAR(3.0, b.worldYMin, 0.001);
    CHECK_NEAR(13.0, b.worldYMax, 0.001);
    CHECK_NEAR(2.0, b.xmin, 0.001);
    CHECK_NEAR(13.0, b.xmax, 0.001);
    CHECK_NEAR(1.0, b.ymin, 0.001);
    CHECK_NEAR(11.0, b.ymax, 0.001);
    CHECK_NEAR(11.0, b.width, 0.001);
    CHECK_NEAR(10.0, b.height, 0.001);

    // Segment crossing Z=0 from z=-10 to z=+10 with r=2: intersection at midpoint (30, 8, 0).
    const float c1[3] = {28.0f, 8.0f, -10.0f};
    const float c2[3] = {32.0f, 8.0f, 10.0f};
    CHECK(extra::boundsAccumulateCapsule(b, 10.0f, 2.0f, c1, c2, 2.0f));
    CHECK_NEAR(32.0, b.worldXMax, 0.001);
    CHECK_NEAR(20.0, b.width, 0.001);
}

TEST(hitbox_heatmap, actionable_ground_frame_tracking_and_settings_sanitization)
{
    u8 frames = 0;
    frames = extra::stepActionableGroundFrames(frames, 0x24, 1.0f); // Attack
    CHECK_EQ(1u, static_cast<unsigned>(frames));
    frames = extra::stepActionableGroundFrames(frames, 0x16, 4.0f); // Early landing <= 4f keeps counting
    CHECK_EQ(2u, static_cast<unsigned>(frames));
    frames = extra::stepActionableGroundFrames(frames, 0x16, 5.0f); // Late landing > 4f resets
    CHECK_EQ(0u, static_cast<unsigned>(frames));

    frames = 10;
    CHECK_EQ(0u, static_cast<unsigned>(extra::stepActionableGroundFrames(frames, 0x0, 0.0f)));  // Wait
    CHECK_EQ(0u, static_cast<unsigned>(extra::stepActionableGroundFrames(frames, 0x1, 0.0f)));  // Walk
    CHECK_EQ(0u, static_cast<unsigned>(extra::stepActionableGroundFrames(frames, 0x12, 0.0f))); // SquatWait
    CHECK_EQ(0u, static_cast<unsigned>(extra::stepActionableGroundFrames(frames, 0x1B, 0.0f))); // Guard
    CHECK_EQ(0u, static_cast<unsigned>(extra::stepActionableGroundFrames(frames, 0x49, 0.0f))); // Tumble
    CHECK_EQ(255u, static_cast<unsigned>(extra::stepActionableGroundFrames(255, 0x24, 1.0f)));  // Saturates at 255

    // DisplaySettings defaults and clamping.
    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);
    const main::DisplaySettings& def = main::settings().display;
    CHECK_EQ(0u, static_cast<unsigned>(def.heatmap));
    CHECK_EQ(0u, static_cast<unsigned>(def.heatmapBounds));
    CHECK_EQ(60u, static_cast<unsigned>(def.heatmapOpacity));
    CHECK_EQ(5u, static_cast<unsigned>(def.heatmapColorFrames));

    main::DisplaySettings& d = main::settings().display;
    d.heatmap = 99;
    d.heatmapBounds = 9;
    d.heatmapOpacity = 0;
    d.heatmapColorFrames = 0;
    main::sanitizeSettings(main::settings());
    CHECK_EQ(3u, static_cast<unsigned>(d.heatmap));
    CHECK_EQ(1u, static_cast<unsigned>(d.heatmapBounds));
    CHECK_EQ(20u, static_cast<unsigned>(d.heatmapOpacity));
    CHECK_EQ(1u, static_cast<unsigned>(d.heatmapColorFrames));

    d.heatmapOpacity = 250;
    d.heatmapColorFrames = 250;
    main::sanitizeSettings(main::settings());
    CHECK_EQ(100u, static_cast<unsigned>(d.heatmapOpacity));
    CHECK_EQ(30u, static_cast<unsigned>(d.heatmapColorFrames));
}
