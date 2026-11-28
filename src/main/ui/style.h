#pragma once

#include "main/gfx/draw.h"
#include "main/gfx/text.h"

// Shared native P+ / Brawl UI styling in Brawl's 640x480 2D coordinate space.
// Header-only so both match and menu modules share it.
// On 16:9 the display widens the 640x480 picture by 4/3: gfx::xScale() (0.75)
// keeps glyphs, circles, and icons aspect-correct via gw(), while menuScale()
// (0.90 on 16:9, 1.0 on 4:3) lets menu panels breathe across ~80% of widescreen
// (x 64..576, well inside 5% CRT safe 32..608) via cx() and w().
namespace tm {
namespace ui {
namespace style {
    inline float menuScale()
    {
        return gfx::widescreen() ? 0.90f : 1.0f;
    }

    inline float cx(float x)
    {
        return 320.0f + (x - 320.0f) * menuScale();
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
        return width * menuScale();
    }

    // Aspect-preserving graphic width for circular badges, icons, and arrows.
    inline float gw(float width)
    {
        return width * gfx::xScale();
    }

    // Pure 60fps GX motion math helpers (out-of-line in style.cpp to save PPC code size).
    float easeOutCubic(float t);
    float easeOutQuart(float t);
    float chase60(float cur, float target, float alpha);
    float snapPx(float v);
    gfx::Rgba scaleAlpha(gfx::Rgba c, float a);

    // Shared 12-byte modal motion state (<= 16 B budget) for 8f enter / 4f exit / chase60 cursor & scroll.
    struct ModalAnim {
        float cursorY;
        float scrollY;
        u8 enter;
        u8 exit;
        u8 hasCursor;
        u8 hasScroll;
    };

    inline void modalAnimReset(ModalAnim& a)
    {
        a.cursorY = 0.0f;
        a.scrollY = 0.0f;
        a.enter = a.exit = a.hasCursor = a.hasScroll = 0;
    }
    inline void modalAnimOpen(ModalAnim& a)
    {
        a.enter = 8;
        a.exit = 0;
        a.hasCursor = 0;
        a.hasScroll = 0;
    }
    inline void modalAnimClose(ModalAnim& a)
    {
        a.enter = 0;
        a.exit = 4;
    }
    float modalAnimStep(ModalAnim& a, float& alphaOut);
    float modalAnimCursor(ModalAnim& a, float targetY);
    float modalAnimScroll(ModalAnim& a, float targetY);

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
    static const gfx::Rgba kLabDivider = 0x344254FF;
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
    static const gfx::Rgba kLabRowSlateEdge = 0x080C12FF;
    static const gfx::Rgba kLabStripeCol = 0x5A6674E0;
    static const gfx::Rgba kLabPillBorder = 0x080C12FF;
    static const gfx::Rgba kLabPillFocusTop = 0xF6F9FCFF;
    static const gfx::Rgba kLabPillFocusBot = 0xE2E8F0FF;
    static const gfx::Rgba kLabPillOnTop = 0x24AA48FF;
    static const gfx::Rgba kLabPillOnBot = 0x167830FF;
    static const gfx::Rgba kLabPillOnMutedTop = 0x1A4C28FF;
    static const gfx::Rgba kLabPillOnMutedBot = 0x11311AFF;
    static const gfx::Rgba kLabPillOffTop = 0x1A2029FF;
    static const gfx::Rgba kLabPillOffBot = 0x131820FF;
    static const gfx::Rgba kLabPillOffMutedTop = 0x151A22FF;
    static const gfx::Rgba kLabPillOffMutedBot = 0x0F131AFF;
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
    static const gfx::Rgba kLabBtnDarkText = 0x051411FF;
    static const gfx::Rgba kLabWhiteText = 0xFFFFFFFF;
    static const gfx::Rgba kLabBackdropDim = 0x070B12B0;
    static const gfx::Rgba kLabModalBackdrop = 0x070B12B8;
    static const gfx::Rgba kLabModalBackdropHeavy = 0x070B12C8;
    static const gfx::Rgba kLabCardTop = 0x111720E8;
    static const gfx::Rgba kLabCardBot = 0x0A0E15E8;
    static const gfx::Rgba kLabCardBorderIn = 0x2A3544FF;
    static const gfx::Rgba kLabRowEdge = 0x26303EFF;
    static const gfx::Rgba kLabConfirmInkOnGold = 0x7A0C00FF;
    static const gfx::Rgba kLabErrorText = 0xFF8C8CFF;
    static const gfx::Rgba kLabAlertFillTop = 0x341016F0;
    static const gfx::Rgba kLabAlertFillBot = 0x20080DF0;
    static const gfx::Rgba kLabBadgeImportTop = 0xD6E0E6FF;
    static const gfx::Rgba kLabBadgeImportBot = 0x8E9EA8FF;
    static const gfx::Rgba kLabBadgeOsdTop = 0x3AC8B8FF;
    static const gfx::Rgba kLabBadgeOsdBot = 0x1C8B7EFF;

    // ---- Shared semantic HUD scrim and feedback tokens (live match + extra overlays) ----
    // Subtle P+-native airy transparency (~42% wash) paired with 4-way outlined text.
    static const gfx::Rgba kSmokedPlateBg = 0x0D131C6Cu;
    static const gfx::Rgba kSmokedPlateEdge = 0x05080DE8u;
    static const gfx::Rgba kSmokedPlateSheen = 0xFFFFFF22u;
    static const gfx::Rgba kSmokedDivider = 0xFFFFFF38u;
    static const gfx::Rgba kHudPlateAlphaBg = 0x1117206Cu;
    static const gfx::Rgba kHudOutlineEdge = 0x04070CE8u;
    static const gfx::Rgba kHudOutlineHeavy = 0x04070CF8u;
    static const gfx::Rgba kHudShadow = 0x000000C8u;
    static const gfx::Rgba kHudTextPrimary = 0xF8FCFFFFu;
    static const gfx::Rgba kHudTextLabel = 0xC6D4E0FFu;
    static const gfx::Rgba kHudTextValue = 0xF4F8FCFFu;
    static const gfx::Rgba kHudTroughBg = 0x070B1298u;
    static const gfx::Rgba kHudCellEmpty = 0x1E2C38B8u;
    static const gfx::Rgba kHudCellSheen = 0xFFFFFF3Cu;
    static const gfx::Rgba kHudGold = 0xFFD300FFu;
    static const gfx::Rgba kHudYellow = 0xFFE24AFFu;
    static const gfx::Rgba kHudGreen = 0x4CE688FFu;
    static const gfx::Rgba kHudWarn = 0xFFB848FFu;
    static const gfx::Rgba kHudRed = 0xFF6658FFu;

    // ---- Shared Fibonacci spacing ladder (F3..F11; 480i anti-twitter floor >= 2 px) ----
    static const float kFibHair = 2.0f;    // F3: horizontal divider rules, anti-twitter floor
    static const float kFibBorder = 3.0f;  // F4: outer frame border, tight micro-gap
    static const float kFibTight = 5.0f;   // F5: inter-row gap, dense footer gap, compact inset
    static const float kFibGap = 8.0f;     // F6: pane/card gutter, scrollbar track width, HUD gap
    static const float kFibPad = 13.0f;    // F7: modal/card inner padding, standard footer item gap
    static const float kFibRowH = 21.0f;   // F8: value pill height, compact row, crumb bar, min thumb
    static const float kFibBandH = 34.0f;  // F9: unified header/footer band height, stat-plate row pitch
    static const float kFibUnit = 55.0f;   // F10: GC controller height, Adaptive CPU plate height
    static const float kFibBlock = 89.0f;  // F11: macro block reference

    // ---- Shared 5-tier typography scale (480i floors: Rodin >= 8.0, Display >= 8.5) ----
    static const float kTypeMicro = 8.0f;    // Rodin: dense footer, version mark, compact HUD hints
    static const float kTypeMeta = 9.0f;     // Rodin / Display: rail items, top tabs, desc, crumbs, labels
    static const float kTypeBody = 10.5f;    // Rodin: Lab rows/pills, Boot Mode list & card body, modal rows
    static const float kTypeHeading = 13.0f; // DisplayFont: Lab header, modal headers, card title, stat values
    static const float kTypeHero = 21.0f;    // DisplayFont: Drill End-Screen hero score (~phi jump from 13.0)

    // ---- Legacy menu panel geometry (used by CSS recordings browser) ----
    static const float kPanelLeft = 48.0f;
    static const float kPanelRight = 592.0f;
    static const float kPanelTop = 44.0f;
    static const float kPanelBottom = 428.0f;
    static const float kBorderX = 2.0f;
    static const float kBorderY = 2.0f;
    static const float kTitleBandBottom = 83.0f;
    static const float kRowsBandBottom = 355.0f;

    static const float kTitleX = 72.0f;
    static const float kTitleCapTop = 58.0f;
    static const float kTitleCap = 20.0f;

    // Browser and Boot Mode Select use kVisibleRows (10); in-match Lab menu uses
    // kLabVisibleRows (10), or kLabSubVisibleRows (9) on subpages with the 21 px crumb bar.
    enum {
        kVisibleRows = 10,
        kLabVisibleRows = 10,
        kLabSubVisibleRows = 9,
        kScrollMargin = 1,
        kFooterLines = 4
    };

    // ---- In-match Lab Menu geometry (inside 5% CRT safe area: x 32..608, y 24..456;
    // bottom at 432 leaves a 24 px strip at 432..456 for storage notices below the menu) ----
    static const float kLabLeft = 36.0f;
    static const float kLabRight = 604.0f;
    static const float kLabTop = 26.0f;
    static const float kLabBottom = 432.0f;
    static const float kLabBorder = kFibBorder;
    static const float kLabInnerLeft = 39.0f;
    static const float kLabInnerRight = 601.0f;
    static const float kLabInnerTop = 29.0f;
    static const float kLabInnerBottom = 429.0f;

    static const float kLabHeaderBottom = 63.0f; // kLabInnerTop + kFibBandH (34)
    static const float kLabBodyTop = 65.0f;      // + kFibHair (2) divider
    static const float kLabCrumbBottom = 86.0f;  // kLabBodyTop + kFibRowH (21)
    static const float kLabSubBodyTop = 88.0f;   // + kFibHair (2) divider
    static const float kLabDescTop = 366.0f;     // 2 px div + 25 px fill (8 + 9 + 8)
    static const float kLabFooterTop = 393.0f;   // 2 px div + 34 px (F9) fill (395..429)
    static const float kLabDescCap = kTypeMeta;
    static const float kLabFooterCap = kTypeMeta;
    static const float kLabRowCap = kTypeBody;
    static const float kLabRowPitch = 29.0f;     // kLabRowH (24) + kFibTight (5) -> 10 rows end at 358 (8 px bottom)
    static const float kLabRowH = 24.0f;
    static const float kLabPillH = kFibRowH;     // 21.0f (F8)
    static const float kLabScrollLeft = 585.0f;  // 8 px (F6) right inset to 601
    static const float kLabScrollW = kFibGap;    // 8.0f (F6)

    // Category Rail geometry (unified 34 px header w/ inline ribbon breadcrumb, symmetric 8 px top/bottom).
    static const float kRailHeaderBottom = kLabHeaderBottom;
    static const float kRailBodyTop = kLabBodyTop;
    static const float kRailRight = 195.0f;      // 39..195 (W = 156) + 2 px div (195..197)
    static const float kRailPaneLeft = 197.0f;   // 197..601 (W = 404)
    static const float kRailRowTop = 73.0f;      // kLabBodyTop (65) + kFibGap (8)
    static const float kRailSubRowTop = 73.0f;   // subpages put breadcrumb on header ribbon (03-mockup)
    static const float kRailScrollH = 285.0f;    // 10 rows: 9 * 29 + 24 (y 73..358, symmetric 8 px bottom)
    static const float kRailSubScrollH = 285.0f; // full 10 rows on Rail subpages
    static const float kRailHlLeft = 202.0f;     // kRailPaneLeft (197) + kFibTight (5)
    static const float kRailHlRight = 580.0f;    // kLabScrollLeft (585) - kFibTight (5)
    static const float kRailNameX = 228.0f;      // crisp 7+ px gap after red cursor triangle (03-mockup)
    static const float kRailNameMaxW = 174.0f;   // 410 - 228 - 8
    static const float kRailPillLeft = 410.0f;
    static const float kRailPillRight = kRailHlRight; // flush with row right edge (no gold overhang)
    static const float kRailValCenter = 495.0f;
    static const float kRailValMaxW = 140.0f;
    static const float kRailItemTop = 73.0f;     // aligns with option row 0
    static const float kRailItemPitch = 28.0f;   // kRailItemH (23) + kFibTight (5)
    static const float kRailItemH = 23.0f;
    static const float kRailUtilDivY = 270.0f;   // centered between 7 categories (end 264) and utilities (278)
    static const float kRailUtilTop = 278.0f;    // 3 utilities end at 357 (symmetric 9 px bottom)
    static const float kRailTextX = 58.0f;       // clears left-rail cursor triangle by 5+ px (03-mockup)
    static const float kRailTextMaxW = 126.0f;
    static const float kRailCap = kTypeBody;     // crisp 10.5 px Rodin system font on rail (03-mockup)

    // Top L/R Tabs geometry (unified 34 px header, 21 px subpage crumb, 5 px gaps).
    static const float kTabsBarBottom = kLabHeaderBottom;
    static const float kTabsBodyTop = kLabBodyTop;
    static const float kTabsCrumbBottom = kLabCrumbBottom;
    static const float kTabsRowTop = kRailRowTop;
    static const float kTabsSubRowTop = 96.0f;   // kLabSubBodyTop (88) + kFibGap (8)
    static const float kTabsScrollH = kRailScrollH;
    static const float kTabsSubScrollH = 256.0f; // 9 rows: 8 * 29 + 24 (y 96..352)
    static const float kTabsHlLeft = 47.0f;      // kLabInnerLeft (39) + kFibGap (8)
    static const float kTabsHlRight = 580.0f;    // kLabScrollLeft (585) - kFibTight (5)
    static const float kTabsNameX = 73.0f;       // crisp 7+ px gap after red cursor triangle
    static const float kTabsNameMaxW = 275.0f;
    static const float kTabsPillLeft = 356.0f;
    static const float kTabsPillRight = kTabsHlRight; // flush with row right edge (no gold overhang)
    static const float kTabsValCenter = 468.0f;
    static const float kTabsValMaxW = 186.0f;
    static const float kTabsCap = kTypeMeta;
    static const float kRowTop = 91.0f;
    static const float kRowPitch = 26.0f;        // kRowBoxH (21) + kFibTight (5)
    static const float kRowBoxH = kFibRowH;      // 21.0f (F8)
    static const float kNameX = 72.0f;
    static const float kRowCap = kTypeBody;      // 10.5f
    static const float kNameMaxW = 248.0f;
    static const float kValueCentre = 446.0f;
    static const float kValueMaxW = 192.0f;
    static const float kRowBoxLeft = 344.0f;
    static const float kRowBoxRight = 556.0f;
    static const float kHighlightLeft = 64.0f;
    static const float kHighlightRight = 554.0f;
    static const float kScrollLeft = 573.0f;     // 589 - 8 inset - 8 track = 573
    static const float kScrollW = kFibGap;       // 8.0f (F6)
    static const float kScrollTop = 91.0f;
    static const float kScrollH = 255.0f;        // 10 rows: 9 * 26 + 21

    static const float kFooterX = 64.0f;         // 51 + kFibPad (13)
    static const float kFooterCapTop = 360.0f;
    static const float kFooterPitch = kFibPad;   // 13.0f (F7)
    static const float kFooterMaxW = 512.0f;

    // Key repeat timings in frames; kRepeatFast while R is held.
    enum { kRepeatStart = 18, kRepeatEvery = 4, kRepeatFast = 2 };

    // Watermark capital height (placed in hud_layout.h).
    static const float kWatermarkCap = 9.0f;

    // Unified glossy GX button color themes: 0=A green, 1=B red, 2=light silver, 3=Z purple.
    enum BtnTheme {
        kBtnA = 0,
        kBtnB = 1,
        kBtnLight = 2,
        kBtnZ = 3
    };
    extern const gfx::Rgba kBtnTheme[4][3];

    // Smoked-charcoal v1 Direction A modal panel specification (4:3 coordinates).
    struct ModalFrame {
        float left43, right43, topY, botY;
        float hdrBot;        // top of header divider (header fills inTop..hdrBot)
        float descTop;       // top of description divider, or 0 if no description band
        float footTop;       // top of footer divider (footer fills footTop+2..inBot)
        float ribbonRight43; // right edge of flush-left teal header ribbon (0 = none)
        gfx::Rgba backdrop;  // full-screen dim behind the modal (0 = none)
    };

    // 4-strip opaque outer frame border around a translucent 4:3-proportioned panel.
    void drawFrameBorder(float left43, float right43, float topY, float botY, float border, gfx::Rgba color);

    // Draws a v1 Direction A modal backdrop, 4-strip border, header/body/(desc)/footer
    // bands with 2px dividers, flush-left header ribbon + // stripes, optional DisplayFont
    // ribbon title, and bottom inner shadow.
    void drawModalFrame(const ModalFrame& f, const char* title = NULL, float dy = 0.0f, float alpha = 1.0f);

    // Flush-left slanted header ribbon + twin "//" diagonal stripes.
    void drawHeaderRibbon(float inL43, float right43, float topY, float height, gfx::Rgba topCol, gfx::Rgba botCol);

#ifndef TM_MODULE_MENU
    // CRT-safe metallic bevel bar: 2-stop vertical gradient with 2px top sheen
    // and 2px bottom shadow (zero 1px horizontal lines for flicker-free 480i).
    void drawBevelBar(float x, float y, float width, float height, gfx::Rgba topCol, gfx::Rgba botCol,
                      gfx::Rgba sheenCol, gfx::Rgba edgeCol);
#endif

    // Stock P+ ~24 deg slanted vertical-gradient quad in 2D screen coordinates.
    void drawSlantGradV(float x0Top, float x0Bot, float x1Top, float x1Bot, float y0, float y1, gfx::Rgba topCol,
                        gfx::Rgba botCol);

    // Slanted v1 Direction A bar in 4:3 coordinates with optional 2px border frame.
    void drawSlantBar(float x043, float x143, float y, float height, bool flushLeft, gfx::Rgba topCol,
                      gfx::Rgba botCol, gfx::Rgba edgeCol);

    // Slanted Solo-Gold focused row bar with red cursor triangle and optional "//" stripes.
    void drawGoldBar(float x043, float x143, float y, float h, float cursorSize, bool flushLeft = false,
                     float stripeRight43 = 0.0f);

    // Slanted v1 Direction A value pill (On green, focused light, or dark slate).
    void drawValuePill(float left43, float right43, float y, float rowH, bool isOn, bool on, bool selected);

    // Left/right adjustment triangles inside a focused value pill.
    void drawValueArrows(float pillLeft43, float pillRight43, float rowY, float rowH, gfx::Rgba col);

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

    // Flags for drawFooterBadge.
    enum FooterBadgeFlags {
        kBadgeRound = 1,
        kBadgeTightGap = 2, // kFibTight (5 px) gap instead of kFibGap (8 px)
        kBadgeWidePad = 4,  // kFibPad + kFibGap trailing pad instead of kFibPad
        kBadgeDense = 8     // kTypeMicro (8 px) dense footer tier
    };

    // Draws a glossy footer button badge + label and returns the next item's x.
    float drawFooterBadge(float x, float cy, float badgeH, const char* glyph, const char* label, u8 theme, u8 flags,
                          gfx::Rgba labelCol = kLabMutedText);

    struct FooterBadgeItem {
        const char* glyph;
        const char* label;
        u8 theme;
        u8 flags;
        gfx::Rgba labelCol;
    };
    // 2-pass batched footer strip: draws all glossy pills first, then all glyph/label text.
    void drawFooterBadges(float x, float cy, float badgeH, const FooterBadgeItem* items, int count);

    // Horizontally squeezes `str` to fit within `maxW` (screen px) when needed.
    void drawFitText(float x, float y, const char* str, gfx::Rgba color, float scale, float maxW,
                     bool centre = false);

    // Scrollbar at an explicit 4:3 left position, top y, height, and palette colors.
    void drawScrollBarFloat(float left43, float topY, float height, int count, float scrollF, int visible,
                            gfx::Rgba trackCol, gfx::Rgba thumbTop, gfx::Rgba thumbBot);
    void drawScrollBarAt(float left43, float topY, float height, int count, int scroll, int visible,
                         gfx::Rgba trackCol, gfx::Rgba thumbTop, gfx::Rgba thumbBot);

#if defined(TM_MODULE_MENU) || !defined(__MWERKS__)
    void drawScrollBar(int count, int scroll, int visible = kVisibleRows, float height = kScrollH);
#endif
}
}
}
