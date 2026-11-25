// Host unit tests for Grab / Tether / Item-Pickup Visualizer pure logic
// (src/extra/grab_visualizer_core.h), DisplaySettings::grabVisualizer
// sanitization, and multi-jumper Fly (0x0D) ledgedash cell classification.
#include <string.h>

#include "extra/grab_visualizer_core.h"
#include "host/fakes.h"
#include "host/test.h"
#include "main/game/status_ids.h"
#include "main/game/techniques.h"
#include "main/settings.h"

using namespace tm;

TEST(grab_visualizer, who_filtering_and_clamping)
{
    CHECK(!extra::grabVizWhoMatches(extra::kGrabVizWhoNone, false));
    CHECK(!extra::grabVizWhoMatches(extra::kGrabVizWhoNone, true));
    CHECK(extra::grabVizWhoMatches(extra::kGrabVizWhoHuman, false));
    CHECK(!extra::grabVizWhoMatches(extra::kGrabVizWhoHuman, true));
    CHECK(!extra::grabVizWhoMatches(extra::kGrabVizWhoCpu, false));
    CHECK(extra::grabVizWhoMatches(extra::kGrabVizWhoCpu, true));
    CHECK(extra::grabVizWhoMatches(extra::kGrabVizWhoBoth, false));
    CHECK(extra::grabVizWhoMatches(extra::kGrabVizWhoBoth, true));

    CHECK_EQ(0u, static_cast<unsigned>(extra::grabVizClampWho(0)));
    CHECK_EQ(3u, static_cast<unsigned>(extra::grabVizClampWho(3)));
    CHECK_EQ(3u, static_cast<unsigned>(extra::grabVizClampWho(4)));
    CHECK_EQ(3u, static_cast<unsigned>(extra::grabVizClampWho(255)));
}

TEST(grab_visualizer, distinct_colors_and_geometry_validation)
{
    u32 cGrab = extra::grabVizColor(extra::kGrabVizGrab);
    u32 cSearch = extra::grabVizColor(extra::kGrabVizSearch);
    u32 cItem = extra::grabVizColor(extra::kGrabVizItem);
    CHECK(cGrab != cSearch);
    CHECK(cGrab != cItem);
    CHECK(cSearch != cItem);
    CHECK_EQ(extra::kGrabVizGrabColor, extra::grabVizColor(99));

    // Half-intensity shading preserves alpha while halving RGB channels.
    CHECK_EQ(0x6C2377B8u, extra::grabVizShadeColor(0xD946EFB8u));
    CHECK_EQ(0x035B6AB8u, extra::grabVizShadeColor(0x06B6D4B8u));
    CHECK_EQ(0x1C5E7CB8u, extra::grabVizShadeColor(extra::kGrabVizItemBubbleColor));

    // Radius validation rejects non-positive and absurd radii.
    CHECK(!extra::grabVizValidRadius(0.0f));
    CHECK(!extra::grabVizValidRadius(-1.0f));
    CHECK(!extra::grabVizValidRadius(500.0f));
    CHECK(extra::grabVizValidRadius(0.25f));
    CHECK(extra::grabVizValidRadius(12.5f));

    // Quad validation accepts real world boxes and rejects degenerate or huge ones.
    const float goodX[4] = {-4.0f, 8.0f, 8.0f, -4.0f};
    const float goodY[4] = {0.0f, 0.0f, 14.0f, 14.0f};
    CHECK(extra::grabVizValidRect(goodX, goodY));

    const float zeroX[4] = {2.0f, 2.0f, 2.0f, 2.0f};
    const float zeroY[4] = {3.0f, 3.0f, 3.0f, 3.0f};
    CHECK(!extra::grabVizValidRect(zeroX, zeroY));

    const float hugeX[4] = {-1000.0f, 1000.0f, 1000.0f, -1000.0f};
    const float hugeY[4] = {0.0f, 0.0f, 10.0f, 10.0f};
    CHECK(!extra::grabVizValidRect(hugeX, hugeY));

    float mtx[3][4];
    extra::grabVizIdentityMatrix34(mtx);
    CHECK_NEAR(1.0, mtx[0][0], 0.0001);
    CHECK_NEAR(1.0, mtx[1][1], 0.0001);
    CHECK_NEAR(1.0, mtx[2][2], 0.0001);
    CHECK_NEAR(0.0, mtx[0][3], 0.0001);
}

TEST(grab_visualizer, display_settings_defaults_and_sanitization)
{
    u8* block = host::fakes::block();
    memset(block, 0, iface::kSettingsBlockSize);
    main::loadSettings(block);

    CHECK_EQ(0u, static_cast<unsigned>(main::settings().display.grabVisualizer));
    CHECK_EQ(16u, sizeof(main::DisplaySettings));
    CHECK_EQ(464u, sizeof(main::Settings));

    main::settings().display.grabVisualizer = 99;
    main::sanitizeSettings(main::settings());
    CHECK_EQ(3u, static_cast<unsigned>(main::settings().display.grabVisualizer));
}

TEST(grab_visualizer, multijump_fly_status_maps_to_ledgedash_jump_cell)
{
    CHECK_EQ(0x0D, static_cast<int>(game::kStatusFly));
    CHECK_EQ(static_cast<unsigned>(game::kCellJump),
             static_cast<unsigned>(game::ledgeCellOf(game::kStatusFly)));
    CHECK_EQ(static_cast<unsigned>(game::kCellJump),
             static_cast<unsigned>(game::ledgeCellOf(game::kStatusJumpAerial)));
    CHECK_EQ(static_cast<unsigned>(game::kCellJump),
             static_cast<unsigned>(game::ledgeCellOf(game::kStatusJump)));
    CHECK_EQ(static_cast<unsigned>(game::kCellJump),
             static_cast<unsigned>(game::ledgeCellOf(0x79)));
    CHECK_EQ(static_cast<unsigned>(game::kCellJump),
             static_cast<unsigned>(game::ledgeCellOf(0x7A)));
    CHECK_EQ(static_cast<unsigned>(game::kCellLedge),
             static_cast<unsigned>(game::ledgeCellOf(0x75)));
    CHECK_EQ(static_cast<unsigned>(game::kCellFall),
             static_cast<unsigned>(game::ledgeCellOf(game::kStatusFall)));
    CHECK_EQ(static_cast<unsigned>(game::kCellAirdodge),
             static_cast<unsigned>(game::ledgeCellOf(game::kStatusEscapeAir)));
    CHECK_EQ(static_cast<unsigned>(game::kCellLanding),
             static_cast<unsigned>(game::ledgeCellOf(game::kStatusLandingFallSpecial)));
    CHECK_EQ(static_cast<unsigned>(game::kCellAttack),
             static_cast<unsigned>(game::ledgeCellOf(game::kStatusCatch)));
    CHECK_EQ(static_cast<unsigned>(game::kCellOther),
             static_cast<unsigned>(game::ledgeCellOf(game::kStatusWait)));
}
