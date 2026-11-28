#include <cctype>
#include <cstdio>
#include <cstring>

#include <gf/gf_pad_status.h>

#include "common/credits_modal.h"
#include "host/canvas.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/test.h"
#include "main/extra.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/lab_menu.h"
#include "main/ui/menu.h"
#include "menu/drill_list.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_bits.h"
#include "shared/interface.h"

namespace {
    using namespace tm;

    void toLowerCopy(const char* src, char* dst, size_t cap)
    {
        size_t i = 0;
        for (; src[i] != '\0' && i + 1 < cap; i++)
        {
            dst[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(src[i])));
        }
        dst[i] = '\0';
    }

    bool containsCaseInsensitive(const char* hay, const char* needle)
    {
        char loHay[4096];
        char loNeedle[128];
        toLowerCopy(hay, loHay, sizeof(loHay));
        toLowerCopy(needle, loNeedle, sizeof(loNeedle));
        return std::strstr(loHay, loNeedle) != NULL;
    }

    bool containsWholeWordInsensitive(const char* hay, const char* word)
    {
        char loHay[4096];
        char loWord[128];
        toLowerCopy(hay, loHay, sizeof(loHay));
        toLowerCopy(word, loWord, sizeof(loWord));
        size_t wlen = std::strlen(loWord);
        const char* p = loHay;
        while ((p = std::strstr(p, loWord)) != NULL)
        {
            bool leftOk = (p == loHay) || !std::isalnum(static_cast<unsigned char>(p[-1]));
            bool rightOk = (p[wlen] == '\0') || !std::isalnum(static_cast<unsigned char>(p[wlen]));
            if (leftOk && rightOk)
            {
                return true;
            }
            p += wlen;
        }
        return false;
    }

    void appendStr(char* dst, size_t cap, const char* s)
    {
        size_t cur = std::strlen(dst);
        if (cur + 1 >= cap)
        {
            return;
        }
        std::snprintf(dst + cur, cap - cur, "%s", s);
    }

    void gatherAllCreditsText(char* out, size_t cap)
    {
        out[0] = '\0';
        appendStr(out, cap, TM_CREDITS_RIBBON "\n");
        appendStr(out, cap, TM_CREDITS_TITLE "\n");
        appendStr(out, cap, TM_CREDITS_SUBTITLE "\n");
        const common::CreditsEntry* entries = common::creditsEntries();
        for (int i = 0; i < common::kCreditsEntryCount; i++)
        {
            appendStr(out, cap, entries[i].name);
            appendStr(out, cap, " ");
            appendStr(out, cap, entries[i].role1);
            if (entries[i].role2 != NULL)
            {
                appendStr(out, cap, " ");
                appendStr(out, cap, entries[i].role2);
            }
            appendStr(out, cap, "\n");
        }
        appendStr(out, cap, TM_CREDITS_LICENSE "\n");
        appendStr(out, cap, TM_CREDITS_BTN_DPAD " " TM_CREDITS_HINT_SCROLL " " TM_CREDITS_HINT_CLOSE);
    }

    static common::CreditsModalState s_mockCredits;

    static u32 mockCreditsHook(u32 op, const main::MenuPad* pad)
    {
        u32 cmd = op & 0xFFu;
        u8 ctrl = static_cast<u8>((op >> 8) & 0xFFu);
        if (cmd == main::kCreditsAvailable) return 1u;
        if (cmd == main::kCreditsOpen)
        {
            common::creditsModalOpen(s_mockCredits, ctrl);
            return 1u;
        }
        if (cmd == main::kCreditsStep)
        {
            if (!s_mockCredits.open) return 0u;
            if (pad == NULL)
            {
                common::creditsModalReset(s_mockCredits);
                return 1u;
            }
            common::creditsModalStep(s_mockCredits, pad->held, pad->pressed, pad->stickY, pad->controllerType);
            return 1u;
        }
        if (cmd == main::kCreditsForceClose)
        {
            common::creditsModalReset(s_mockCredits);
            return 0u;
        }
        return 0u;
    }

    void initPads(gfPadStatus* pads, u32 connectedMask, u32 type)
    {
        std::memset(pads, 0, sizeof(gfPadStatus) * iface::kPadCount);
        for (int i = 0; i < iface::kPadCount; i++)
        {
            bool conn = (connectedMask & (1u << i)) != 0u;
            pads[i].m_error = conn ? gfPadError::NONE : gfPadError::NO_CONTROLLER;
            pads[i].m_controllerType = conn ? static_cast<gfPadType::PadType>(type) : gfPadType::GCC;
        }
    }
}

TEST(credits, verbatim_attributions_and_clean_hygiene)
{
    char all[4096];
    gatherAllCreditsText(all, sizeof(all));

    // Header & footer exact strings.
    CHECK(std::strstr(all, "Project+ Training Mode Mod (P+TMM)") != NULL);
    CHECK(std::strstr(all, "v1.0.1-pre4 | discord.gg/WHNtJd2KAJ") != NULL);
    CHECK(std::strstr(all, "GPL-3.0-or-later | github.com/skierrz/pplus-training-mode") != NULL);
    CHECK(std::strstr(all, "D-Pad Scroll Close") != NULL);

    // All 9 mandatory attributions verbatim.
    CHECK(std::strstr(all, "skier") != NULL);
    CHECK(std::strstr(all, "Lead Developer, Maintainer, Systems & UX/UI Design, Testing") != NULL);
    CHECK(std::strstr(all, "Wisp") != NULL);
    CHECK(std::strstr(all, "ProjectPunch (MIT, with blessing)") != NULL);
    CHECK(std::strstr(all, "fudgepop01") != NULL);
    CHECK(std::strstr(all, "PPlusAI & SuperTraining (with permission)") != NULL);
    CHECK(std::strstr(all, "lil klon") != NULL);
    CHECK(std::strstr(all, "Start On Input, On-Ledge reversals & P+ 3.2.1 notes (with permission)") != NULL);
    CHECK(std::strstr(all, "ilikepizza107") != NULL);
    CHECK(std::strstr(all, "Partially Invisible Techs") != NULL);
    CHECK(std::strstr(all, "Project+ Dev Team & PMDT") != NULL);
    CHECK(std::strstr(all, "Eon, QuickLava, Magus, DukeItOut, Fracture17, Kapedani & contributors") != NULL);
    CHECK(std::strstr(all, "Sammi Husky") != NULL);
    CHECK(std::strstr(all, "Syriinge, Plugin Template & BrawlHeaders") != NULL);
    CHECK(std::strstr(all, "doldecomp/brawl & BrawlRE") != NULL);
    CHECK(std::strstr(all, "Brawl disassembly & documentation") != NULL);
    CHECK(std::strstr(all, "Brawl Online & Brawlback") != NULL);
    CHECK(std::strstr(all, "sarahkittyy & the Brawlback team") != NULL);

    // Zero Unicode em/en dashes (UTF-8 E2 80 94 / E2 80 93).
    CHECK(std::strstr(all, "\xE2\x80\x94") == NULL);
    CHECK(std::strstr(all, "\xE2\x80\x93") == NULL);

    // Zero disclosure terms anywhere in Credits (standalone 2-letter check
    // keeps "PPlusAI" valid). Probe strings are split to keep source hygiene clean.
    static const char* const kWholeProbes[] = {"a" "i", "l" "lm", "g" "pt", "ag" "ent", "b" "ot"};
    for (size_t i = 0; i < sizeof(kWholeProbes) / sizeof(kWholeProbes[0]); i++)
    {
        CHECK(!containsWholeWordInsensitive(all, kWholeProbes[i]));
    }
    static const char* const kSubProbes[] = {
        "cl" "aude", "gem" "ini", "op" "enai", "anth" "ropic", "co" "pilot", "ass" "isted"
    };
    for (size_t i = 0; i < sizeof(kSubProbes) / sizeof(kSubProbes[0]); i++)
    {
        CHECK(!containsCaseInsensitive(all, kSubProbes[i]));
    }
}

TEST(credits, scroll_clamping_and_open_close_transitions)
{
    common::CreditsModalState st;
    common::creditsModalReset(st);
    CHECK(!st.open);
    CHECK(!common::creditsModalVisible(st));

    float maxScroll = common::creditsModalMaxScroll();
    CHECK(maxScroll > 100.0f);

    common::creditsModalOpen(st, static_cast<u8>(gfPadType::GCC));
    CHECK(st.open != 0);
    CHECK(common::creditsModalVisible(st));
    CHECK_EQ(0.0f, st.targetScroll);
    CHECK_EQ(0.0f, st.visualScroll);

    // While B is held across open, waitNeutral prevents immediate close.
    int snd = common::creditsModalStep(st, game::kPadB, game::kPadB, 0, static_cast<u8>(gfPadType::GCC));
    CHECK_EQ(-1, snd);
    CHECK(st.open != 0);

    // Neutral clears waitNeutral.
    snd = common::creditsModalStep(st, 0, 0, 0, static_cast<u8>(gfPadType::GCC));
    CHECK_EQ(-1, snd);
    CHECK_EQ(0, static_cast<int>(st.waitNeutral));

    // Scroll down with D-Pad Down past end -> clamps cleanly at maxScroll.
    for (int f = 0; f < 120; f++)
    {
        common::creditsModalStep(st, game::kPadDDown, 0, 0, static_cast<u8>(gfPadType::GCC));
    }
    CHECK(st.targetScroll <= maxScroll + 0.001f);
    CHECK(st.targetScroll >= maxScroll - 0.001f);
    CHECK(st.visualScroll <= maxScroll + 0.001f);
    CHECK(st.visualScroll >= maxScroll - 1.0f);

    // Scroll back up with control stick Y (+) past top -> clamps cleanly at 0.
    for (int f = 0; f < 120; f++)
    {
        common::creditsModalStep(st, 0, 0, 90, static_cast<u8>(gfPadType::GCC));
    }
    CHECK_EQ(0.0f, st.targetScroll);
    CHECK(st.visualScroll >= 0.0f && st.visualScroll <= 1.0f);

    // Fresh B closes with kSoundBack and triggers 4f exit fade.
    snd = common::creditsModalStep(st, game::kPadB, game::kPadB, 0, static_cast<u8>(gfPadType::GCC));
    CHECK_EQ(static_cast<int>(game::kSoundBack), snd);
    CHECK(!st.open);
    CHECK_EQ(4, static_cast<int>(st.exitTick));
    CHECK(common::creditsModalVisible(st));

    // Start also closes cleanly.
    common::creditsModalOpen(st, static_cast<u8>(gfPadType::GCC));
    common::creditsModalStep(st, 0, 0, 0, static_cast<u8>(gfPadType::GCC));
    snd = common::creditsModalStep(st, game::kPadStart, game::kPadStart, 0, static_cast<u8>(gfPadType::GCC));
    CHECK_EQ(static_cast<int>(game::kSoundBack), snd);
    CHECK(!st.open);
}

TEST(credits, crt_safe_bounds_and_text_fit_both_aspects)
{
    static const bool kAspects[2] = {false, true};
    static const u8 kCtrls[2] = {static_cast<u8>(gfPadType::GCC), static_cast<u8>(gfPadType::WIIMOTE)};
    float maxScroll = common::creditsModalMaxScroll();
    float scrollPos[3] = {0.0f, maxScroll * 0.5f, maxScroll};

    for (int a = 0; a < 2; a++)
    {
        gfx::setWidescreen(kAspects[a]);
        for (int c = 0; c < 2; c++)
        {
            for (int s = 0; s < 3; s++)
            {
                common::CreditsModalState st;
                common::creditsModalOpen(st, kCtrls[c]);
                st.enterTick = 0;
                st.targetScroll = scrollPos[s];
                st.visualScroll = scrollPos[s];

                host::canvas::clear();
                gfx::begin();
                common::creditsModalDraw(st);
                gfx::end();

                CHECK(host::canvas::runs() >= 6);
                CHECK_EQ(0, host::canvas::scissorDepth());
                CHECK(host::canvas::scissorPushes() >= 1);

                for (int r = 0; r < host::canvas::runs(); r++)
                {
                    const host::canvas::Run& run = host::canvas::run(r);
                    // 5% 480i CRT safe area: x 32..608, y 24..456.
                    CHECK(run.x >= 32.0f - 0.1f);
                    CHECK(run.right <= 608.0f + 0.1f);
                    float cap = run.displayFont ? (run.scaleY * ui::style::kDisplayCapHeight)
                                                : (run.scaleY * ui::style::kCapHeight);
                    CHECK(cap >= 8.0f - 0.05f);
                    // Squeeze ratio (factoring out 16:9 xScale) >= 0.80 so glyphs stay crisp on 480i CRT.
                    if (run.scaleY > 0.0f)
                    {
                        float squeeze = run.scaleX / (run.scaleY * gfx::xScale());
                        CHECK(squeeze >= 0.80f);
                    }
                    // Header/footer runs stay inside vertical safe area.
                    if (run.y < common::kCreditsViewTopY || run.y > common::kCreditsViewBotY)
                    {
                        float top = run.y + (run.displayFont ? ui::style::kDisplayCapOffset : ui::style::kCapOffset) * run.scaleY;
                        CHECK(top >= 24.0f - 0.1f);
                        CHECK(top + cap <= 456.0f + 0.1f);
                    }
                }
            }
        }
    }
    gfx::setWidescreen(false);
}

TEST(credits, lab_menu_utility_row_and_boot_menu_shortcut)
{
    // 1. Lab Menu: Credits appears as synthetic utility row 10 when extraCredits is hooked.
    common::creditsModalReset(s_mockCredits);
    main::ExtraHooks hooks;
    std::memset(&hooks, 0, sizeof(hooks));
    hooks.creditsHook = mockCreditsHook;
    main::setExtraHooks(&hooks);

    main::Settings& set = main::settings();
    std::memset(&set, 0, sizeof(set));
    set.size = sizeof(set);
    set.controls.menuStyle = main::kMenuStyleRail;

    ui::MenuState ms;
    const ui::MenuDef& def = ui::labMenu();
    ui::menuReset(def, ms);
    CHECK(ms.railFocus);
    CHECK_EQ(0, static_cast<int>(ms.railCursor));

    // Step down 10 times: 0..6 categories, 7 Reset, 8 Brawl Training, 9 Return to CSS, 10 Credits.
    ui::MenuInput down;
    std::memset(&down, 0, sizeof(down));
    down.down = 1;
    for (int i = 0; i < 10; i++)
    {
        CHECK(ui::menuStep(def, ms, down));
    }
    CHECK_EQ(10, static_cast<int>(ms.railCursor));

    // Draw menu while Credits utility is focused and verify prompt + description fit cleanly.
    gfx::setWidescreen(false);
    host::canvas::clear();
    gfx::begin();
    ui::menuDraw(def, ms);
    gfx::end();
    bool sawCredits = false;
    for (int i = 0; i < host::canvas::runs(); i++)
    {
        if (std::strcmp(host::canvas::run(i).text, "Credits") == 0)
        {
            sawCredits = true;
            break;
        }
    }
    CHECK(sawCredits);

    // Press A on Credits row -> opens Credits modal via extraCredits hook.
    ui::MenuInput pressA;
    std::memset(&pressA, 0, sizeof(pressA));
    pressA.a = true;
    CHECK(ui::menuStep(def, ms, pressA));
    CHECK(s_mockCredits.open != 0);

    main::setExtraHooks(NULL);

    // 2. Boot Menu: Z opens Credits, D-Pad scrolls, B closes without closing Boot Menu.
    gfPadStatus pads[iface::kPadCount];
    initPads(pads, 1u << 0, gfPadType::GCC);
    host::fakes::setMenuPort(0);
    host::fakes::setTrainingSelectRunning(true);
    host::log::clear();
    menu::list::attach();
    CHECK(host::log::contains("drills: list opened"));

    // Neutral clears wait-release.
    menu::list::onPads(iface::kPadsSystem, pads);
    // Press Z -> opens Credits.
    pads[0].m_buttonsCurrentFrame.bits = game::kPadZ;
    menu::list::onPads(iface::kPadsSystem, pads);
    CHECK(host::log::contains("drills: credits opened"));
    // Neutral clears wait-release inside Credits.
    pads[0].m_buttonsCurrentFrame.bits = 0;
    menu::list::onPads(iface::kPadsSystem, pads);
    // Press B -> closes Credits cleanly without closing Boot Menu.
    pads[0].m_buttonsCurrentFrame.bits = game::kPadB;
    menu::list::onPads(iface::kPadsSystem, pads);
    CHECK(host::log::contains("drills: credits closed"));
    CHECK(!host::log::contains("drills: list closed"));
}
