#pragma once

#include <gf/gf_pad_status.h>
#include <types.h>
#ifdef __MWERKS__
#include <GX/GXGeometry.h>
#include <GX/GXHardware.h>
#include <GX/GXTransform.h>
#endif

#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/ui/style.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_bits.h"

// Native Wii GX scrollable Credits modal shared by extra (in-match Lab Menu)
// and menu (CSS Boot Menu). Header-only, C++03, zero heap, compact PPC size.
// String literals are defined via preprocessor macros to keep shared copy in
// one place and avoid duplicate literal tables across host scans.

#define TM_CREDITS_RIBBON      "CREDITS"
#define TM_CREDITS_TITLE       "Project+ Training Mode Mod (P+TMM)"
#define TM_CREDITS_SUBTITLE    "v1.0.1-pre4 | discord.gg/WHNtJd2KAJ"
#define TM_CREDITS_LICENSE     "GPL-3.0-or-later | github.com/skierrz/pplus-training-mode"
#define TM_CREDITS_HINT_SCROLL "D-Pad/Stick: Scroll"
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

    // Modal frame layout in 4:3 coordinates (inside 5% CRT safe area 32..608 x 24..456).
    static const float kCreditsInL43 = 55.0f;
    static const float kCreditsInR43 = 585.0f;
    static const float kCreditsViewTopY = 88.0f;
    static const float kCreditsViewBotY = 384.0f;
    static const float kCreditsCardL43 = 67.0f;
    static const float kCreditsCardW43 = 494.0f;
    static const float kCreditsTextX43 = 78.0f;
    static const float kCreditsTextMaxW43 = 474.0f;
    static const float kCreditsScrollL43 = 569.0f;
    static const float kCreditsScrollW43 = 8.0f;

    static const float kCreditsCardH1 = 41.0f;
    static const float kCreditsCardH2 = 54.0f;
    static const float kCreditsCardGap = 6.0f;
    static const float kCreditsTopPad = 8.0f;
    static const float kCreditsBotPad = 10.0f;
    // Precomputed content height: 8 + 6*41 + 3*54 + 8*6 + 10 = 474.0 px; viewH = 296.0 px.
    static const float kCreditsContentH = 474.0f;
    static const float kCreditsMaxScroll = 178.0f;

    static const float kCreditsTitleCap = 10.5f;
    static const float kCreditsSubCap = 9.0f;
    static const float kCreditsNameCap = 10.5f;
    static const float kCreditsRoleCap = 9.0f;
    static const float kCreditsLicCap = 9.0f;
    static const float kCreditsHintCap = 9.0f;

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
#if defined(TM_MODULE_MENU) || !defined(__MWERKS__)
    static inline gfx::Rgba creditsAlpha(gfx::Rgba c, float a) { return ui::style::scaleAlpha(c, a); }
    static inline float creditsSnap(float v) { return ui::style::snapPx(v); }
    static inline float creditsChase(float cur, float target, float a) { return ui::style::chase60(cur, target, a); }
    typedef gfx::ScissorState CreditsScissor;
    static inline bool creditsPushScissor(float x0, float y0, float x1, float y1, CreditsScissor& saved)
    {
        return gfx::pushScissor(x0, y0, x1, y1, saved);
    }
    static inline void creditsPopScissor(const CreditsScissor& saved) { gfx::popScissor(saved); }
#else
    static gfx::Rgba creditsAlpha(gfx::Rgba c, float a)
    {
        if (a >= 0.999f) return c;
        if (a <= 0.0f) return c & 0xFFFFFF00u;
        u32 na = static_cast<u32>(static_cast<float>(c & 0xFFu) * a + 0.5f);
        if (na > 255u) na = 255u;
        return (c & 0xFFFFFF00u) | na;
    }

    static float creditsSnap(float v)
    {
        return v >= 0.0f ? static_cast<float>(static_cast<int>(v + 0.5f))
                         : static_cast<float>(static_cast<int>(v - 0.5f));
    }

    static float creditsChase(float cur, float target, float alpha)
    {
        float d = target - cur;
        return (d > -0.2f && d < 0.2f) ? target : (cur + d * alpha);
    }

    struct CreditsScissor {
        u32 x, y, w, h;
    };

    static bool creditsPushScissor(float x0, float y0, float x1, float y1, CreditsScissor& saved)
    {
        GXGetScissor(&saved.x, &saved.y, &saved.w, &saved.h);
        f32 vp[6];
        GXGetViewportv(vp);
        float vx = vp[2] > 0.0f ? vp[0] : 0.0f;
        float vy = vp[3] > 0.0f ? vp[1] : 0.0f;
        float vw = vp[2] > 0.0f ? vp[2] : 640.0f;
        float vh = vp[3] > 0.0f ? vp[3] : 480.0f;
        float fx0 = vx + x0 * (vw / 640.0f);
        float fy0 = vy + y0 * (vh / 480.0f);
        float fx1 = vx + x1 * (vw / 640.0f);
        float fy1 = vy + y1 * (vh / 480.0f);
        int px0 = fx0 <= 0.0f ? 0 : static_cast<int>(fx0 + 0.5f);
        int py0 = fy0 <= 0.0f ? 0 : static_cast<int>(fy0 + 0.5f);
        int px1 = fx1 <= 0.0f ? 0 : static_cast<int>(fx1 + 0.5f);
        int py1 = fy1 <= 0.0f ? 0 : static_cast<int>(fy1 + 0.5f);
        int sx0 = static_cast<int>(saved.x);
        int sy0 = static_cast<int>(saved.y);
        int sx1 = sx0 + static_cast<int>(saved.w);
        int sy1 = sy0 + static_cast<int>(saved.h);
        if (px0 < sx0) px0 = sx0;
        if (py0 < sy0) py0 = sy0;
        if (px1 > sx1) px1 = sx1;
        if (py1 > sy1) py1 = sy1;
        if (px1 <= px0 || py1 <= py0) return false;
        GXSetScissor(static_cast<u32>(px0), static_cast<u32>(py0), static_cast<u32>(px1 - px0),
                     static_cast<u32>(py1 - py0));
        return true;
    }

    static void creditsPopScissor(const CreditsScissor& saved)
    {
        GXSetScissor(saved.x, saved.y, saved.w, saved.h);
    }
#endif

    static void creditsGrad(float x43, float y, float w43, float h, gfx::Rgba topCol, gfx::Rgba botCol, float a)
    {
        gfx::fillGradV(ui::style::cx(x43), y, ui::style::w(w43), h, creditsAlpha(topCol, a), creditsAlpha(botCol, a));
    }

    static void creditsFitText(float x43, float capTop, const char* str, gfx::Rgba col, float cap, float maxW43,
                               float a, bool rightAlign)
    {
        float scale = ui::style::scaleForCap(cap);
        float tw = gfx::textWidth(str, scale);
        float limit = ui::style::w(maxW43);
        float sx = (tw > limit && tw > 0.0f) ? (scale * limit / tw) : scale;
        if (tw > limit) tw = limit;
        float x = ui::style::cx(x43) - (rightAlign ? tw : 0.0f);
        gfx::text(x, ui::style::textYForCap(capTop, scale), str, creditsAlpha(col, a), sx, scale);
    }

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
        st.visualScroll = creditsChase(st.visualScroll, st.targetScroll, 0.35f);
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

        float alpha = 1.0f;
        float dy = 0.0f;
        if (st.open)
        {
            if (st.enterTick > 0)
            {
                float u = 1.0f - static_cast<float>(9 - st.enterTick) / 8.0f;
                float ease = 1.0f - u * u * u;
                alpha = 0.25f + 0.75f * ease;
                dy = creditsSnap((1.0f - ease) * 6.0f);
                st.enterTick--;
            }
        }
        else if (st.exitTick > 0)
        {
            float u = 1.0f - static_cast<float>(5 - st.exitTick) / 4.0f;
            float ease = 1.0f - u * u * u * u;
            alpha = 1.0f - ease;
            dy = creditsSnap(ease * 4.0f);
            st.exitTick--;
        }
        if (alpha <= 0.01f)
        {
            return;
        }

        using namespace ui::style;
        float pa = alpha >= 0.99f ? 1.0f : (0.25f + 0.75f * alpha);

        // 1. Backdrop + compact table-driven Direction A modal chrome.
        gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, creditsAlpha(kLabModalBackdropHeavy, alpha));
        struct Band {
            float x, y, w, h;
            gfx::Rgba top, bot;
        };
        static const Band kBands[16] = {
            {52.0f,  30.0f, 536.0f,   3.0f, kLabBorderColor,  kLabBorderColor},
            {52.0f, 443.0f, 536.0f,   3.0f, kLabBorderColor,  kLabBorderColor},
            {52.0f,  33.0f,   3.0f, 410.0f, kLabBorderColor,  kLabBorderColor},
            {585.0f, 33.0f,   3.0f, 410.0f, kLabBorderColor,  kLabBorderColor},
            {55.0f,  33.0f, 530.0f,  30.0f, kLabHeaderTop,    kLabHeaderBot},
            {55.0f,  33.0f, 118.0f,  30.0f, kLabTealTop,      kLabTealBot},
            {169.0f, 33.0f,   4.0f,  30.0f, kLabTealSheen,    kLabTealSheen},
            {55.0f,  63.0f, 530.0f,  23.0f, 0x0C1118F2u,      0x0C1118F2u},
            {55.0f,  63.0f,   4.0f,  23.0f, kLabTealTop,      kLabTealTop},
            {55.0f,  86.0f, 530.0f,   2.0f, kLabDivider,      kLabDivider},
            {55.0f,  88.0f, 530.0f, 296.0f, kLabBaseTop,      kLabBaseBot},
            {55.0f, 384.0f, 530.0f,   2.0f, kLabDivider,      kLabDivider},
            {55.0f, 386.0f, 530.0f,  25.0f, kLabDescTopCol,   kLabDescBotCol},
            {55.0f, 411.0f, 530.0f,   2.0f, kLabDivider,      kLabDivider},
            {55.0f, 413.0f, 530.0f,  30.0f, kLabFooterTopCol, kLabFooterBotCol},
            {55.0f, 441.0f, 530.0f,   2.0f, kLabBevelDark,    kLabBevelDark}
        };
        for (int b = 0; b < 16; b++)
        {
            creditsGrad(kBands[b].x, kBands[b].y + dy, kBands[b].w, kBands[b].h, kBands[b].top, kBands[b].bot, pa);
        }

        // 2. Header text (ribbon tag + title + subtitle).
        float ribScale = displayScaleForCap(12.5f);
        gfx::Rgba ribInk = creditsAlpha(kLabActiveTabText, pa);
        gfx::displayText(cx(67.0f), displayYForCap(41.75f + dy, ribScale), TM_CREDITS_RIBBON, ribInk, ribInk,
                         ribScale, ribScale, 0.0f, 0);
        creditsFitText(573.0f, 42.75f + dy, TM_CREDITS_TITLE, kLabWhiteText, kCreditsTitleCap, 392.0f, pa, true);
        creditsFitText(69.0f, 70.0f + dy, TM_CREDITS_SUBTITLE, kLabDescText, kCreditsSubCap, 502.0f, pa, false);

        // 3. Hardware scissor-clipped scrollable credit cards.
        float viewTop = kCreditsViewTopY + dy;
        float viewBot = kCreditsViewBotY + dy;
        float diff = st.targetScroll - st.visualScroll;
        float drawScroll = (diff > -0.25f && diff < 0.25f) ? creditsSnap(st.targetScroll) : st.visualScroll;

        CreditsScissor sc;
        if (creditsPushScissor(cx(kCreditsInL43), viewTop, cx(kCreditsInR43), viewBot, sc))
        {
            const CreditsEntry* entries = creditsEntries();
            // Pass 1: plates + left accent bars; Pass 2: text.
            for (int pass = 0; pass < 2; pass++)
            {
                float y = viewTop + kCreditsTopPad - drawScroll;
                for (int i = 0; i < kCreditsEntryCount; i++)
                {
                    float ch = entries[i].role2 != NULL ? kCreditsCardH2 : kCreditsCardH1;
                    if (y + ch > viewTop - 2.0f && y < viewBot + 2.0f)
                    {
                        if (pass == 0)
                        {
                            creditsGrad(kCreditsCardL43, y, kCreditsCardW43, ch, 0x1A222EE0u, 0x121822E0u, pa);
                            creditsGrad(kCreditsCardL43, y + ch - 2.0f, kCreditsCardW43, 2.0f, 0x080C12E8u,
                                        0x080C12E8u, pa);
                            gfx::Rgba acc = i == 0 ? kLabFocusGold : kLabTealTop;
                            creditsGrad(kCreditsCardL43, y, 4.0f, ch, acc, acc, pa);
                        }
                        else
                        {
                            creditsFitText(kCreditsTextX43, y + 6.0f, entries[i].name,
                                           i == 0 ? kLabFocusGold : kLabWhiteText, kCreditsNameCap,
                                           kCreditsTextMaxW43, pa, false);
                            creditsFitText(kCreditsTextX43, y + 22.0f, entries[i].role1, kLabDescText,
                                           kCreditsRoleCap, kCreditsTextMaxW43, pa, false);
                            if (entries[i].role2 != NULL)
                            {
                                creditsFitText(kCreditsTextX43, y + 35.0f, entries[i].role2, kLabDescText,
                                               kCreditsRoleCap, kCreditsTextMaxW43, pa, false);
                            }
                        }
                    }
                    y += ch + kCreditsCardGap;
                }
            }
            if (drawScroll > 1.0f)
            {
                creditsGrad(55.0f, viewTop, 512.0f, 14.0f, 0x10151CE8u, 0x10151C00u, pa);
            }
            if (drawScroll < kCreditsMaxScroll - 1.0f)
            {
                creditsGrad(55.0f, viewBot - 14.0f, 512.0f, 14.0f, 0x0D111700u, 0x0D1117E8u, pa);
            }
            creditsPopScissor(sc);
        }

        // 4. Right-edge scroll track, proportional thumb, and overflow arrows.
        float trackTop = viewTop + 14.0f;
        float trackH = 268.0f;
        float thumbH = 167.0f;
        float ratio = drawScroll / kCreditsMaxScroll;
        float thumbY = trackTop + (trackH - thumbH) * ratio;
        creditsGrad(kCreditsScrollL43, trackTop, kCreditsScrollW43, trackH, 0x222C3AE0u, 0x222C3AE0u, pa);
        creditsGrad(kCreditsScrollL43, thumbY, kCreditsScrollW43, thumbH, kLabTealTop, kLabTealBot, pa);
        float midX = cx(573.0f);
        float triR = gw(3.5f);
        gfx::Rgba arrCol = creditsAlpha(kLabTealTop, pa);
        if (drawScroll > 1.0f)
        {
            gfx::fillTri(midX, viewTop + 5.0f, midX - triR, viewTop + 10.0f, midX + triR, viewTop + 10.0f, arrCol);
        }
        if (drawScroll < kCreditsMaxScroll - 1.0f)
        {
            gfx::fillTri(midX, viewBot - 5.0f, midX - triR, viewBot - 10.0f, midX + triR, viewBot - 10.0f, arrCol);
        }

        // 5. Footer text: license & repo + controls hint.
        creditsFitText(69.0f, 394.0f + dy, TM_CREDITS_LICENSE, kLabDescText, kCreditsLicCap, 502.0f, pa, false);
        float hintMidY = 428.0f + dy;
        creditsFitText(69.0f, hintMidY - 4.5f, TM_CREDITS_HINT_SCROLL, kLabMutedText, kCreditsHintCap, 240.0f, pa,
                       false);

        const char* closeBtn = st.ctrlType == gfPadType::WIIMOTE ? TM_CREDITS_BTN_1 : TM_CREDITS_BTN_B;
        float hintScale = scaleForCap(kCreditsHintCap);
        float closeW = gfx::textWidth(TM_CREDITS_HINT_CLOSE, hintScale);
        float btnW = gw(16.0f);
        float closeX = cx(571.0f) - closeW;
        float badgeX = closeX - gw(6.0f) - btnW;
        float badgeY = hintMidY - 8.0f;
        float rimX = gw(1.5f);
        gfx::fillRect(badgeX - rimX, badgeY - 1.5f, btnW + 2.0f * rimX, 19.0f, creditsAlpha(kLabBorderColor, pa));
        gfx::fillGradV(badgeX, badgeY, btnW, 16.0f, creditsAlpha(kLabBtnBTop, pa), creditsAlpha(kLabBtnBBot, pa));

        float glyphScale = scaleForCap(kTypeMicro);
        gfx::text(badgeX + (btnW - gfx::textWidth(closeBtn, glyphScale)) * 0.5f,
                  textYForCap(hintMidY - 4.0f, glyphScale), closeBtn, creditsAlpha(kLabWhiteText, pa), glyphScale);
        gfx::text(closeX, textYForCap(hintMidY - 4.5f, hintScale), TM_CREDITS_HINT_CLOSE,
                  creditsAlpha(kLabWhiteText, pa), hintScale);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif
}
}
