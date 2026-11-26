#include "main/ui/settings_modal.h"

#include <cstdio>
#include <gf/gf_pad_status.h>

#include "main/extra.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/storage/settings_file.h"
#include "main/ui/menu.h"
#include "main/ui/style.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_bits.h"
#include "shared/ui_icons.h"

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

        // Dim scrim + Brawl/P+ double-framed dark chrome dialog (5% CRT safe).
        gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, 0x050C10C8u);
        float xs = gfx::xScale();
        float left = 320.0f - 224.0f * xs;
        float panelW = 448.0f * xs;
        float bx = 2.0f * xs;
        gfx::fillRect(left, 92.0f, panelW, 296.0f, kLabBorderColor);
        gfx::fillRect(left + bx, 94.0f, panelW - 2.0f * bx, 292.0f, kLabAccent);

        static const struct {
            float y, h;
            gfx::Rgba top, bot;
        } kBands[5] = {
            {96.0f,  42.0f,  kLabHeaderTop,    kLabHeaderBot},
            {138.0f, 2.0f,   kLabDivider,      kLabDivider},
            {140.0f, 210.0f, kLabBaseTop,      kLabBaseBot},
            {350.0f, 2.0f,   kLabDivider,      kLabDivider},
            {352.0f, 32.0f,  kLabFooterTopCol, kLabFooterBotCol}
        };
        float bodyX = left + 2.0f * bx;
        float bodyW = panelW - 4.0f * bx;
        for (int i = 0; i < 5; i++)
        {
            gfx::fillGradV(bodyX, kBands[i].y, bodyW, kBands[i].h, kBands[i].top, kBands[i].bot);
        }

        // Brawl Display header + subtitle.
        float ts = displayScaleForCap(13.0f);
        const char* title = "SETTINGS UPDATED";
        gfx::displayText(320.0f - gfx::displayTextWidth(title, ts) * 0.5f, displayYForCap(110.0f, ts), title,
                         kLabTitleTop, kLabTitleBot, ts, ts, 1.2f, kLabTitleEdge);
        float ss = scaleForCap(8.8f);
        const char* sub = "Older v1 settings found on SD. Choose how to update:";
        gfx::text(320.0f - gfx::textWidth(sub, ss) * 0.5f, textYForCap(152.0f, ss), sub, kLabMutedText, ss);

        // 3 metallic-bevel choices (zero generic left-accent bars).
        static const char* const kTitles[3] = {
            "Reset All Settings (Recommended)",
            "Reset Affected Settings",
            "Keep My Settings"
        };
        char affectedSub[48];
        snprintf(affectedSub, sizeof(affectedSub), "Reset %d changed area(s); keep the rest.",
                 storage::settingsUpgradeAffectedCount());
        const char* subs[3] = {
            "Use fresh v2 defaults for every option.",
            affectedSub,
            "Keep compatible values and trim old fields."
        };
        float cardX = left + 18.0f * xs;
        float cardW = 412.0f * xs;
        float textX = left + 40.0f * xs;
        float textMaxW = 380.0f * xs;
        for (int i = 0; i < 3; i++)
        {
            float y = 176.0f + i * 56.0f;
            bool sel = i == s_cursor;
            gfx::fillGradV(cardX, y, cardW, 48.0f, sel ? kLabGoldTop : 0x11242EE0u, sel ? kLabGoldBot : 0x0A161CE0u);
            gfx::fillRect(cardX, y, cardW, 2.0f, sel ? kLabGoldSheen : 0x3A687A30u);
            gfx::fillRect(cardX, y + 46.0f, cardW, 2.0f, sel ? kLabGoldEdge : kLabBevelDark);
            if (sel && !main::extraDrawUiIcon(kIconCursorRight, cardX + 5.0f * xs, y + 18.0f, 12.0f * xs, 12.0f,
                                              0xFFFFFFFFu))
            {
                drawCursorTri(cardX + 6.0f * xs, y + 24.0f, 9.0f);
            }
            fitText(textX, y + 9.0f, kTitles[i], sel ? kLabGoldInk : kText, 10.5f, textMaxW);
            fitText(textX, y + 28.0f, subs[i], sel ? 0x2B2100FFu : kLabMutedText, 8.5f, textMaxW);
        }

        // Native Brawl footer emblems (glossy GX pill fallback when icons are absent).
        static const u8 kOkIcons[4] = {kIconGcA, kIconClA, kIconWii2, kIconWiiA};
        static const gfx::Rgba kFootCols[2][2] = {{kLabBtnZTop, kLabBtnZBot}, {kLabBtnATop, kLabBtnABot}};
        u8 icons[2] = {static_cast<u8>(s_ctrlType == gfPadType::GCC ? kIconGcDpad : kIconWiiDpad), kOkIcons[s_ctrlType]};
        const char* glyphs[2] = {"+", s_ctrlType == gfPadType::WIIMOTE ? "2" : "A"};
        const char* labels[2] = {"Choose", "Confirm & Save v2"};
        float fx = left + 22.0f * xs;
        float ls = scaleForCap(9.5f);
        float gs = scaleForCap(8.5f);
        float iw = 16.0f * xs;
        for (int i = 0; i < 2; i++)
        {
            if (!main::extraDrawUiIcon(icons[i], fx, 360.0f, iw, 16.0f, 0xFFFFFFFFu))
            {
                drawGlossyPillBadge(fx, 360.0f, iw, 16.0f, kFootCols[i][0], kFootCols[i][1]);
                float gw = gfx::textWidth(glyphs[i], gs);
                gfx::text(fx + (iw - gw) * 0.5f, textYForCap(363.75f, gs), glyphs[i], kLabWhiteText, gs);
            }
            fx = gfx::text(fx + 22.0f * xs, textYForCap(363.25f, ls), labels[i], kText, ls) + 26.0f * xs;
        }
    }
}
}
