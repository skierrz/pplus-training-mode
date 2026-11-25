// OSDs > OSD Position: where each player's message stack sits in native
// 640x480 screen space (src/main/ui/osd_place.h). Not covered: the drawing
// itself (make test-osds).
#include "host/test.h"
#include "main/ui/hud_layout.h"
#include "main/ui/osd_place.h"

using namespace tm;

TEST(osd_place, hud_and_sides_align_and_step_upward)
{
    const float xs[2] = {1.0f, 0.75f};
    for (int k = 0; k < 2; k++)
    {
        float half = osd::kBoxWidth * xs[k] * 0.5f;
        float minX = xs[k] >= 1.0f ? ui::layout::kSafeMarginX + half : half;
        float maxX = xs[k] >= 1.0f ? 640.0f - ui::layout::kSafeMarginX - half : 640.0f - half;
        for (int p = 0; p < 4; p++)
        {
            osd::StackPlace h = osd::stackPlace(osd::kPositionHud, p, xs[k]);
            float right = xs[k] < 1.0f ? osd::kHudRight[p] : osd::kHudRight43[p];
            float hx = right - half;
            if (hx < minX) hx = minX;
            if (hx > maxX) hx = maxX;
            CHECK_NEAR(hx, h.x, 0.001f);
            CHECK_NEAR(osd::kDigitsTop - osd::kHudGap - osd::kBoxHeight * 0.5f, h.y, 0.001f);
            CHECK_EQ(0.0f, h.dx);
            CHECK_EQ(-osd::kPitch, h.dy);

            // 4-player Sides keeps the four evenly spaced columns.
            osd::StackPlace s4 = osd::stackPlace(osd::kPositionSides, p, xs[k], 0.0f, 0.0f, 4);
            float sx = osd::kSidesX[p];
            if (sx < minX) sx = minX;
            if (sx > maxX) sx = maxX;
            CHECK_NEAR(sx, s4.x, 0.001f);
            CHECK_EQ(osd::kSidesY, s4.y);
            CHECK_EQ(0.0f, s4.dx);
            CHECK_EQ(-osd::kPitch, s4.dy);
        }
        // 1v1 Sides pushes P1 to the outer left edge and P2 to the outer right edge,
        // making Sides visibly distinct from HUD in both 4:3 and 16:9.
        float marginX = xs[k] >= 1.0f ? ui::layout::kSafeMarginX : 12.0f;
        osd::StackPlace s1 = osd::stackPlace(osd::kPositionSides, 0, xs[k], 0.0f, 0.0f, 2);
        osd::StackPlace s2 = osd::stackPlace(osd::kPositionSides, 1, xs[k], 0.0f, 0.0f, 2);
        osd::StackPlace h2 = osd::stackPlace(osd::kPositionHud, 1, xs[k], 0.0f, 0.0f, 2);
        CHECK_NEAR(marginX + half, s1.x, 0.001f);
        CHECK_NEAR(640.0f - marginX - half, s2.x, 0.001f);
        CHECK(s2.x - h2.x >= 250.0f);
    }
}

TEST(osd_place, top_left_rows_grow_right_inside_safe_area)
{
    const float rowPitch = osd::kBoxHeight + osd::kTopRowGap;
    for (int p = 0; p < 4; p++)
    {
        osd::StackPlace t = osd::stackPlace(osd::kPositionTopLeft, p, 1.0f);
        CHECK_NEAR(ui::layout::kSafeMarginX + osd::kBoxWidth * 0.5f, t.x, 0.001f);
        CHECK_NEAR(ui::layout::kSafeMarginY + osd::kBoxHeight * 0.5f + p * rowPitch, t.y, 0.001f);
        CHECK_NEAR(osd::kBoxWidth + osd::kTopColGap, t.dx, 0.001f);
        CHECK_EQ(0.0f, t.dy);
    }
    osd::StackPlace w = osd::stackPlace(osd::kPositionTopLeft, 0, 0.75f);
    CHECK_NEAR(12.0f + osd::kBoxWidth * 0.75f * 0.5f, w.x, 0.001f);
    CHECK_NEAR((osd::kBoxWidth + osd::kTopColGap) * 0.75f, w.dx, 0.001f);
}

TEST(osd_place, top_right_mirrors_top_left)
{
    const float xs[2] = {1.0f, 0.75f};
    for (int k = 0; k < 2; k++)
    {
        for (int p = 0; p < 4; p++)
        {
            osd::StackPlace l = osd::stackPlace(osd::kPositionTopLeft, p, xs[k]);
            osd::StackPlace r = osd::stackPlace(osd::kPositionTopRight, p, xs[k]);
            CHECK_NEAR(640.0f - l.x, r.x, 0.001f);
            CHECK_EQ(l.y, r.y);
            CHECK_EQ(-l.dx, r.dx);
            CHECK_EQ(0.0f, r.dy);
        }
    }
}

TEST(osd_place, top_rows_are_on_screen_and_apart)
{
    const float xs[2] = {1.0f, 0.75f};
    const float halfH = osd::kBoxHeight * 0.5f;
    for (int k = 0; k < 2; k++)
    {
        float halfW = osd::kBoxWidth * xs[k] * 0.5f;
        for (int pos = osd::kPositionTopLeft; pos <= osd::kPositionTopRight; pos++)
        {
            for (int p = 0; p < 4; p++)
            {
                osd::StackPlace t = osd::stackPlace(pos, p, xs[k]);
                CHECK(t.x - halfW >= 0.0f && t.x + halfW <= 640.0f);
                CHECK(t.y - halfH >= 0.0f && t.y + halfH <= 480.0f);
                CHECK(t.dx * t.dx >= (2.0f * halfW) * (2.0f * halfW));
                if (p > 0)
                {
                    CHECK(t.y - osd::stackPlace(pos, p - 1, xs[k]).y >= osd::kBoxHeight);
                }
            }
        }
    }
}

TEST(osd_place, an_unknown_position_is_hud)
{
    osd::StackPlace h = osd::stackPlace(osd::kPositionHud, 1, 1.0f);
    osd::StackPlace u = osd::stackPlace(osd::kPositionCount, 1, 1.0f);
    CHECK_EQ(h.x, u.x);
    CHECK_EQ(h.y, u.y);
    CHECK_EQ(h.dy, u.dy);
}

TEST(osd_place, hud_43_aligns_with_damage_readouts_on_screen_and_apart)
{
    // Brawl's info_training.pac 4:3 Info SCN0 camera (pos z 136.5, fovy 20,
    // aspect 1.26894 -> 10.477495 px/unit), InfPos_TopN frame 4 pos01..pos04
    // X = {-21.5004, -7.5043, 6.4991, 20.4957}, InfDamage0_TopN pers glyph
    // right edge dx = +5.7360.
    const float pos43[4] = {-21.5004f, -7.5043f, 6.4991f, 20.4957f};
    const float pxPerUnit = 10.477495f;
    const float persDx = 5.7360f;
    const float half = osd::kBoxWidth * 0.5f;
    // 16:9 InfoWide SCN0 (aspect 1.73232 -> 7.674860 px/unit), InfPos_TopN
    // frame 14 pos01..pos04 X = {-28.5033, -9.5048, 9.4948, 27.4933}.
    const float pos16[4] = {-28.5033f, -9.5048f, 9.4948f, 27.4933f};
    const float pxPerUnit16 = 7.674860f;
    for (int p = 0; p < 4; p++)
    {
        osd::StackPlace h = osd::stackPlace(osd::kPositionHud, p, 1.0f);
        float expectedRight = 320.0f + (pos43[p] + persDx) * pxPerUnit;
        float right = h.x + half;
        float left = h.x - half;
        if (p == 0 && expectedRight - osd::kBoxWidth < ui::layout::kSafeMarginX)
        {
            CHECK_NEAR(ui::layout::kSafeMarginX, left, 0.001f);
        }
        else
        {
            CHECK_NEAR(expectedRight, right, 0.6f);
            CHECK_NEAR(osd::kHudRight43[p], right, 0.001f);
        }
        CHECK(left >= ui::layout::kSafeMarginX && right <= 640.0f - ui::layout::kSafeMarginX);
        if (p > 0)
        {
            osd::StackPlace prev = osd::stackPlace(osd::kPositionHud, p - 1, 1.0f);
            CHECK(left >= prev.x + half);
        }
        osd::StackPlace w = osd::stackPlace(osd::kPositionHud, p, 0.75f);
        float expectedRight16 = 320.0f + (pos16[p] + persDx) * pxPerUnit16;
        CHECK_NEAR(osd::kHudRight[p], w.x + osd::kBoxWidth * 0.75f * 0.5f, 0.001f);
        CHECK_NEAR(expectedRight16, osd::kHudRight[p], 1.5f);
    }
}

TEST(osd_place, all_positions_fit_on_screen_and_stay_apart_in_43_and_169)
{
    const float xs[2] = {1.0f, 0.75f};
    const float halfH = osd::kBoxHeight * 0.5f;
    for (int k = 0; k < 2; k++)
    {
        float halfW = osd::kBoxWidth * xs[k] * 0.5f;
        for (int pos = osd::kPositionHud; pos <= osd::kPositionSides; pos++)
        {
            for (int p = 0; p < 4; p++)
            {
                osd::StackPlace s = osd::stackPlace(pos, p, xs[k], 0.0f, 0.0f, 4);
                CHECK(s.x - halfW >= 0.0f && s.x + halfW <= 640.0f);
                CHECK(s.y - halfH >= 0.0f && s.y + halfH <= 480.0f);
                if (p > 0)
                {
                    osd::StackPlace prev = osd::stackPlace(pos, p - 1, xs[k], 0.0f, 0.0f, 4);
                    CHECK(s.x - prev.x >= 2.0f * halfW);
                }
            }
        }
        for (int pos = osd::kPositionTopLeft; pos <= osd::kPositionTopRight; pos++)
        {
            for (int p = 0; p < 4; p++)
            {
                osd::StackPlace t = osd::stackPlace(pos, p, xs[k]);
                for (int slot = 0; slot < 3; slot++)
                {
                    float cx = t.x + slot * t.dx;
                    CHECK(cx - halfW >= 0.0f && cx + halfW <= 640.0f);
                    CHECK(t.y - halfH >= 0.0f && t.y + halfH <= 480.0f);
                }
            }
        }
    }
}

TEST(osd_place, floor_lifts_lower_stacks_above_rec_bar_and_leaves_top_rows)
{
    const float floorY = ui::layout::recBarOsdFloor();
    const float barTop = ui::layout::kRecBarCentreY - ui::layout::kRecBarHeight * 0.5f;
    CHECK_NEAR(barTop - ui::layout::kStepGap, floorY, 0.001f);
    const float xs[2] = {1.0f, 0.75f};
    for (int k = 0; k < 2; k++)
    {
        for (int p = 0; p < 4; p++)
        {
            for (int pos = osd::kPositionHud; pos <= osd::kPositionSides; pos++)
            {
                osd::StackPlace base = osd::stackPlace(pos, p, xs[k]);
                osd::StackPlace lifted = osd::stackPlace(pos, p, xs[k], floorY);
                CHECK_EQ(base.x, lifted.x);
                CHECK_EQ(base.dx, lifted.dx);
                CHECK_EQ(base.dy, lifted.dy);
                CHECK(lifted.y < base.y);
                CHECK_NEAR(floorY, lifted.y + osd::kBoxHeight * 0.5f, 0.001f);
            }
            for (int pos = osd::kPositionTopLeft; pos <= osd::kPositionTopRight; pos++)
            {
                osd::StackPlace base = osd::stackPlace(pos, p, xs[k]);
                osd::StackPlace withFloor = osd::stackPlace(pos, p, xs[k], floorY);
                CHECK(base.y + osd::kBoxHeight * 0.5f < floorY);
                CHECK_EQ(base.x, withFloor.x);
                CHECK_EQ(base.y, withFloor.y);
            }
        }
    }
}

TEST(osd_place, vs_stock_floor_clears_stock_icons_during_glide_in)
{
    // Offline VS stock icons sit around y = 384..397; capping floorY at 374.0f
    // leaves >= 5px clearance even on frame-0 glide-in (+4.9px downward offset).
    const float vsFloorY = 374.0f;
    const float xs[2] = {1.0f, 0.75f};
    for (int k = 0; k < 2; k++)
    {
        for (int p = 0; p < 4; p++)
        {
            for (int pos = osd::kPositionHud; pos <= osd::kPositionSides; pos++)
            {
                osd::StackPlace s = osd::stackPlace(pos, p, xs[k], vsFloorY);
                float bottomWithGlide = s.y + osd::kBoxHeight * 0.5f + 4.9f;
                CHECK(bottomWithGlide < 384.0f);
            }
        }
    }
}
