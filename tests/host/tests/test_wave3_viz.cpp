// Host unit tests for Stage 4B Visual Tool Backends:
//  - Multi-controller Input Viewer layouts, button/stick/trigger/AI mapping (extra/input_viewer_core.h)
//  - P+-native Hitbox Color Modes: all 6 Property classifications + Grab + Priority palette (extra/hitbox_colors_core.h)
//  - Live DI Vector Picker: all 9 modes + unit-circle normalization (extra/di_picker_core.h)
//  - HUD layout 0-overlap & 5% CRT safe-area containment across 16:9 and 4:3 (main/ui/hud_layout.h)
//  - SD storage backward-compatible migration from pre-Wave-3 kAreaDisplay (used == 12) + round-trip persistence
#include <math.h>
#include <string.h>

#include "extra/di_picker_core.h"
#include "extra/hitbox_colors_core.h"
#include "extra/input_viewer_core.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/hud_layout.h"
#include "main/ui/osd_place.h"
#include "main/ui/watermark.h"

using namespace tm;
using namespace tm::extra;
using tm::ui::layout::Box;

namespace {
    const float kTol = 0.001f;

    bool insideBox(const Box& a, const Box& room)
    {
        return a.x0 >= room.x0 - 0.5f && a.y0 >= room.y0 - 0.5f && a.x1 <= room.x1 + 0.5f && a.y1 <= room.y1 + 0.5f;
    }

    bool overlapsBox(const Box& a, const Box& b)
    {
        float dx = (a.x1 < b.x1 ? a.x1 : b.x1) - (a.x0 > b.x0 ? a.x0 : b.x0);
        float dy = (a.y1 < b.y1 ? a.y1 : b.y1) - (a.y0 > b.y0 ? a.y0 : b.y0);
        return dx > 0.5f && dy > 0.5f;
    }

    Box osdStackBox(int position, int player, int count, bool wide, float floorY = 0.0f)
    {
        float xs = wide ? 0.75f : 1.0f;
        osd::StackPlace at = osd::stackPlace(position, player, xs, floorY);
        float w = osd::kBoxWidth * xs;
        Box all = ui::layout::none();
        for (int slot = 0; slot < count; slot++)
        {
            float cx = at.x + slot * at.dx;
            float cy = at.y + slot * at.dy;
            Box one = {cx - w * 0.5f, cy - osd::kBoxHeight * 0.5f, cx + w * 0.5f, cy + osd::kBoxHeight * 0.5f};
            all = ui::layout::unite(all, one);
        }
        return all;
    }

    bool layoutHasIcon(const ViewerLayout& layout, u8 iconId)
    {
        const ViewerButtonSpec* btns = viewerLayoutButtons(layout);
        for (u8 i = 0; i < layout.buttonCount; i++)
        {
            if (btns[i].iconId == iconId)
            {
                return true;
            }
        }
        return false;
    }

    const ViewerButtonSpec* findButton(const ViewerLayout& layout, u8 iconId)
    {
        const ViewerButtonSpec* btns = viewerLayoutButtons(layout);
        for (u8 i = 0; i < layout.buttonCount; i++)
        {
            if (btns[i].iconId == iconId)
            {
                return &btns[i];
            }
        }
        return NULL;
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

    void pumpStorage(int n = 4)
    {
        for (int i = 0; i < n; i++)
        {
            storage::onRender(host::fakes::match());
        }
    }

    void resealFile(const char* name)
    {
        u8* buf = host::card::data(name);
        storage::FileHeader* h = reinterpret_cast<storage::FileHeader*>(buf);
        h->checksum = storage::checksum(buf + storage::kChecksumFrom, h->size - storage::kChecksumFrom);
    }
}

// ---- Tool 1: Multi-controller Input Viewer --------------------------------

TEST(wave3_viz, input_viewer_all_four_controller_layouts)
{
    // 1. GameCube: 2 octagonal sticks, 2 analog triggers, A/B/X/Y/L/R/Z/Start/Dpad.
    const ViewerLayout& gcc = viewerLayoutForType(kViewerGcc);
    CHECK_EQ(kViewerGcc, gcc.kind);
    CHECK_EQ(1, gcc.sticks[0].present);
    CHECK_EQ(1, gcc.sticks[0].octagonal);
    CHECK_EQ(1, gcc.sticks[1].present);
    CHECK_EQ(1, gcc.sticks[1].isCStick);
    CHECK_EQ(1, gcc.triggers[0].present);
    CHECK_EQ(1, gcc.triggers[1].present);
    CHECK(layoutHasIcon(gcc, ui::kIconGcA));
    CHECK(layoutHasIcon(gcc, ui::kIconGcB));
    CHECK(layoutHasIcon(gcc, ui::kIconGcX));
    CHECK(layoutHasIcon(gcc, ui::kIconGcY));
    CHECK(layoutHasIcon(gcc, ui::kIconGcL));
    CHECK(layoutHasIcon(gcc, ui::kIconGcR));
    CHECK(layoutHasIcon(gcc, ui::kIconGcZ));
    CHECK(layoutHasIcon(gcc, ui::kIconGcStart));
    CHECK(layoutHasIcon(gcc, ui::kIconGcDpad));

    // 2. Classic Controller: 2 circular sticks, L/R/ZL/ZR, A/B/X/Y, Dpad, +/-.
    const ViewerLayout& cc = viewerLayoutForType(kViewerClassic);
    CHECK_EQ(kViewerClassic, cc.kind);
    CHECK_EQ(1, cc.sticks[0].present);
    CHECK_EQ(0, cc.sticks[0].octagonal);
    CHECK_EQ(1, cc.sticks[1].present);
    CHECK_EQ(0, cc.sticks[1].octagonal);
    CHECK_EQ(1, cc.triggers[0].present);
    CHECK_EQ(1, cc.triggers[1].present);
    CHECK(layoutHasIcon(cc, ui::kIconClA));
    CHECK(layoutHasIcon(cc, ui::kIconClB));
    CHECK(layoutHasIcon(cc, ui::kIconClX));
    CHECK(layoutHasIcon(cc, ui::kIconClY));
    CHECK(layoutHasIcon(cc, ui::kIconClL));
    CHECK(layoutHasIcon(cc, ui::kIconClR));
    CHECK(layoutHasIcon(cc, ui::kIconClZL));
    CHECK(layoutHasIcon(cc, ui::kIconClZR));
    CHECK(layoutHasIcon(cc, ui::kIconWiiDpad));
    CHECK(layoutHasIcon(cc, ui::kIconWiiPlus));
    CHECK(layoutHasIcon(cc, ui::kIconWiiMinus));

    // 3. Nunchuk: 1 octagonal stick, C/Z, Wii A/B/1/2/+/-/Dpad.
    const ViewerLayout& nun = viewerLayoutForType(kViewerNunchuk);
    CHECK_EQ(kViewerNunchuk, nun.kind);
    CHECK_EQ(1, nun.sticks[0].present);
    CHECK_EQ(1, nun.sticks[0].octagonal);
    CHECK_EQ(0, nun.sticks[1].present);
    CHECK_EQ(0, nun.triggers[0].present);
    CHECK(layoutHasIcon(nun, ui::kIconNunC));
    CHECK(layoutHasIcon(nun, ui::kIconNunZ));
    CHECK(layoutHasIcon(nun, ui::kIconWiiA));
    CHECK(layoutHasIcon(nun, ui::kIconWiiB));
    CHECK(layoutHasIcon(nun, ui::kIconWii1));
    CHECK(layoutHasIcon(nun, ui::kIconWii2));
    CHECK(layoutHasIcon(nun, ui::kIconWiiPlus));
    CHECK(layoutHasIcon(nun, ui::kIconWiiMinus));
    CHECK(layoutHasIcon(nun, ui::kIconWiiDpad));

    // 4. Sideways Wii Remote: 0 sticks, 0 triggers, Wii Dpad/1/2/A/B/+/-.
    const ViewerLayout& wii = viewerLayoutForType(kViewerWiimote);
    CHECK_EQ(kViewerWiimote, wii.kind);
    CHECK_EQ(0, wii.sticks[0].present);
    CHECK_EQ(0, wii.sticks[1].present);
    CHECK_EQ(0, wii.triggers[0].present);
    CHECK(layoutHasIcon(wii, ui::kIconWiiDpad));
    CHECK(layoutHasIcon(wii, ui::kIconWii1));
    CHECK(layoutHasIcon(wii, ui::kIconWii2));
    CHECK(layoutHasIcon(wii, ui::kIconWiiA));
    CHECK(layoutHasIcon(wii, ui::kIconWiiB));
    CHECK(layoutHasIcon(wii, ui::kIconWiiPlus));
    CHECK(layoutHasIcon(wii, ui::kIconWiiMinus));
}

TEST(wave3_viz, input_viewer_pressed_idle_sticks_triggers_and_ai_pad)
{
    const ViewerLayout& gcc = viewerLayoutForType(kViewerGcc);
    const ViewerButtonSpec* btnA = findButton(gcc, ui::kIconGcA);
    const ViewerButtonSpec* btnZ = findButton(gcc, ui::kIconGcZ);
    REQUIRE(btnA != NULL && btnZ != NULL);

    CHECK(!viewerButtonPressed(0, *btnA));
    CHECK(viewerButtonPressed(game::kPadA, *btnA));
    CHECK(!viewerButtonPressed(game::kPadA, *btnZ));
    CHECK(viewerButtonPressed(game::kPadZ, *btnZ));
    CHECK_EQ(static_cast<u32>(kViewerColorIdle), viewerIconTint(false));
    CHECK_EQ(static_cast<u32>(kViewerColorPressed), viewerIconTint(true));

    // Classic ZL vs ZR distinction.
    const ViewerLayout& cc = viewerLayoutForType(kViewerClassic);
    const ViewerButtonSpec* zl = findButton(cc, ui::kIconClZL);
    const ViewerButtonSpec* zr = findButton(cc, ui::kIconClZR);
    REQUIRE(zl != NULL && zr != NULL);
    CHECK(viewerButtonPressed(game::kPadClassicZL, *zl));
    CHECK(!viewerButtonPressed(game::kPadClassicZL, *zr));
    CHECK(viewerButtonPressed(game::kPadClassicZR, *zr));

    // Trigger fill: analog 0..150 and digital override.
    CHECK_NEAR(0.0f, viewerTriggerFill(0, false), kTol);
    CHECK_NEAR(0.5f, viewerTriggerFill(75, false), kTol);
    CHECK_NEAR(1.0f, viewerTriggerFill(150, false), kTol);
    CHECK_NEAR(1.0f, viewerTriggerFill(200, false), kTol);
    CHECK_NEAR(1.0f, viewerTriggerFill(0, true), kTol);

    // Stick normalization clamps corners inside unit circle.
    float sx = 0.0f, sy = 0.0f;
    viewerStickNorm(80, 80, sx, sy);
    CHECK_NEAR(1.0f, sx * sx + sy * sy, 0.01f);
    CHECK_NEAR(0.7071f, sx, 0.01f);
    CHECK_NEAR(0.7071f, sy, 0.01f);
    viewerStickNorm(-40, 0, sx, sy);
    CHECK_NEAR(-0.5f, sx, 0.01f);
    CHECK_NEAR(0.0f, sy, 0.01f);

    // D-pad direction extraction.
    int dx = 0, dy = 0;
    viewerDpadDir(game::kPadDUp | game::kPadDLeft, dx, dy);
    CHECK_EQ(-1, dx);
    CHECK_EQ(1, dy);

    // CPU aiInput -> ViewerPad synthesis (grab = Attack|Guard -> Z; jump -> X).
    display::ViewerPad pad;
    viewerAiInputToPad(0x1u | 0x8u, 0.5f, -1.0f, 0.0f, 0.0f, pad);
    CHECK((pad.buttons & game::kPadZ) != 0);
    CHECK((pad.buttons & game::kPadA) == 0);
    CHECK_EQ(40, pad.sx);
    CHECK_EQ(-80, pad.sy);

    viewerAiInputToPad(0x2u | 0x4u | 0x8u, -1.0f, 0.0f, 0.5f, 0.0f, pad);
    CHECK((pad.buttons & game::kPadB) != 0);
    CHECK((pad.buttons & game::kPadX) != 0);
    CHECK((pad.buttons & game::kPadL) != 0);
    CHECK_EQ(75, pad.lTrig);

    char tag[8];
    viewerTagText(0, false, tag);
    CHECK_EQ(0, strcmp(tag, "P1"));
    viewerTagText(1, true, tag);
    CHECK_EQ(0, strcmp(tag, "CPU"));
}

// ---- Tool 2: P+-Native Hitbox Color Modes ---------------------------------

TEST(wave3_viz, hitbox_color_property_classifier_and_pplus_meteor_window)
{
    // Flags word (+0x38): setOffKind = (w >> 30) & 3; shieldable = (w >> 21) & 1.
    const u32 kClangShieldable = (3u << 30) | (1u << 21);
    const u32 kTranscendentShieldable = (0u << 30) | (1u << 21);
    const u32 kUnblockable = (3u << 30) | (0u << 21);

    // 1. Unblockable takes highest priority.
    HitboxPartProps unblock = decodeAttackPartFlags(12, 270, kUnblockable);
    CHECK_EQ(0, unblock.shieldable);
    CHECK_EQ(kHitPropUnblockable, classifyHitboxProperty(unblock, 8, 12));
    CHECK_EQ(static_cast<u32>(kHitboxColorUnblockable), hitboxPropertyColor(kHitPropUnblockable));

    // 2. P+ 3.2.1 Meteor window: inclusive 260..280; spikes 259 & 281 are NOT meteors!
    CHECK(isPPlusMeteorAngle(260));
    CHECK(isPPlusMeteorAngle(270));
    CHECK(isPPlusMeteorAngle(280));
    CHECK(!isPPlusMeteorAngle(259));
    CHECK(!isPPlusMeteorAngle(281));
    CHECK(!isPPlusMeteorAngle(361));

    HitboxPartProps meteor260 = decodeAttackPartFlags(14, 260, kClangShieldable);
    HitboxPartProps meteor280 = decodeAttackPartFlags(14, 280, kClangShieldable);
    HitboxPartProps spike259 = decodeAttackPartFlags(14, 259, kClangShieldable);
    HitboxPartProps spike281 = decodeAttackPartFlags(14, 281, kClangShieldable);
    CHECK_EQ(kHitPropMeteor, classifyHitboxProperty(meteor260, 14, 14));
    CHECK_EQ(kHitPropMeteor, classifyHitboxProperty(meteor280, 14, 14));
    CHECK_EQ(kHitPropNormal, classifyHitboxProperty(spike259, 14, 14));
    CHECK_EQ(kHitPropNormal, classifyHitboxProperty(spike281, 14, 14));
    CHECK_EQ(static_cast<u32>(kHitboxColorMeteor), hitboxPropertyColor(kHitPropMeteor));

    // 3. Transcendent (setOffKind == 0).
    HitboxPartProps transc = decodeAttackPartFlags(10, 45, kTranscendentShieldable);
    CHECK_EQ(0, transc.setOffKind);
    CHECK_EQ(kHitPropTranscendent, classifyHitboxProperty(transc, 10, 10));
    CHECK_EQ(static_cast<u32>(kHitboxColorTranscendent), hitboxPropertyColor(kHitPropTranscendent));

    // 4. Sweetspot vs Sourspot on multi-part attacks (minPower < maxPower).
    HitboxPartProps sweet = decodeAttackPartFlags(18, 32, kClangShieldable);
    HitboxPartProps sour = decodeAttackPartFlags(6, 361, kClangShieldable);
    CHECK_EQ(kHitPropSweetspot, classifyHitboxProperty(sweet, 6, 18));
    CHECK_EQ(kHitPropSourspot, classifyHitboxProperty(sour, 6, 18));
    CHECK_EQ(static_cast<u32>(kHitboxColorSweetspot), hitboxPropertyColor(kHitPropSweetspot));
    CHECK_EQ(static_cast<u32>(kHitboxColorSourspot), hitboxPropertyColor(kHitPropSourspot));

    // 5. Normal (single-power standard attack).
    HitboxPartProps normal = decodeAttackPartFlags(10, 45, kClangShieldable);
    CHECK_EQ(kHitPropNormal, classifyHitboxProperty(normal, 10, 10));
    CHECK_EQ(static_cast<u32>(kHitboxColorNormal), hitboxPropertyColor(kHitPropNormal));

    // 6. Grab emerald green color.
    CHECK_EQ(static_cast<u32>(kHitboxColorGrab), hitboxPropertyColor(kHitPropGrab));
}

TEST(wave3_viz, hitbox_color_priority_palette_distinct_ids)
{
    u32 cols[8];
    for (int id = 0; id < 8; id++)
    {
        cols[id] = hitboxPriorityColor(id);
        for (int prev = 0; prev < id; prev++)
        {
            CHECK(cols[id] != cols[prev]);
        }
    }
    // Wraps cleanly for id >= 8.
    CHECK_EQ(cols[0], hitboxPriorityColor(8));
    CHECK_EQ(cols[1], hitboxPriorityColor(9));

    HitboxPartProps dummy = decodeAttackPartFlags(10, 270, (3u << 30) | (1u << 21));
    CHECK_EQ(cols[0], hitboxColorForMode(main::kHitboxColorPriority, 0, dummy, 10, 10));
    CHECK_EQ(cols[2], hitboxColorForMode(main::kHitboxColorPriority, 2, dummy, 10, 10));
    CHECK_EQ(static_cast<u32>(kHitboxColorMeteor), hitboxColorForMode(main::kHitboxColorProperty, 2, dummy, 10, 10));
}

// ---- Tool 3: Live DI Vector Picker ----------------------------------------

TEST(wave3_viz, di_picker_all_nine_modes_and_unit_circle)
{
    // Rightward 45-degree launch: kx = +2.0, ky = +2.0.
    // 0: Live Stick (passes live stick clamped to unit circle).
    DiVectorResult live = resolveDiVector(main::kDiVectorLive, 2.0f, 2.0f, 10.0f, 0.0f, 1.0f, 0.8f, 0.8f);
    CHECK(!live.overridden);
    CHECK_NEAR(1.0f, live.dx * live.dx + live.dy * live.dy, 0.01f);

    // 1: No DI.
    DiVectorResult none = resolveDiVector(main::kDiVectorNone, 2.0f, 2.0f, 10.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    CHECK(none.overridden);
    CHECK_NEAR(0.0f, none.dx, kTol);
    CHECK_NEAR(0.0f, none.dy, kTol);

    // 2: In (-1, 0) for rightward launch, (+1, 0) for leftward launch.
    DiVectorResult inR = resolveDiVector(main::kDiVectorIn, 2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    CHECK_NEAR(-1.0f, inR.dx, kTol);
    CHECK_NEAR(0.0f, inR.dy, kTol);
    DiVectorResult inL = resolveDiVector(main::kDiVectorIn, -2.0f, 2.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
    CHECK_NEAR(1.0f, inL.dx, kTol);
    CHECK_NEAR(0.0f, inL.dy, kTol);

    // 3: Out (+1, 0) for rightward launch.
    DiVectorResult outR = resolveDiVector(main::kDiVectorOut, 2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    CHECK_NEAR(1.0f, outR.dx, kTol);
    CHECK_NEAR(0.0f, outR.dy, kTol);

    // 4..7: Diagonals (unit circle length == 1.0).
    DiVectorResult ui = resolveDiVector(main::kDiVectorUpIn, 2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    DiVectorResult uo = resolveDiVector(main::kDiVectorUpOut, 2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    DiVectorResult di = resolveDiVector(main::kDiVectorDownIn, 2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    DiVectorResult dout = resolveDiVector(main::kDiVectorDownOut, 2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    CHECK_NEAR(-0.7071f, ui.dx, 0.002f);
    CHECK_NEAR(0.7071f, ui.dy, 0.002f);
    CHECK_NEAR(0.7071f, uo.dx, 0.002f);
    CHECK_NEAR(0.7071f, uo.dy, 0.002f);
    CHECK_NEAR(-0.7071f, di.dx, 0.002f);
    CHECK_NEAR(-0.7071f, di.dy, 0.002f);
    CHECK_NEAR(0.7071f, dout.dx, 0.002f);
    CHECK_NEAR(-0.7071f, dout.dy, 0.002f);

    // 8: Survival DI is perpendicular to launch and rotates upward/inward.
    DiVectorResult survR = resolveDiVector(main::kDiVectorSurvival, 3.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    CHECK_NEAR(0.0f, 3.0f * survR.dx + 1.0f * survR.dy, 0.005f); // perpendicular
    CHECK(survR.dx < 0.0f && survR.dy > 0.0f);                    // Up-In for rightward launch
    CHECK_NEAR(1.0f, survR.dx * survR.dx + survR.dy * survR.dy, 0.005f);

    DiVectorResult survL = resolveDiVector(main::kDiVectorSurvival, -3.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
    CHECK_NEAR(0.0f, -3.0f * survL.dx + 1.0f * survL.dy, 0.005f);
    CHECK(survL.dx > 0.0f && survL.dy > 0.0f); // Up-In for leftward launch

    // Pure vertical launch from right side of stage: horizontal inward.
    DiVectorResult survUp = resolveDiVector(main::kDiVectorSurvival, 0.0f, 4.0f, 30.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    CHECK_NEAR(-1.0f, survUp.dx, kTol);
    CHECK_NEAR(0.0f, survUp.dy, kTol);

    // Compact readout labels for all 9 modes.
    for (u8 m = 0; m < main::kDiVectorModeCount; m++)
    {
        char buf[16];
        diVectorShortLabel(m, buf);
        CHECK(strncmp(buf, "DI: ", 4) == 0);
        CHECK(strlen(buf) >= 6 && strlen(buf) <= 10);
    }
}

// ---- Layout: 0-Overlap & 5% CRT Safe Area in 16:9 AND 4:3 -----------------

TEST(wave3_viz, hud_layout_zero_overlap_and_safe_area_16_9_and_4_3)
{
    using namespace ui::layout;
    for (int a = 0; a < 2; a++)
    {
        bool wide = (a == 1);
        Box safe = safeArea(wide);
        Box timer = timerRoom(wide);
        Box watermark = {rightX(kWatermarkRight, wide) - 85.0f * xsOf(wide), kWatermarkCapTop,
                         rightX(kWatermarkRight, wide), kWatermarkCapTop + 11.0f};
        Box recBar = {180.0f, kRecBarCentreY - kRecBarHeight * 0.5f, 460.0f, kRecBarCentreY + kRecBarHeight * 0.5f};
        Box recMsg = {120.0f, kRecMessageY, 520.0f, kRecMessageY + 18.0f};

        for (int ledge = 0; ledge <= 1; ledge++)
        {
            Box band = labBand(true, ledge ? 64 : 0, wide);
            float infoW = infoWidth(2, wide);
            float infoY = infoTop(band, infoW, wide);
            Box infoHmn = box(infoLeft(wide), infoY, infoW, infoHeight(8, 2));
            Box infoCpu = box(infoLeft(wide), infoCpuTop(infoY, infoHmn.y1), infoW, infoHeight(8, 2));

            // Check Input Viewer P1 + CPU cards and DI Compass together.
            Box v0 = inputViewerCardBox(0, band, wide);
            Box v1 = inputViewerCardBox(1, band, wide);
            Box comp0 = diCompassBox(band, 0, wide);
            Box comp2 = diCompassBox(band, 2, wide);

            // Strictly inside 5% CRT safe area (and x <= 608, y >= 24).
            CHECK(insideBox(v0, safe));
            CHECK(insideBox(v1, safe));
            CHECK(insideBox(comp0, safe));
            CHECK(insideBox(comp2, safe));
            CHECK(v0.x1 <= 608.0f + 0.01f && v0.y0 >= 24.0f - 0.01f);
            CHECK(comp2.x1 <= 608.0f + 0.01f && comp2.y0 >= 24.0f - 0.01f);

            // Non-overlap between viewer cards and compass.
            CHECK(!overlapsBox(v0, v1));
            CHECK(!overlapsBox(v0, comp2));
            CHECK(!overlapsBox(v1, comp2));

            // Non-overlap against Timer, Ledgedash band, Info Display, Recording HUD, Watermark.
            Box viz[3] = {v0, v1, comp2};
            for (int i = 0; i < 3; i++)
            {
                CHECK(!overlapsBox(viz[i], timer));
                if (ledge)
                {
                    CHECK(!overlapsBox(viz[i], band));
                }
                CHECK(!overlapsBox(viz[i], infoHmn));
                CHECK(!overlapsBox(viz[i], infoCpu));
                CHECK(!overlapsBox(viz[i], recBar));
                CHECK(!overlapsBox(viz[i], recMsg));
                CHECK(!overlapsBox(viz[i], watermark));

                // Non-overlap against typical 2P OSD stacks (HUD, Sides, TopLeft).
                for (int p = 0; p < 2; p++)
                {
                    CHECK(!overlapsBox(viz[i], osdStackBox(osd::kPositionHud, p, 3, wide)));
                    CHECK(!overlapsBox(viz[i], osdStackBox(osd::kPositionSides, p, 3, wide)));
                    CHECK(!overlapsBox(viz[i], osdStackBox(osd::kPositionTopLeft, p, 3, wide)));
                }
            }
        }
    }
}

// ---- SD Backward-Compatible Migration & Persistence -----------------------

TEST(wave3_viz, sd_storage_migration_from_pre_wave3_display_area_and_round_trip)
{
    host::card::reset();
    bootStorage();

    // Save non-default Wave 3 values and verify round-trip persistence across reboot.
    main::Settings& s = main::settings();
    s.display.hitboxes = 3;
    s.display.grabVisualizer = 1;
    s.display.inputViewer = 3;
    s.display.hitboxColorMode = main::kHitboxColorProperty;
    s.display.diVectorMode = main::kDiVectorSurvival;
    storage::settingsMenuClosed();
    pumpStorage();

    bootStorage();
    CHECK_EQ(3, main::settings().display.hitboxes);
    CHECK_EQ(1, main::settings().display.grabVisualizer);
    CHECK_EQ(3, main::settings().display.inputViewer);
    CHECK_EQ(static_cast<int>(main::kHitboxColorProperty), main::settings().display.hitboxColorMode);
    CHECK_EQ(static_cast<int>(main::kDiVectorSurvival), main::settings().display.diVectorMode);

    // Simulate a pre-Wave-3 SD file where kAreaDisplay had used == 12 (before the 3 new bytes).
    const char* name = host::card::has("settings-b.bin") ? "settings-b.bin" : "settings-a.bin";
    REQUIRE(host::card::has(name));
    u8* file = host::card::data(name);
    storage::SettingsHead* head = reinterpret_cast<storage::SettingsHead*>(file + sizeof(storage::FileHeader));
    storage::AreaEntry* table =
        reinterpret_cast<storage::AreaEntry*>(file + sizeof(storage::FileHeader) + sizeof(storage::SettingsHead));
    storage::AreaEntry* dispEntry = NULL;
    for (int i = 0; i < head->areaCount; i++)
    {
        if (table[i].id == storage::kAreaDisplay)
        {
            dispEntry = &table[i];
            break;
        }
    }
    REQUIRE(dispEntry != NULL);
    CHECK_EQ(15, dispEntry->used);
    dispEntry->used = 12;
    resealFile(name);

    // Rebooting with the pre-Wave-3 kAreaDisplay (used == 12) must preserve the 12 old
    // bytes and default the 3 new fields to (0, 0, 0).
    bootStorage();
    CHECK_EQ(3, main::settings().display.hitboxes);
    CHECK_EQ(1, main::settings().display.grabVisualizer);
    CHECK_EQ(0, main::settings().display.inputViewer);
    CHECK_EQ(0, main::settings().display.hitboxColorMode);
    CHECK_EQ(0, main::settings().display.diVectorMode);

    storage::detach();
}
