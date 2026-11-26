#pragma once

#include "main/gfx/draw.h"
#include "main/gfx/text.h"

// Shared native P+ / Brawl UI styling in Brawl's 640x480 2D coordinate space.
// Header-only so both match and menu modules share it.
// On 16:9 the display widens the 640x480 picture, so horizontal positions pass
// through cx(), lx(), or rx() and widths through w() to preserve 4:3 aspect.
namespace tm {
namespace ui {
namespace style {
    inline float cx(float x)
    {
        return 320.0f + (x - 320.0f) * gfx::xScale();
    }

    inline float lx(float x)
    {
        return x * gfx::xScale();
    }

    inline float rx(float x)
    {
        return 640.0f - (640.0f - x) * gfx::xScale();
    }

    inline float w(float width)
    {
        return width * gfx::xScale();
    }

    // System font metrics at scale 1: capital letters are kCapHeight tall and
    // begin kCapOffset below the y passed to gfx::text.
    static const float kCapHeight = 15.0f;
    static const float kCapOffset = 5.0f;

    inline float scaleForCap(float cap)
    {
        return cap / kCapHeight;
    }

    // Returns the y coordinate for gfx::text so capitals start at `capTop`.
    inline float textYForCap(float capTop, float scale)
    {
        return capTop - kCapOffset * scale;
    }

    // Brawl chunky 32x32 display font (0x80497DA4) metrics at scale 1:
    // capitals span kDisplayCapHeight starting kDisplayCapOffset below y.
    static const float kDisplayCapHeight = 27.0f;
    static const float kDisplayCapOffset = 2.0f;

    inline float displayScaleForCap(float cap)
    {
        return cap / kDisplayCapHeight;
    }

    // Returns the y coordinate for gfx::displayText so capitals start at `capTop`.
    inline float displayYForCap(float capTop, float scale)
    {
        return capTop - kDisplayCapOffset * scale;
    }

    // Native font rendering without artificial stretching or double-strike.
    inline float boldWidth(const char* str, float scale)
    {
        return gfx::textWidth(str, scale);
    }

    inline void boldText(float x, float y, const char* str, gfx::Rgba color, float scaleX, float scaleY)
    {
        gfx::text(x, y, str, color, scaleX, scaleY);
    }

    // ---- Neutral dark-slate palette (shared by CSS browser & drill list) ----
    static const gfx::Rgba kBorder = 0x4A5568FF;
    static const gfx::Rgba kTitleBand = 0x12171FE0;
    static const gfx::Rgba kRowsBand = 0x161D27D8;
    static const gfx::Rgba kFooterBand = 0x10151CE0;
    static const gfx::Rgba kRowBox = 0x222C3AD0;
    static const gfx::Rgba kRowBoxOn = 0x256D4AD8;
    static const gfx::Rgba kHighlight = 0x3B82F648;
    static const gfx::Rgba kScrollTrack = 0x64748BFF;
    static const gfx::Rgba kScrollThumb = 0x60A5FAC0;
    static const gfx::Rgba kText = 0xF1F5F9FF;
    static const gfx::Rgba kTextDisabled = 0x7A8699FF;
    static const gfx::Rgba kInfoFill = 0x121822CC;

    // ---- Native P+ in-match Lab Menu palette (v1 Direction A smoked charcoal) ----
    static const gfx::Rgba kLabBorderColor = 0x06080DFF;
    static const gfx::Rgba kLabBevelLight = 0x2A3544A0;
    static const gfx::Rgba kLabBevelDark = 0x05070BFF;
    static const gfx::Rgba kLabBaseBg = 0x10151CE8;
    static const gfx::Rgba kLabHeaderTop = 0x151B24F2;
    static const gfx::Rgba kLabHeaderBot = 0x0E1219F2;
    static const gfx::Rgba kLabRailTop = 0x11161EF2;
    static const gfx::Rgba kLabRailBot = 0x0B0E14F2;
    static const gfx::Rgba kLabBaseTop = 0x141A22E6;
    static const gfx::Rgba kLabBaseBot = 0x0D1117E6;
    static const gfx::Rgba kLabDescTopCol = 0x0F141BF5;
    static const gfx::Rgba kLabDescBotCol = 0x090C11F5;
    static const gfx::Rgba kLabFooterTopCol = 0x0B0E14FF;
    static const gfx::Rgba kLabFooterBotCol = 0x05070BFF;
    static const gfx::Rgba kLabDivider = 0x26303EFF;
    static const gfx::Rgba kLabAccent = 0x2DB3A1FF;
    static const gfx::Rgba kLabTealTop = 0x2DB3A1FF;
    static const gfx::Rgba kLabTealBot = 0x209485FF;
    static const gfx::Rgba kLabTealSheen = 0x7CE4D670;
    static const gfx::Rgba kLabTealEdge = 0x125B52FF;
    static const gfx::Rgba kLabActiveTabText = 0x061413FF;
    static const gfx::Rgba kLabTabTop = 0x242C38FF;
    static const gfx::Rgba kLabTabBot = 0x181E27FF;
    static const gfx::Rgba kLabTabInactiveText = 0xC0CCD6FF;
    static const gfx::Rgba kLabMutedText = 0x9AA7B4FF;
    static const gfx::Rgba kLabFocusGold = 0xF4C830FF;
    static const gfx::Rgba kLabGoldTop = 0xF7CE38FF;
    static const gfx::Rgba kLabGoldBot = 0xDDA914FF;
    static const gfx::Rgba kLabGoldSheen = 0xFFE98A80;
    static const gfx::Rgba kLabGoldEdge = 0x8A6408FF;
    static const gfx::Rgba kLabGoldInk = 0x0A0E14FF;
    static const gfx::Rgba kLabGoldInkMuted = 0x524214FF;
    static const gfx::Rgba kLabRowSlateTop = 0x2A323EE8;
    static const gfx::Rgba kLabRowSlateBot = 0x212832E8;
    static const gfx::Rgba kLabRowSlateEdge = 0x10141BE8;
    static const gfx::Rgba kLabStripeCol = 0x5A6674E0;
    static const gfx::Rgba kLabPillBorder = 0x080C12FF;
    static const gfx::Rgba kLabPillFocusTop = 0xF6F9FCFF;
    static const gfx::Rgba kLabPillFocusBot = 0xE2E8F0FF;
    static const gfx::Rgba kLabPillOnTop = 0x24AA48FF;
    static const gfx::Rgba kLabPillOnBot = 0x167830FF;
    static const gfx::Rgba kLabPillOnMutedTop = 0x1A4C28B0;
    static const gfx::Rgba kLabPillOnMutedBot = 0x11311AB0;
    static const gfx::Rgba kLabPillOffTop = 0x1A2029EE;
    static const gfx::Rgba kLabPillOffBot = 0x131820EE;
    static const gfx::Rgba kLabPillOffMutedTop = 0x151A22B0;
    static const gfx::Rgba kLabPillOffMutedBot = 0x0F131AB0;
    static const gfx::Rgba kLabPillOnSheen = 0xFFFFFF38;
    static const gfx::Rgba kLabPillOffSheen = 0x50607430;
    static const gfx::Rgba kLabDescText = 0xD2DCE4FF;
    static const gfx::Rgba kLabTitleTop = 0xFFFFFFFF;
    static const gfx::Rgba kLabTitleBot = 0xC6DCE6FF;
    static const gfx::Rgba kLabTitleEdge = 0x000000C0;
    static const gfx::Rgba kLabBtnATop = 0x3ED476FF;
    static const gfx::Rgba kLabBtnABot = 0x1D8A44FF;
    static const gfx::Rgba kLabBtnBTop = 0xF0564CFF;
    static const gfx::Rgba kLabBtnBBot = 0xB22620FF;
    static const gfx::Rgba kLabBtnZTop = 0x6E64D8FF;
    static const gfx::Rgba kLabBtnZBot = 0x3D3598FF;
    static const gfx::Rgba kLabBtnLightTop = 0xF4F7FAFF;
    static const gfx::Rgba kLabBtnLightBot = 0xB6C2CCFF;
    static const gfx::Rgba kLabBtnDarkText = 0x081217FF;
    static const gfx::Rgba kLabWhiteText = 0xFFFFFFFF;

    // ---- Legacy menu panel geometry (used by CSS browser & drill list) ----
    static const float kPanelLeft = 48.0f;
    static const float kPanelRight = 592.0f;
    static const float kPanelTop = 44.0f;
    static const float kPanelBottom = 428.0f;
    static const float kBorderX = 2.0f;
    static const float kBorderY = 2.0f;
    static const float kTitleBandBottom = 88.0f;
    static const float kRowsBandBottom = 354.0f;

    static const float kTitleX = 72.0f;
    static const float kTitleCapTop = 58.0f;
    static const float kTitleCap = 20.0f;

    // Browser and CSS launcher use kVisibleRows (10); in-match Lab menu uses
    // kLabVisibleRows (11), or kLabSubVisibleRows (10) in Top Tabs subpages.
    enum {
        kVisibleRows = 10,
        kLabVisibleRows = 11,
        kLabSubVisibleRows = 10,
        kScrollMargin = 1,
        kFooterLines = 4
    };

    // ---- In-match Lab Menu geometry (inside 5% CRT safe area: x 32..608, y 24..456;
    // bottom at 432 leaves a 24 px strip at 432..456 for storage notices below the menu) ----
    static const float kLabLeft = 36.0f;
    static const float kLabRight = 604.0f;
    static const float kLabTop = 26.0f;
    static const float kLabBottom = 432.0f;
    static const float kLabBorder = 3.0f;
    static const float kLabInnerLeft = 39.0f;
    static const float kLabInnerRight = 601.0f;
    static const float kLabInnerTop = 29.0f;
    static const float kLabInnerBottom = 429.0f;

    static const float kLabDescTop = 373.0f;
    static const float kLabFooterTop = 400.0f;
    static const float kLabDescCap = 9.5f;
    static const float kLabFooterCap = 9.0f;
    static const float kLabRowCap = 10.5f;
    static const float kLabRowPitch = 28.0f;
    static const float kLabRowH = 23.0f;
    static const float kLabPillH = 21.0f;
    static const float kLabScrollLeft = 586.0f;
    static const float kLabScrollW = 8.0f;

    // Category Rail geometry.
    static const float kRailHeaderBottom = 56.0f;
    static const float kRailBodyTop = 58.0f;
    static const float kRailRight = 196.0f;
    static const float kRailPaneLeft = 197.0f;
    static const float kRailRowTop = 63.0f;
    static const float kRailScrollH = 303.0f;
    static const float kRailHlLeft = 200.0f;
    static const float kRailHlRight = 581.0f;
    static const float kRailNameX = 216.0f;
    static const float kRailNameMaxW = 190.0f;
    static const float kRailPillLeft = 410.0f;
    static const float kRailPillRight = 577.0f;
    static const float kRailValCenter = 493.5f;
    static const float kRailValMaxW = 135.0f;
    static const float kRailItemTop = 63.0f;
    static const float kRailItemPitch = 28.0f;
    static const float kRailItemH = 24.0f;
    static const float kRailUtilDivY = 262.0f;
    static const float kRailUtilTop = 270.0f;
    static const float kRailTextX = 56.0f;
    static const float kRailTextMaxW = 132.0f;
    static const float kRailCap = 9.0f;

    // Top L/R Tabs geometry.
    static const float kTabsBarBottom = 58.0f;
    static const float kTabsBodyTop = 60.0f;
    static const float kTabsCrumbBottom = 80.0f;
    static const float kTabsRowTop = 64.0f;
    static const float kTabsSubRowTop = 85.0f;
    static const float kTabsScrollH = 303.0f;
    static const float kTabsSubScrollH = 275.0f;
    static const float kTabsHlLeft = 45.0f;
    static const float kTabsHlRight = 579.0f;
    static const float kTabsNameX = 62.0f;
    static const float kTabsNameMaxW = 286.0f;
    static const float kTabsPillLeft = 356.0f;
    static const float kTabsPillRight = 574.0f;
    static const float kTabsValCenter = 465.0f;
    static const float kTabsValMaxW = 182.0f;
    static const float kTabsCap = 8.5f;
    static const float kRowTop = 98.0f;
    static const float kRowPitch = 25.0f;
    static const float kRowBoxH = 21.0f;
    static const float kNameX = 72.0f;
    static const float kRowCap = 11.5f;
    static const float kNameMaxW = 248.0f;
    static const float kValueCentre = 446.0f;
    static const float kValueMaxW = 192.0f;
    static const float kRowBoxLeft = 344.0f;
    static const float kRowBoxRight = 548.0f;
    static const float kHighlightLeft = 64.0f;
    static const float kHighlightRight = 554.0f;
    static const float kScrollLeft = 566.0f;
    static const float kScrollW = 10.0f;
    static const float kScrollTop = 98.0f;
    static const float kScrollH = 246.0f;

    static const float kFooterX = 72.0f;
    static const float kFooterCapTop = 362.0f;
    static const float kFooterPitch = 15.0f;
    static const float kFooterMaxW = 476.0f;

    // Key repeat timings in frames; kRepeatFast while R is held.
    enum { kRepeatStart = 18, kRepeatEvery = 4, kRepeatFast = 2 };

    // Watermark capital height (placed in hud_layout.h).
    static const float kWatermarkCap = 9.0f;

    // 4-strip opaque outer frame border around a translucent 4:3-proportioned panel.
    void drawFrameBorder(float left43, float right43, float topY, float botY, float border, gfx::Rgba color);

    // Draws the legacy panel layout (recordings browser). When `withFooter` is
    // false, the rows band fills the full height down to the bottom border.
    void drawMenuPanel(bool withFooter = false);

    // CRT-safe metallic bevel bar: 2-stop vertical gradient with 2px top sheen
    // and 2px bottom shadow (zero 1px horizontal lines for flicker-free 480i).
    void drawBevelBar(float x, float y, float width, float height, gfx::Rgba topCol, gfx::Rgba botCol,
                      gfx::Rgba sheenCol, gfx::Rgba edgeCol);

    // Stock P+ ~24 deg slanted vertical-gradient quad in 2D screen coordinates.
    void drawSlantGradV(float x0Top, float x0Bot, float x1Top, float x1Bot, float y0, float y1, gfx::Rgba topCol,
                        gfx::Rgba botCol);

    // Slanted v1 Direction A bar in 4:3 coordinates with optional 2px border frame.
    void drawSlantBar(float x043, float x143, float y, float height, bool flushLeft, gfx::Rgba topCol,
                      gfx::Rgba botCol, gfx::Rgba edgeCol);

    // Slanted v1 Direction A value pill (On green, focused light, or dark slate).
    void drawValuePill(float left43, float right43, float y, float rowH, bool isOn, bool on, bool selected);

    // Twin P+ "//" diagonal stripes inside a slanted bar ending at `rightTopX`.
    void drawDiagStripes(float rightTopX, float y0, float y1, float slantPx, float stripeW, float gap,
                         gfx::Rgba color);

    // Fallback Brawl red cursor triangle pointing right (black outer frame +
    // crisp crimson fill + upper specular highlight).
    void drawCursorTri(float x, float cy, float size);

    // Stadium / rounded-pill silhouette using aspect-corrected endcap ellipses.
    void fillStadium(float x, float y, float width, float height, gfx::Rgba color);

    // Sculpted glossy rounded-pill GX button emblem (black rim + dome + specular).
    // When width <= height * xScale(), fillStadium renders a true circular dome.
    void drawGlossyPillBadge(float x, float y, float width, float height, gfx::Rgba topCol, gfx::Rgba botCol);

    // Sculpted glossy circular GX button emblem (black rim + dome + specular).
    inline void drawGlossyRoundBadge(float cxPos, float cyPos, float radius, gfx::Rgba topCol, gfx::Rgba botCol)
    {
        float rx = radius * gfx::xScale();
        drawGlossyPillBadge(cxPos - rx, cyPos - radius, 2.0f * rx, 2.0f * radius, topCol, botCol);
    }

    void drawScrollBar(int count, int scroll, int visible = kVisibleRows, float height = kScrollH);
}
}
}
