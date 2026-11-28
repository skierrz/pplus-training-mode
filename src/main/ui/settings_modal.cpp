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
    static ModalAnim s_anim;

    void settingsModalAttach()
    {
        s_cursor = 0;
        s_ctrlType = 0;
        s_waitNeutral = 1;
        s_dirHeld = 0;
        modalAnimReset(s_anim);
        if (storage::settingsUpgradePending())
        {
            modalAnimOpen(s_anim);
        }
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
            modalAnimClose(s_anim);
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
        bool pending = storage::settingsUpgradePending();
        if ((!pending && s_anim.exit == 0) || gfx::lineHeight(scaleForCap(10.5f)) <= 0.0f)
        {
            return;
        }

        float alpha = 1.0f;
        float dy = modalAnimStep(s_anim, alpha);

        // Smoked-charcoal v1 Direction A upgrade modal (5% CRT safe, >= 2px scanlines).
        static const ModalFrame kFrame = {
            96.0f, 544.0f, 92.0f, 388.0f, 129.0f, 0.0f, 349.0f, 446.0f, kLabModalBackdropHeavy
        };
        const float inL43 = kFrame.left43 + kLabBorder;
        const float inR43 = kFrame.right43 - kLabBorder;
        const float inTop = kFrame.topY + dy + kLabBorder;
        if (!pending)
        {
            gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, scaleAlpha(kLabModalBackdropHeavy, alpha));
            return;
        }
        drawModalFrame(kFrame, "Update Saved Settings", dy, alpha);

        float ss = scaleForCap(kTypeMeta);
        drawFitText(320.0f, textYForCap(144.0f + dy, ss), "Settings from an older build were found on SD.",
                    kLabDescText, ss, w(384.0f), true);

        // 3 v1 Direction A slanted choice cards (Solo-Gold focused / dark-slate unselected).
        static const char* const kTitles[3] = {
            "Reset All to New Defaults (Recommended)",
            "Reset Updated Options Only",
            "Keep Existing Settings"
        };
        int affected = storage::settingsUpgradeAffectedCount();
        char affectedSub[64];
        const char* affectedText = "No section layouts changed; keeps your saved choices.";
        if (affected > 0)
        {
            snprintf(affectedSub, sizeof(affectedSub), "Resets %d changed %s and keeps your other choices.",
                     affected, affected == 1 ? "section" : "sections");
            affectedText = affectedSub;
        }
        const char* subs[3] = {
            "Starts fresh with the latest default settings.",
            affectedText,
            "Keeps compatible choices and removes obsolete options."
        };
        const float cardL43 = inL43 + kFibPad;
        const float cardR43 = inR43 - kFibPad;
        const float cardH = 48.0f;
        const float cardStep = cardH + kFibGap;
        float textX = cx(cardL43 + 34.0f);
        float textMaxW = w(348.0f);
        float barY = modalAnimCursor(s_anim, 169.0f + s_cursor * cardStep) + dy;
        for (int i = 0; i < 3; i++)
        {
            drawSlantBar(cardL43, cardR43, 169.0f + i * cardStep + dy, cardH, false, kLabRowSlateTop, kLabRowSlateBot,
                         kLabRowSlateEdge);
        }
        drawGoldBar(cardL43, cardR43, barY, cardH, 9.0f, false, cardR43 - 10.0f);
        for (int i = 0; i < 3; i++)
        {
            float y = 169.0f + i * cardStep + dy;
            bool sel = i == s_cursor;
            fitText(textX, y + kFibGap, kTitles[i], sel ? kLabGoldInk : kText, kTypeBody, textMaxW);
            fitText(textX, y + 27.0f, subs[i], sel ? kLabGoldInkMuted : kLabMutedText, kTypeMeta, textMaxW);
        }

        // Unified glossy vector footer emblems (neutral silver D-Pad pill + green glossy A/2).
        float fx = cx(inL43 + kFibPad);
        float footMid = 351.0f + dy + kFibBandH * 0.5f;
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
