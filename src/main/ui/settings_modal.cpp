#include "main/ui/settings_modal.h"

#include <cstdio>
#include <gf/gf_pad_status.h>

#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/storage/settings_file.h"
#include "main/ui/menu.h"
#include "main/ui/style.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_bits.h"

namespace tm {
namespace ui {
    using namespace style;

    enum { kStickDeadZone = 40 };

    static u8 s_cursor = 0;
    static u8 s_ctrlType = 0;
    static u8 s_waitNeutral = 1;
    static u8 s_dirHeld = 0;

    void settingsModalAttach()
    {
        s_cursor = 0;
        s_ctrlType = 0;
        s_waitNeutral = 1;
        s_dirHeld = 0;
    }

    bool settingsModalOpen()
    {
        return storage::settingsUpgradePending();
    }

    int settingsModalCursor()
    {
        return s_cursor;
    }

    bool settingsModalStep(const SettingsModalInput& in)
    {
        if (!storage::settingsUpgradePending())
        {
            return false;
        }
        s_ctrlType = static_cast<u8>(in.controllerType < 4 ? in.controllerType : 0);
        if (s_waitNeutral)
        {
            if (in.held == 0 && in.stickX > -kStickDeadZone && in.stickX < kStickDeadZone
                && in.stickY > -kStickDeadZone && in.stickY < kStickDeadZone)
            {
                s_waitNeutral = 0;
            }
            return false;
        }
        bool up = (in.held & game::kPadDUp) != 0 || in.stickY > kStickDeadZone;
        bool down = (in.held & game::kPadDDown) != 0 || in.stickY < -kStickDeadZone;
        if (up != down)
        {
            if (!s_dirHeld)
            {
                s_dirHeld = 1;
                int next = static_cast<int>(s_cursor) + (down ? 1 : -1);
                if (next >= 0 && next < storage::kUpgradeChoiceCount)
                {
                    s_cursor = static_cast<u8>(next);
                    menuSound(game::kSoundMove);
                }
            }
        }
        else
        {
            s_dirHeld = 0;
        }
        if ((in.pressed & game::kPadA) != 0)
        {
            menuSound(game::kSoundConfirm);
            storage::settingsUpgradeApply(s_cursor);
            return true;
        }
        return false;
    }

    static void fitText(float x, float y, const char* str, gfx::Rgba col, float cap, float maxW)
    {
        float s = scaleForCap(cap);
        float tw = gfx::textWidth(str, s);
        gfx::text(x, textYForCap(y, s), str, col, tw > maxW ? s * maxW / tw : s, s);
    }

    void settingsModalDraw()
    {
        if (!storage::settingsUpgradePending() || gfx::lineHeight(scaleForCap(10.5f)) <= 0.0f)
        {
            return;
        }

        // Smoked-charcoal v1 Direction A upgrade modal (5% CRT safe, >= 2px scanlines).
        gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, 0x070B12C8u);
        const float left43 = 96.0f;
        const float right43 = 544.0f;
        const float topY = 92.0f;
        const float botY = 388.0f;
        const float b = kLabBorder;
        const float inL43 = left43 + b;
        const float inR43 = right43 - b;
        const float inTop = topY + b;
        const float inBot = botY - b;
        const float slantTan = 0.445f;
        float xs = gfx::xScale();
        drawFrameBorder(left43, right43, topY, botY, b, kLabBorderColor);

        static const struct {
            float y, h;
            gfx::Rgba top, bot;
        } kBands[5] = {
            {95.0f,  kFibBandH, kLabHeaderTop,    kLabHeaderBot},
            {129.0f, kFibHair,  kLabDivider,      kLabDivider},
            {131.0f, 218.0f,    kLabBaseTop,      kLabBaseBot},
            {349.0f, kFibHair,  kLabDivider,      kLabDivider},
            {351.0f, kFibBandH, kLabFooterTopCol, kLabFooterBotCol}
        };
        float bodyX = cx(inL43);
        float bodyW = w(inR43 - inL43);
        for (int i = 0; i < 5; i++)
        {
            gfx::fillGradV(bodyX, kBands[i].y, bodyW, kBands[i].h, kBands[i].top, kBands[i].bot);
        }

        // Flush-left emerald-teal slanted header ribbon + // diagonal stripes.
        float ribbonRight43 = 446.0f;
        drawSlantBar(inL43, ribbonRight43, inTop, kFibBandH, true, kLabTealTop, kLabTealBot, 0);
        drawDiagStripes(cx(ribbonRight43 - 6.0f), inTop, inTop + kFibBandH, w(kFibBandH * slantTan), w(4.5f), w(3.5f),
                        kLabStripeCol);

        float titleCap = kTypeHeading;
        float ts = displayScaleForCap(titleCap);
        const char* title = "UPDATE SAVED SETTINGS";
        gfx::displayText(cx(inL43 + kFibPad), displayYForCap(inTop + (kFibBandH - titleCap) * 0.5f, ts), title,
                         kLabActiveTabText, kLabActiveTabText, ts, ts, 0.0f, 0);
        float ss = scaleForCap(kTypeMeta);
        const char* sub = "Settings from an older build were found on SD.";
        gfx::text(320.0f - gfx::textWidth(sub, ss) * 0.5f, textYForCap(144.0f, ss), sub, kLabDescText, ss);

        // 3 v1 Direction A slanted choice cards (Solo-Gold focused / dark-slate unselected).
        static const char* const kTitles[3] = {
            "Reset All to New Defaults (Recommended)",
            "Reset Updated Options Only",
            "Keep Existing Settings"
        };
        int affected = storage::settingsUpgradeAffectedCount();
        char affectedSub[64];
        snprintf(affectedSub, sizeof(affectedSub), "Resets %d changed %s and keeps your other choices.",
                 affected, affected == 1 ? "section" : "sections");
        const char* subs[3] = {
            "Starts fresh with the latest default settings.",
            affectedSub,
            "Keeps compatible choices and removes obsolete options."
        };
        const float cardL43 = inL43 + kFibPad;
        const float cardR43 = inR43 - kFibPad;
        const float cardH = 48.0f;
        float textX = cx(cardL43 + 34.0f);
        float textMaxW = w(348.0f);
        for (int i = 0; i < 3; i++)
        {
            float y = 169.0f + i * (cardH + kFibGap);
            bool sel = i == s_cursor;
            if (sel)
            {
                drawSlantBar(cardL43, cardR43, y, cardH, false, kLabGoldTop, kLabGoldBot, kLabGoldEdge);
                drawDiagStripes(cx(cardR43 - 10.0f), y + kFibHair, y + cardH - kFibHair,
                                w((cardH - 2.0f * kFibHair) * slantTan), w(4.5f), w(3.5f), kLabStripeCol);
                float iconX = cx(cardL43 + cardH * slantTan * 0.55f + 2.0f);
                drawCursorTri(iconX + 1.0f * xs, y + 24.0f, 9.0f);
            }
            else
            {
                drawSlantBar(cardL43, cardR43, y, cardH, false, kLabRowSlateTop, kLabRowSlateBot, kLabRowSlateEdge);
            }
            fitText(textX, y + kFibGap, kTitles[i], sel ? kLabGoldInk : kText, kTypeBody, textMaxW);
            fitText(textX, y + 27.0f, subs[i], sel ? kLabGoldInkMuted : kLabMutedText, kTypeMeta, textMaxW);
        }

        // Unified glossy vector footer emblems (neutral silver D-Pad + green glossy A).
        static const gfx::Rgba kFootCols[2][3] = {
            {kLabBtnLightTop, kLabBtnLightBot, kLabBtnDarkText},
            {kLabBtnATop, kLabBtnABot, kLabActiveTabText}
        };
        const char* glyphs[2] = {"+", s_ctrlType == gfPadType::WIIMOTE ? "2" : "A"};
        const char* labels[2] = {"Choose", "Confirm & Save"};
        float fx = cx(inL43 + kFibPad);
        float footMid = 351.0f + kFibBandH * 0.5f;
        float ls = scaleForCap(kTypeMeta);
        float gs = scaleForCap(kTypeMeta);
        float iw = gw(16.0f);
        for (int i = 0; i < 2; i++)
        {
            drawGlossyPillBadge(fx, footMid - 8.0f, iw, 16.0f, kFootCols[i][0], kFootCols[i][1]);
            float gwid = gfx::textWidth(glyphs[i], gs);
            gfx::text(fx + (iw - gwid) * 0.5f, textYForCap(footMid - kTypeMeta * 0.5f, gs), glyphs[i],
                      kFootCols[i][2], gs);
            fx = gfx::text(fx + iw + gw(kFibTight), textYForCap(footMid - kTypeMeta * 0.5f, ls), labels[i],
                           kLabMutedText, ls)
                 + gw(kFibPad + kFibGap);
        }

        // Subtle 2px dark inner top/bottom bevel matching menu.cpp.
        gfx::fillRect(bodyX, inTop, bodyW, kFibHair, kLabDivider);
        gfx::fillRect(bodyX, inBot - kFibHair, bodyW, kFibHair, kLabBevelDark);
    }
}
}
