#pragma once

#include <gf/gf_pad_status.h>
#include <types.h>

#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/ui/style.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_bits.h"

// Native Wii GX scrollable Credits modal shared by extra (in-match Lab Menu)
// and menu (CSS Boot Menu). Reuses style.cpp's native P+ modal frame, slanted
// teal header ribbon, scrollbar, and glossy vector footer badges with clean
// typographic credit rows (zero nested cards, zero vertical accent stripes).

#define TM_CREDITS_RIBBON      "Credits"
#define TM_CREDITS_TITLE       "Project+ Training Mode Mod (P+TMM)"
#define TM_CREDITS_SUBTITLE    "v1.0.1-pre4 | discord.gg/WHNtJd2KAJ"
#define TM_CREDITS_LICENSE     "GPL-3.0-or-later | github.com/skierrz/pplus-training-mode"
#define TM_CREDITS_BTN_DPAD    "D-Pad"
#define TM_CREDITS_HINT_SCROLL "Scroll"
#define TM_CREDITS_HINT_CLOSE  "Close"
#define TM_CREDITS_BTN_B       "B"
#define TM_CREDITS_BTN_1       "1"
#define TM_CREDITS_MENU_DESC   "View project credits, attributions, and license info."

#define TM_CREDITS_N0 "skier"
#define TM_CREDITS_R0A "Lead Developer, Maintainer,"
#define TM_CREDITS_R0B "Systems & UX/UI Design, Testing"

#define TM_CREDITS_N1 "Wisp"
#define TM_CREDITS_R1A "ProjectPunch (MIT, with blessing)"

#define TM_CREDITS_N2 "fudgepop01"
#define TM_CREDITS_R2A "PPlusAI & SuperTraining (with permission)"

#define TM_CREDITS_N3 "lil klon"
#define TM_CREDITS_R3A "Start On Input, On-Ledge reversals"
#define TM_CREDITS_R3B "& P+ 3.2.1 notes (with permission)"

#define TM_CREDITS_N4 "ilikepizza107"
#define TM_CREDITS_R4A "Partially Invisible Techs"

#define TM_CREDITS_N5 "Project+ Dev Team & PMDT"
#define TM_CREDITS_R5A "Eon, QuickLava, Magus, DukeItOut,"
#define TM_CREDITS_R5B "Fracture17, Kapedani & contributors"

#define TM_CREDITS_N6 "Sammi Husky"
#define TM_CREDITS_R6A "Syriinge, Plugin Template & BrawlHeaders"

#define TM_CREDITS_N7 "doldecomp/brawl & BrawlRE"
#define TM_CREDITS_R7A "Brawl disassembly & documentation"

#define TM_CREDITS_N8 "Brawl Online & Brawlback"
#define TM_CREDITS_R8A "sarahkittyy & the Brawlback team"

namespace tm {
namespace common {
    struct CreditsEntry {
        const char* name;
        const char* role1;
        const char* role2;
    };

    enum {
        kCreditsEntryCount = 9
    };

    // Modal frame layout in 4:3 coordinates matching osd_quick.cpp (56..584 x 34..446,
    // well inside the 5% 480i CRT safe area 32..608 x 24..456).
    static const float kCreditsLeft43 = 56.0f;
    static const float kCreditsRight43 = 584.0f;
    static const float kCreditsTopY = 34.0f;
    static const float kCreditsBotY = 446.0f;
    static const float kCreditsHdrBot = 71.0f;   // 34 px (kFibBandH) native header
    static const float kCreditsMetaTop = 73.0f;  // + 2 px divider
    static const float kCreditsMetaBot = 94.0f;  // 21 px (kFibRowH) meta band (matches Lab crumb bar)
    static const float kCreditsViewTopY = 96.0f; // + 2 px divider
    static const float kCreditsViewBotY = 379.0f;
    static const float kCreditsFootTop = 407.0f; // 26 px license strip + 2 px divider -> 34 px footer
    static const float kCreditsRibbonR43 = 188.0f;

    static const float kCreditsInL43 = 59.0f;
    static const float kCreditsInR43 = 581.0f;
    static const float kCreditsTextX43 = 75.0f;
    static const float kCreditsScrollL43 = 565.0f;
    static const float kCreditsTextMaxW43 = 480.0f;
    static const float kCreditsRuleW43 = 480.0f;

    // Open typographic vertical rhythm (tight inside each credit, generous between credits).
    static const float kCreditsTopPad = 12.0f;
    static const float kCreditsBotPad = 14.0f;
    static const float kCreditsLeadStep = 59.0f;   // skier (2 lines + section breath + 2px divider)
    static const float kCreditsRow1H = 24.0f;      // name (10.5) + gap (4.5) + role1 (9.0)
    static const float kCreditsRow2H = 37.0f;      // + role2 (13.0 pitch)
    static const float kCreditsRowGap = 16.0f;     // generous vertical separation w/ centered 2px hairline
    // Content height: 12 + 59 + 2*(37+16) + 5*(24+16) + 24 + 14 = 415.0 px; viewH = 283.0 px.
    static const float kCreditsContentH = 415.0f;
    static const float kCreditsMaxScroll = 132.0f;

    static const float kCreditsTitleCap = ui::style::kTypeBody;
    static const float kCreditsSubCap = ui::style::kTypeMeta;
    static const float kCreditsNameCap = ui::style::kTypeBody;
    static const float kCreditsRoleCap = ui::style::kTypeMeta;
    static const float kCreditsLicCap = ui::style::kTypeMeta;

    struct CreditsModalState {
        float targetScroll;
        float visualScroll;
        u8 open;
        u8 ctrlType;
        u8 waitNeutral;
        u8 enterTick;
        u8 exitTick;
    };

    inline const CreditsEntry* creditsEntries()
    {
        static const CreditsEntry kEntries[kCreditsEntryCount] = {
            {TM_CREDITS_N0, TM_CREDITS_R0A, TM_CREDITS_R0B},
            {TM_CREDITS_N1, TM_CREDITS_R1A, NULL},
            {TM_CREDITS_N2, TM_CREDITS_R2A, NULL},
            {TM_CREDITS_N3, TM_CREDITS_R3A, TM_CREDITS_R3B},
            {TM_CREDITS_N4, TM_CREDITS_R4A, NULL},
            {TM_CREDITS_N5, TM_CREDITS_R5A, TM_CREDITS_R5B},
            {TM_CREDITS_N6, TM_CREDITS_R6A, NULL},
            {TM_CREDITS_N7, TM_CREDITS_R7A, NULL},
            {TM_CREDITS_N8, TM_CREDITS_R8A, NULL}
        };
        return kEntries;
    }

    inline float creditsModalContentHeight()
    {
        return kCreditsContentH;
    }

    inline float creditsModalMaxScroll()
    {
        return kCreditsMaxScroll;
    }

    inline void creditsModalReset(CreditsModalState& st)
    {
        st.targetScroll = 0.0f;
        st.visualScroll = 0.0f;
        st.open = 0;
        st.ctrlType = 0;
        st.waitNeutral = 0;
        st.enterTick = 0;
        st.exitTick = 0;
    }

    inline void creditsModalOpen(CreditsModalState& st, u8 ctrlType)
    {
        st.targetScroll = 0.0f;
        st.visualScroll = 0.0f;
        st.open = 1;
        st.ctrlType = ctrlType;
        st.waitNeutral = 1;
        st.enterTick = 8;
        st.exitTick = 0;
    }

    inline bool creditsModalVisible(const CreditsModalState& st)
    {
        return st.open != 0 || st.exitTick != 0;
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    // Steps 60fps scrolling and close handling. Returns a game::kSound* id or -1.
    static int creditsModalStep(CreditsModalState& st, u32 held, u32 pressed, s8 stickY, u8 ctrlType)
    {
        if (!st.open)
        {
            return -1;
        }
        st.ctrlType = ctrlType;
        const u32 closeMask = game::kPadB | game::kPadStart | game::kPadClassicPlus;
        if (st.waitNeutral && (held & closeMask) == 0u)
        {
            st.waitNeutral = 0;
        }
        if (!st.waitNeutral && (pressed & closeMask) != 0u)
        {
            st.open = 0;
            st.enterTick = 0;
            st.exitTick = 4;
            return game::kSoundBack;
        }

        float delta = 0.0f;
        if ((held & game::kPadDUp) != 0u) delta -= 3.75f;
        if ((held & game::kPadDDown) != 0u) delta += 3.75f;
        int sy = static_cast<int>(stickY);
        if (sy > 28 || sy < -28)
        {
            int mag = (sy > 0 ? sy : -sy) - 28;
            float frac = static_cast<float>(mag) / 64.0f;
            if (frac > 1.0f) frac = 1.0f;
            float spd = 1.25f + 4.25f * frac;
            delta += sy > 0 ? -spd : spd;
        }

        if (delta != 0.0f)
        {
            st.targetScroll += delta;
            if (st.targetScroll < 0.0f) st.targetScroll = 0.0f;
            else if (st.targetScroll > kCreditsMaxScroll) st.targetScroll = kCreditsMaxScroll;
        }
        st.visualScroll = ui::style::chase60(st.visualScroll, st.targetScroll, 0.35f);
        if (st.visualScroll < 0.0f) st.visualScroll = 0.0f;
        else if (st.visualScroll > kCreditsMaxScroll) st.visualScroll = kCreditsMaxScroll;
        return -1;
    }

    static void creditsModalDraw(CreditsModalState& st)
    {
        if (!creditsModalVisible(st) || gfx::lineHeight(ui::style::scaleForCap(kCreditsNameCap)) <= 0.0f)
        {
            return;
        }

        using namespace ui::style;
        float alpha = 1.0f;
        float dy = 0.0f;
        if (st.open)
        {
            if (st.enterTick > 0)
            {
                float u = 1.0f - static_cast<float>(9 - st.enterTick) / 8.0f;
                float ease = 1.0f - u * u * u;
                alpha = 0.25f + 0.75f * ease;
                dy = snapPx((1.0f - ease) * 6.0f);
                st.enterTick--;
            }
        }
        else if (st.exitTick > 0)
        {
            float u = 1.0f - static_cast<float>(5 - st.exitTick) / 4.0f;
            float ease = 1.0f - u * u * u * u;
            alpha = 1.0f - ease;
            dy = snapPx(ease * 4.0f);
            st.exitTick--;
        }
        if (alpha <= 0.01f)
        {
            return;
        }

        float pa = alpha >= 0.99f ? 1.0f : (0.25f + 0.75f * alpha);

        // 1. Native P+ modal frame (slanted teal header ribbon + // stripes + smoked charcoal body).
        // descTop = 0.0f so MENU and EXTRA render identically; license band is drawn explicitly below.
        static const ModalFrame kFrame = {
            kCreditsLeft43, kCreditsRight43, kCreditsTopY, kCreditsBotY, kCreditsHdrBot, 0.0f, kCreditsFootTop,
            kCreditsRibbonR43, kLabModalBackdropHeavy
        };
        drawModalFrame(kFrame, TM_CREDITS_RIBBON, dy, alpha);

        float bodyX = cx(kCreditsInL43);
        float bodyW = w(kCreditsInR43 - kCreditsInL43);

        // 2. Sub-header meta strip (21 px, matching Lab subpage breadcrumb band; zero accent stripes)
        //    + license description band (26 px, matching Quick Setup / Lab description band).
        gfx::fillGradV(bodyX, kCreditsMetaTop + dy, bodyW, kCreditsMetaBot - kCreditsMetaTop,
                       scaleAlpha(kLabRailTop, pa), scaleAlpha(kLabRailBot, pa));
        gfx::fillRect(bodyX, kCreditsMetaBot + dy, bodyW, kFibHair, scaleAlpha(kLabDivider, pa));

        gfx::fillRect(bodyX, kCreditsViewBotY + dy, bodyW, kFibHair, scaleAlpha(kLabDivider, pa));
        gfx::fillGradV(bodyX, kCreditsViewBotY + kFibHair + dy, bodyW,
                       kCreditsFootTop - (kCreditsViewBotY + kFibHair), scaleAlpha(kLabDescTopCol, pa),
                       scaleAlpha(kLabDescBotCol, pa));

        // 3. Header right title + sub-header version/Discord line.
        float hdrMid = (kCreditsTopY + kLabBorder + kCreditsHdrBot) * 0.5f + dy;
        float titleScale = scaleForCap(kCreditsTitleCap);
        float titleMaxW = w(kCreditsInR43 - kCreditsRibbonR43 - kFibPad - 6.0f);
        float titleW = gfx::textWidth(TM_CREDITS_TITLE, titleScale);
        float titleDrawW = titleW > titleMaxW ? titleMaxW : titleW;
        drawFitText(cx(kCreditsInR43 - kFibPad) - titleDrawW, textYForCap(hdrMid - kCreditsTitleCap * 0.5f, titleScale),
                    TM_CREDITS_TITLE, scaleAlpha(kLabTitleBot, pa), titleScale, titleMaxW);

        float subScale = scaleForCap(kCreditsSubCap);
        float metaMid = (kCreditsMetaTop + kCreditsMetaBot) * 0.5f + dy;
        drawFitText(cx(kCreditsInL43 + kFibPad), textYForCap(metaMid - kCreditsSubCap * 0.5f, subScale),
                    TM_CREDITS_SUBTITLE, scaleAlpha(kLabMutedText, pa), subScale, w(496.0f));

        // 4. Hardware scissor-clipped open typographic credits list (batched: rules pass, then text pass).
        float viewTop = kCreditsViewTopY + dy;
        float viewBot = kCreditsViewBotY + dy;
        float diff = st.targetScroll - st.visualScroll;
        float drawScroll = (diff > -0.25f && diff < 0.25f) ? snapPx(st.targetScroll) : st.visualScroll;

        gfx::ScissorState sc;
        if (gfx::pushScissor(bodyX, viewTop, cx(kCreditsScrollL43 - 2.0f), viewBot, sc))
        {
            const CreditsEntry* entries = creditsEntries();
            float tx = cx(kCreditsTextX43);
            float maxW = w(kCreditsTextMaxW43);
            float ruleW = w(kCreditsRuleW43);
            float nameScale = scaleForCap(kCreditsNameCap);
            float roleScale = scaleForCap(kCreditsRoleCap);

            // Pass 1: clean CRT-safe 2px horizontal separators (stronger divider below Lead Maintainer).
            float y = viewTop + kCreditsTopPad - drawScroll;
            for (int i = 0; i < kCreditsEntryCount; i++)
            {
                bool twoLine = entries[i].role2 != NULL;
                float step = i == 0 ? kCreditsLeadStep : ((twoLine ? kCreditsRow2H : kCreditsRow1H) + kCreditsRowGap);
                if (i + 1 < kCreditsEntryCount)
                {
                    float ruleY = i == 0 ? (y + 47.0f) : (y + (twoLine ? kCreditsRow2H : kCreditsRow1H) + 7.0f);
                    if (ruleY + kFibHair > viewTop && ruleY < viewBot)
                    {
                        gfx::Rgba ruleCol = i == 0 ? kLabDivider : 0x283444B0u;
                        gfx::fillRect(tx, snapPx(ruleY), ruleW, kFibHair, scaleAlpha(ruleCol, pa));
                    }
                }
                y += step;
            }

            // Pass 2: crisp names + slate-tinted roles.
            y = viewTop + kCreditsTopPad - drawScroll;
            for (int i = 0; i < kCreditsEntryCount; i++)
            {
                bool twoLine = entries[i].role2 != NULL;
                float bodyH = twoLine ? kCreditsRow2H : kCreditsRow1H;
                float step = i == 0 ? kCreditsLeadStep : (bodyH + kCreditsRowGap);
                if (y + bodyH > viewTop - 2.0f && y < viewBot + 2.0f)
                {
                    gfx::Rgba nameCol = scaleAlpha(i == 0 ? kLabFocusGold : kLabWhiteText, pa);
                    gfx::Rgba roleCol = scaleAlpha(i == 0 ? kLabDescText : kLabMutedText, pa);
                    drawFitText(tx, textYForCap(y, nameScale), entries[i].name, nameCol, nameScale, maxW);
                    drawFitText(tx, textYForCap(y + 15.0f, roleScale), entries[i].role1, roleCol, roleScale, maxW);
                    if (twoLine)
                    {
                        drawFitText(tx, textYForCap(y + 28.0f, roleScale), entries[i].role2, roleCol, roleScale, maxW);
                    }
                }
                y += step;
            }

            // Soft edge fades when scrollable content extends past top/bottom.
            float fadeW = cx(kCreditsScrollL43 - 4.0f) - bodyX;
            if (drawScroll > 1.0f)
            {
                gfx::fillGradV(bodyX, viewTop, fadeW, 14.0f, scaleAlpha(0x131921E8u, pa), 0x13192100u);
            }
            if (drawScroll < kCreditsMaxScroll - 1.0f)
            {
                gfx::fillGradV(bodyX, viewBot - 14.0f, fadeW, 14.0f, 0x0D111700u, scaleAlpha(0x0D1117E8u, pa));
            }
            gfx::popScissor(sc);
        }

        // 5. Native shared scrollbar (matching Lab Menu & Quick Setup).
        drawScrollBarFloat(kCreditsScrollL43, viewTop + kFibGap, (viewBot - viewTop) - 2.0f * kFibGap,
                           static_cast<int>(kCreditsContentH), drawScroll,
                           static_cast<int>(kCreditsViewBotY - kCreditsViewTopY), scaleAlpha(kLabDivider, pa),
                           scaleAlpha(kLabTealTop, pa), scaleAlpha(kLabTealBot, pa));

        // 6. License strip + unified glossy vector footer badges (matching Lab Menu, Quick Setup & Settings Modal).
        float licScale = scaleForCap(kCreditsLicCap);
        float licMid = (kCreditsViewBotY + kFibHair + kCreditsFootTop) * 0.5f + dy;
        drawFitText(cx(kCreditsInL43 + kFibPad), textYForCap(licMid - kCreditsLicCap * 0.5f, licScale),
                    TM_CREDITS_LICENSE, scaleAlpha(kLabDescText, pa), licScale, w(496.0f));

        float footMid = (kCreditsFootTop + kFibHair + (kCreditsBotY - kLabBorder)) * 0.5f + dy;
        const char* closeBtn = st.ctrlType == gfPadType::WIIMOTE ? TM_CREDITS_BTN_1 : TM_CREDITS_BTN_B;
        gfx::Rgba footInk = scaleAlpha(kLabMutedText, pa);
        FooterBadgeItem foot[2] = {
            {TM_CREDITS_BTN_DPAD, TM_CREDITS_HINT_SCROLL, kBtnLight, kBadgeTightGap | kBadgeWidePad, footInk},
            {closeBtn, TM_CREDITS_HINT_CLOSE, kBtnB, kBadgeRound | kBadgeTightGap, footInk}
        };
        drawFooterBadges(cx(kCreditsInL43 + kFibPad), footMid, 17.0f, foot, 2);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif
}
}
