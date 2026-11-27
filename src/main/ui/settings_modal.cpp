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
        drawFitText(x, textYForCap(y, s), str, col, s, maxW);
    }

    void settingsModalDraw()
    {
        if (!storage::settingsUpgradePending() || gfx::lineHeight(scaleForCap(10.5f)) <= 0.0f)
        {
            return;
        }

        // Smoked-charcoal v1 Direction A upgrade modal (5% CRT safe, >= 2px scanlines).
        static const ModalFrame kFrame = {
            96.0f, 544.0f, 92.0f, 388.0f, 129.0f, 0.0f, 349.0f, 446.0f, kLabModalBackdropHeavy
        };
        const float inL43 = kFrame.left43 + kLabBorder;
        const float inR43 = kFrame.right43 - kLabBorder;
        const float inTop = kFrame.topY + kLabBorder;
        drawModalFrame(kFrame, "Update Saved Settings");

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
                drawGoldBar(cardL43, cardR43, y, cardH, 9.0f, false, cardR43 - 10.0f);
            }
            else
            {
                drawSlantBar(cardL43, cardR43, y, cardH, false, kLabRowSlateTop, kLabRowSlateBot, kLabRowSlateEdge);
            }
            fitText(textX, y + kFibGap, kTitles[i], sel ? kLabGoldInk : kText, kTypeBody, textMaxW);
            fitText(textX, y + 27.0f, subs[i], sel ? kLabGoldInkMuted : kLabMutedText, kTypeMeta, textMaxW);
        }

        // Unified glossy vector footer emblems (neutral silver D-Pad pill + green glossy A/2).
        float fx = cx(inL43 + kFibPad);
        float footMid = 351.0f + kFibBandH * 0.5f;
        u8 dpadFlags = kBadgeTightGap | kBadgeWidePad;
        u8 btnFlags = kBadgeRound | kBadgeTightGap | kBadgeWidePad;
        fx = drawFooterBadge(fx, footMid, 16.0f, "D-Pad", "Choose", kBtnLight, dpadFlags);
        drawFooterBadge(fx, footMid, 16.0f, s_ctrlType == gfPadType::WIIMOTE ? "2" : "A", "Confirm & Save", kBtnA,
                        btnFlags);

        // Subtle 2px top inner bevel matching existing modal framing.
        gfx::fillRect(cx(inL43), inTop, w(inR43 - inL43), kFibHair, kLabDivider);
    }
}
}
