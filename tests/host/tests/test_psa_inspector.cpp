// Host unit tests for the live read-only PSA AnimCmd 11-Thread & FSM Inspector:
//  - 11-thread names, opcode header decoding, and 6-row priority selection
//  - SyncWait (tag 0) and AsyncWait (tag 7) timer math and edge clamping
//  - FSM, title, subheader, frame, wait, and opcode formatting & font fit (4:3 and 16:9)
//  - psaInspectorBox non-overlap and 5% CRT safe-area containment across 4:3 and 16:9
//  - SD storage round-trip, out-of-range clamping, and pre-PSA migration (used == 15)
#include <math.h>
#include <string.h>

#include "extra/psa_inspector_core.h"
#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/settings_file.h"
#include "main/storage/storage.h"
#include "main/ui/hud_layout.h"
#include "main/ui/osd_place.h"
#include "main/ui/style.h"

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

TEST(psa_inspector, eleven_thread_decoding_and_priority_selection)
{
    static const char* const kExpectedNames[kPsaThreadCount] = {
        "ActMain", "SubMain", "SubGfx", "SubSfx", "SubOth",
        "Alt0",    "Alt1",    "Alt2",   "Alt3",   "Loop",   "ActHid"
    };
    for (int i = 0; i < kPsaThreadCount; i++)
    {
        CHECK_EQ(0, strcmp(psaThreadName(i), kExpectedNames[i]));
    }
    CHECK_EQ(0, strcmp(psaThreadName(-1), "Thr?"));
    CHECK_EQ(0, strcmp(psaThreadName(11), "Thr?"));

    // Command header decode: SetFrameSpeedModifier = 0x04070200 (module 0x04, code 0x07, numArgs 2, opt 0).
    PsaCmdHeader fsm = psaDecodeCmdHeader(0x04070200u);
    CHECK_EQ(0x04, fsm.module);
    CHECK_EQ(0x07, fsm.code);
    CHECK_EQ(0x02, fsm.numArgs);
    CHECK_EQ(0x00, fsm.option);

    // Hitbox create = 0x06000D00 (module 0x06, code 0x00, numArgs 13).
    PsaCmdHeader hit = psaDecodeCmdHeader(0x06000D00u);
    CHECK_EQ(0x06, hit.module);
    CHECK_EQ(0x00, hit.code);
    CHECK_EQ(0x0D, hit.numArgs);

    // Priority selection with all 11 threads active picks ActMain, SubMain, SubGfx, SubSfx, SubOth, Loop.
    PsaThreadSnapshot all[kPsaThreadCount];
    memset(all, 0, sizeof(all));
    for (int i = 0; i < kPsaThreadCount; i++)
    {
        all[i].id = static_cast<u32>(i);
        all[i].active = 1;
    }
    int picks[kPsaDisplayRows];
    int n = psaSelectDisplayThreads(all, picks, kPsaDisplayRows);
    CHECK_EQ(6, n);
    CHECK_EQ(0, picks[0]);
    CHECK_EQ(1, picks[1]);
    CHECK_EQ(2, picks[2]);
    CHECK_EQ(3, picks[3]);
    CHECK_EQ(4, picks[4]);
    CHECK_EQ(9, picks[5]);

    // When only ActMain, Alt2, and ActHid are active, all 3 are returned in priority order.
    PsaThreadSnapshot sparse[kPsaThreadCount];
    memset(sparse, 0, sizeof(sparse));
    sparse[0].active = 1;
    sparse[7].active = 1;
    sparse[10].active = 1;
    n = psaSelectDisplayThreads(sparse, picks, kPsaDisplayRows);
    CHECK_EQ(3, n);
    CHECK_EQ(0, picks[0]);
    CHECK_EQ(10, picks[1]);
    CHECK_EQ(7, picks[2]);
}

TEST(psa_inspector, sync_and_async_wait_math)
{
    float w = -1.0f;

    // Tag 0 = SyncWait: remaining frames directly at stackEntry +0x10.
    CHECK_EQ(static_cast<int>(kPsaWaitSync), static_cast<int>(psaDecodeWait(0u, 7.5f, 12.0f, w)));
    CHECK_NEAR(7.5f, w, kTol);
    CHECK_EQ(static_cast<int>(kPsaWaitSync), static_cast<int>(psaDecodeWait(0u, -2.0f, 12.0f, w)));
    CHECK_NEAR(0.0f, w, kTol);

    // Tag 1 = active AsyncWait (0x8013AF0C): remaining = targetLogicalFrame - m_logicalFrame.
    CHECK_EQ(static_cast<int>(kPsaWaitAsync), static_cast<int>(psaDecodeWait(1u, 12.0f, 3.6f, w)));
    CHECK_NEAR(8.4f, w, kTol);
    CHECK_EQ(static_cast<int>(kPsaWaitAsync), static_cast<int>(psaDecodeWait(1u, 10.0f, 14.0f, w)));
    CHECK_NEAR(0.0f, w, kTol);

    // Tag 7 = AsyncWait marker (0x8013ADFC): reports remaining if positive, else none.
    CHECK_EQ(static_cast<int>(kPsaWaitAsync), static_cast<int>(psaDecodeWait(7u, 28.0f, 12.5f, w)));
    CHECK_NEAR(15.5f, w, kTol);
    CHECK_EQ(static_cast<int>(kPsaWaitNone), static_cast<int>(psaDecodeWait(7u, 10.0f, 14.0f, w)));
    CHECK_NEAR(0.0f, w, kTol);

    // Other stack entry kinds (e.g. subroutine/loop) -> no wait.
    CHECK_EQ(static_cast<int>(kPsaWaitNone), static_cast<int>(psaDecodeWait(3u, 5.0f, 1.0f, w)));
    CHECK_NEAR(0.0f, w, kTol);
}

TEST(psa_inspector, formatting_and_card_text_fit_4_3_and_16_9)
{
    char buf[48];

    psaFormatTitle(false, 0, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "PSA P1"));
    psaFormatTitle(false, 2, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "PSA P3"));
    psaFormatTitle(true, 1, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "PSA CPU"));

    psaFormatFsm(1.0f, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "FSM x1.00"));
    CHECK(!psaFsmIsModified(1.0f));
    psaFormatFsm(1.7895f, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "FSM x1.79"));
    CHECK(psaFsmIsModified(1.7895f));
    CHECK(psaFsmIsModified(0.55f));

    psaFormatSubHeader(0x11C, 0x1D9, 12.5f, -1, 0.0f, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "A:11C S:1D9 f12.5"));
    psaFormatSubHeader(0x33, 0x62, 4.0f, 0x0C, 1.25f, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "A:033 S:062 f4.0 2x1.25"));

    psaFormatFrame(24.5f, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "f24.5"));
    psaFormatWait(kPsaWaitSync, 4.5f, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "w4.5"));
    psaFormatWait(kPsaWaitAsync, 16.0f, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "a16.0"));
    psaFormatWait(kPsaWaitNone, 0.0f, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "-"));
    psaFormatOpcode(0x04070200u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "04:07"));

    // Verify worst-case strings fit inside card columns on both 4:3 and 16:9.
    for (int a = 0; a < 2; a++)
    {
        bool wide = (a == 1);
        float xs = ui::layout::xsOf(wide);
        gfx::setWidescreen(wide);

        float hdrScale = ui::style::scaleForCap(6.5f);
        float subScale = ui::style::scaleForCap(5.5f);
        float rowScale = ui::style::scaleForCap(6.0f);

        psaFormatTitle(true, 3, buf, sizeof(buf));
        CHECK(gfx::textWidth(buf, hdrScale) <= 84.0f * xs);
        psaFormatFsm(99.99f, buf, sizeof(buf));
        CHECK(gfx::textWidth(buf, hdrScale) <= 84.0f * xs);
        psaFormatSubHeader(0xFFF, 0xFFF, 999.9f, 0x0C, 99.99f, buf, sizeof(buf));
        CHECK(gfx::textWidth(buf, subScale) <= 170.0f * xs);

        for (int i = 0; i < kPsaThreadCount; i++)
        {
            CHECK(gfx::textWidth(psaThreadName(i), rowScale) <= 48.0f * xs);
        }
        psaFormatFrame(999.9f, buf, sizeof(buf));
        CHECK(gfx::textWidth(buf, rowScale) <= 40.0f * xs);
        psaFormatWait(kPsaWaitSync, 99.9f, buf, sizeof(buf));
        CHECK(gfx::textWidth(buf, rowScale) <= 40.0f * xs);
        psaFormatWait(kPsaWaitAsync, 99.9f, buf, sizeof(buf));
        CHECK(gfx::textWidth(buf, rowScale) <= 40.0f * xs);
        psaFormatOpcode(0xFFFF0000u, buf, sizeof(buf));
        CHECK(gfx::textWidth(buf, rowScale) <= 36.0f * xs);
    }
}

TEST(psa_inspector, hud_layout_non_overlap_and_safe_area_4_3_and_16_9)
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
            float infoW = infoWidth(2, wide); // Large Info Display (worst width: x=34..270 on 4:3)
            float infoY = infoTop(band, infoW, wide);
            Box infoHmn = box(infoLeft(wide), infoY, infoW, infoHeight(8, 2));
            Box infoCpu = box(infoLeft(wide), infoCpuTop(infoY, infoHmn.y1), infoW, infoHeight(8, 2));

            for (int slots = 0; slots <= 2; slots++)
            {
                for (int comp = 0; comp <= 1; comp++)
                {
                    float infoBot = infoCpu.y1;
                    Box psa = psaInspectorBox(band, slots, comp != 0, wide, infoBot);
                    CHECK(insideBox(psa, safe));
                    CHECK(psa.x1 <= 608.0f + 0.01f && psa.y0 >= 24.0f - 0.01f);
                    CHECK(psa.x1 - psa.x0 <= 218.0f * xsOf(wide) + 0.01f);

                    // Clears Large Info Display, Timer, Ledgedash band, Recording HUD, Watermark.
                    CHECK(!overlapsBox(psa, infoHmn));
                    CHECK(!overlapsBox(psa, infoCpu));
                    CHECK(!overlapsBox(psa, timer));
                    if (ledge)
                    {
                        CHECK(!overlapsBox(psa, band));
                    }
                    CHECK(!overlapsBox(psa, recBar));
                    CHECK(!overlapsBox(psa, recMsg));
                    CHECK(!overlapsBox(psa, watermark));

                    // Clears active Input Viewer cards and DI Compass.
                    for (int s = 0; s < slots; s++)
                    {
                        CHECK(!overlapsBox(psa, inputViewerCardBox(s, band, wide)));
                    }
                    if (comp)
                    {
                        CHECK(!overlapsBox(psa, diCompassBox(band, slots, wide)));
                    }

                    // Clears typical 2P lower-screen OSD stacks (HUD, Sides).
                    for (int p = 0; p < 2; p++)
                    {
                        CHECK(!overlapsBox(psa, osdStackBox(osd::kPositionHud, p, 3, wide)));
                        CHECK(!overlapsBox(psa, osdStackBox(osd::kPositionSides, p, 3, wide)));
                    }
                }
            }

            // Explicit 4:3 Panel B Frame-Step Inspector contract: Top-Left x = 32..250 (8px = F6 clear of 4:3 timer x=258).
            if (!wide && !ledge)
            {
                Box standalone = psaInspectorBox(band, 1, false, false, 0.0f);
                CHECK_NEAR(32.0f, standalone.x0, 0.01f);
                CHECK_NEAR(250.0f, standalone.x1, 0.01f);
                CHECK(standalone.x1 <= timer.x0 - 8.0f + 0.01f);
            }
        }
    }
}

TEST(psa_inspector, sd_storage_round_trip_clamping_and_migration)
{
    CHECK_EQ(16u, sizeof(main::DisplaySettings));
    CHECK_EQ(464u, sizeof(main::Settings));
    CHECK_EQ(55u, offsetof(main::Settings, display.psaInspector));

    host::card::reset();
    bootStorage();
    CHECK_EQ(static_cast<int>(main::kPsaOff), main::settings().display.psaInspector);

    // Round-trip P1 and CPU across reboot.
    main::settings().display.psaInspector = main::kPsaP1;
    storage::settingsMenuClosed();
    pumpStorage();
    bootStorage();
    CHECK_EQ(static_cast<int>(main::kPsaP1), main::settings().display.psaInspector);

    main::settings().display.psaInspector = main::kPsaCpu;
    storage::settingsMenuClosed();
    pumpStorage();
    bootStorage();
    CHECK_EQ(static_cast<int>(main::kPsaCpu), main::settings().display.psaInspector);

    // Simulate pre-PSA SD file where kAreaDisplay had used == 15: migrates psaInspector to 0 (Off).
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
    CHECK_EQ(16, dispEntry->used);
    dispEntry->used = 15;
    resealFile(name);

    bootStorage();
    CHECK_EQ(static_cast<int>(main::kPsaOff), main::settings().display.psaInspector);

    // Out-of-range value in settings block is clamped cleanly by loadSettings/sanitizeSettings.
    main::settings().display.psaInspector = 99;
    main::sanitizeSettings(main::settings());
    CHECK_EQ(static_cast<int>(main::kPsaCpu), main::settings().display.psaInspector);

    storage::detach();
}

TEST(psa_inspector, opcode_names_module_prefixes_and_typed_args)
{
    psaOpcodesAttach();
    char buf[64];

    // All key module prefixes from fudgepop01's AnimCmd spec.
    CHECK_EQ(0, strcmp(psaModulePrefix(0x00), "flow"));
    CHECK_EQ(0, strcmp(psaModulePrefix(0x02), "status"));
    CHECK_EQ(0, strcmp(psaModulePrefix(0x04), "motion"));
    CHECK_EQ(0, strcmp(psaModulePrefix(0x06), "coll"));
    CHECK_EQ(0, strcmp(psaModulePrefix(0x12), "work"));
    CHECK_EQ(0, strcmp(psaModulePrefix(0x64), "cancel"));
    CHECK(psaModulePrefix(0xFF) == NULL);

    // Named opcodes + numArgs disambiguation (00:02 with 0 args = NoEvent, 1 arg = AsyncWait).
    psaFormatOpcodeName(0x00010100u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "SyncWait"));
    psaFormatOpcodeName(0x00020000u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "NoEvent"));
    psaFormatOpcodeName(0x00020100u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "AsyncWait"));
    psaFormatOpcodeName(0x04070200u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "SetFSM"));
    psaFormatOpcodeName(0x06000D00u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "Hitbox"));
    psaFormatOpcodeName(0x64000000u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "AllowInt"));
    psaFormatOpcodeName(0x120A0100u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "BitSet"));

    // Fallback to module prefix + hex code for unnamed commands, and raw hex for unknown module.
    psaFormatOpcodeName(0x11FE0100u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "gfx:FE"));
    psaFormatOpcodeName(0x99AB0000u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "99:AB"));
    psaFormatOpcodeName(0u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "-"));

    // Typed acAnimCmdArgument formatting (0 int, 1 scalar / 60000.0f, 2 ptr, 3 bool, 4 file, 5 var, 6 req).
    psaFormatArg(0u, static_cast<u32>(-12), buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "-12"));
    psaFormatArg(1u, 120000u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "2"));
    psaFormatArg(1u, 107400u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "1.79"));
    psaFormatArg(2u, 0x80FB1234u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "@80FB1234"));
    psaFormatArg(3u, 1u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "true"));
    psaFormatArg(3u, 0u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "false"));
    psaFormatArg(4u, 0x1Au, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "f:1A"));
    psaFormatArg(5u, 0x1000004Fu, buf, sizeof(buf)); // LA-Basic[79]
    CHECK_EQ(0, strcmp(buf, "LA-Int[79]"));
    psaFormatArg(5u, 0x11000022u, buf, sizeof(buf)); // LA-Float[34]
    CHECK_EQ(0, strcmp(buf, "LA-Flt[34]"));
    psaFormatArg(5u, 0x2200000Au, buf, sizeof(buf)); // RA-Bit[10]
    CHECK_EQ(0, strcmp(buf, "RA-Bit[10]"));
    psaFormatArg(5u, 0x00000003u, buf, sizeof(buf)); // IC-Basic[3]
    CHECK_EQ(0, strcmp(buf, "IC-Int[3]"));
    psaFormatArg(6u, 0x00000004u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "R:4"));
    psaFormatArg(6u, 0x80000004u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "!R:4"));

    // Focused command formatting combines opcode name + typed args + ellipsis when numArgs > captured.
    PsaArgSnapshot args[2] = {{1u, 107400u}, {0u, 0u}};
    psaFormatFocusCommand(0x04070200u, args, 2u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "SetFSM(1.79,0)"));

    PsaArgSnapshot hitArgs[4] = {{0u, 0u}, {0u, 0u}, {0u, 12u}, {0u, 45u}};
    psaFormatFocusCommand(0x06000D00u, hitArgs, 4u, buf, sizeof(buf));
    CHECK_EQ(0, strcmp(buf, "Hitbox(0,0,12,45..)"));

    // Focus thread selection prefers SubMain (1) over ActMain (0).
    PsaThreadSnapshot th[kPsaThreadCount];
    memset(th, 0, sizeof(th));
    th[0].active = 1;
    th[0].cmdHeader = 0x00010100u;
    CHECK_EQ(0, psaSelectFocusThread(th));
    th[1].active = 1;
    th[1].cmdHeader = 0x04070200u;
    CHECK_EQ(1, psaSelectFocusThread(th));

    psaOpcodesDetach();
}
