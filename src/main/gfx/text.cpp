#include "main/gfx/text.h"

#include <GX/GXPixel.h>
#include <ms/ms_char_writer.h>
#ifdef __MWERKS__
#include <GX/GXTev.h>
#endif
#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
#include <GX/GXGeometry.h>
#include <GX/GXHardware.h>
#include <GX/GXTexture.h>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#endif

#include "main/gfx/format_core.h"
#include "shared/perf.h"

namespace tm {
namespace gfx {
    // The game's resident system font object (24px) and chunky display font
    // object (32px, slot 2 right beside it in MEM1 MeleeFont). Their word at
    // +0x10 points into the font resource once loaded.
    static void* const kSystemFont = reinterpret_cast<void*>(0x80497E44);
    static void* const kDisplayFont = reinterpret_cast<void*>(0x80497DA4);

#if !(defined(TM_MODULE_MAIN) && defined(__MWERKS__))
    static float s_widths[kTextSlots];
#endif

    static bool fontLoaded()
    {
        u32 resource = *reinterpret_cast<const u32*>(reinterpret_cast<u32>(kSystemFont) + 0x10);
        return resource >= 0x80000000 && resource < 0x81800000;
    }

    bool displayFontLoaded()
    {
        u32 resource = *reinterpret_cast<const u32*>(reinterpret_cast<u32>(kDisplayFont) + 0x10);
        return resource >= 0x80000000 && resource < 0x81800000;
    }

    // Verified against live MEM1 RFNU CMAP at 0x81009580: 79 printable ASCII
    // chars present; missing the 16 chars #$()*<>[\]^_`{|}.
    bool displayHasChar(u8 c)
    {
        if (c < 0x20 || c > 0x7E)
        {
            return false;
        }
        static const u32 kMask[3] = {0xAFFFF8E7u, 0x07FFFFFFu, 0x47FFFFFEu};
        u32 idx = static_cast<u32>(c - 0x20);
        return ((kMask[idx >> 5] >> (idx & 31u)) & 1u) != 0u;
    }

    // Constructed on first use, inside the render hook, once the game is up.
    static ms::CharWriter& writer()
    {
        static ms::CharWriter s_writer;
        return s_writer;
    }

#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
    // ms::CharWriter::Print (0x8006FE50) queries the font vtable (+0x50) and
    // calls PrintGlyph (0x8007001C), which runs 4 integer divisions (divwu)
    // and a GXBegin(GX_QUADS, 0, 4) per character. Caching the precomputed
    // glyph metrics/UVs per font resource lets us batch runs on the same
    // texture sheet into one GXBegin and compute textWidth() without touching
    // GX at all. Matches 0x8007001C's vertex format, float rounding order,
    // and static texture cache (0x80497EC8).
    enum { kGlyphFirst = 32, kGlyphCodes = 96, kMaxSheets = 16, kBatchChars = 64 };
    struct Glyph {
        u16 u0, v0, u1, v1;
        s8 left;
        u8 width;
        s8 advance;
        u8 height;
        s8 sheetIdx;
        u8 pad;
    };
    struct TexSheet {
        const void* texture;
        GXTexObj obj;
    };
    // Allocated in Heaps::Network on match attach (fail-closed to CharWriter::Print
    // if unavailable) to keep ~1.9 KiB out of the match module's static .bss.
    struct GlyphCache {
        Glyph glyphs[kGlyphCodes];
        TexSheet sheets[kMaxSheets];
        float widths[kTextSlots];
        u8 haveGlyph[kGlyphCodes / 8];
        u32 sheetCount;
        u32 glyphResource;
        u32 glyphMinF;
        u32 glyphMagF;
    };
    static GlyphCache* s_glyphCache = NULL;

    static float* widthSlots()
    {
        return s_glyphCache != NULL ? s_glyphCache->widths : NULL;
    }

    static void syncGlyphCache(const ms::CharWriter& w)
    {
        GlyphCache* c = s_glyphCache;
        u32 resource = *reinterpret_cast<const u32*>(reinterpret_cast<u32>(kSystemFont) + 0x10);
        u32 minF = static_cast<u32>(w.m_56);
        u32 magF = static_cast<u32>(w.m_60);
        if (resource != c->glyphResource || minF != c->glyphMinF || magF != c->glyphMagF)
        {
            for (u32 i = 0; i < sizeof(c->haveGlyph); i++)
            {
                c->haveGlyph[i] = 0;
            }
            c->sheetCount = 0;
            c->glyphResource = resource;
            c->glyphMinF = minF;
            c->glyphMagF = magF;
        }
    }

    // Matches ms::CharWriter::PrintGlyph's static texture cache at 0x80497EC8
    // ({0, pTexture, minFilter, magFilter}), cleared by SetupGX (0x8006EEEC).
    // Exact RSBE01 rev 2 GX addresses called by PrintGlyph (0x8007020C..0x80070240).
    static int findOrInitSheet(const void* texture, u32 texFormat, u16 texWidth, u16 texHeight)
    {
        GlyphCache* c = s_glyphCache;
        for (u32 i = 0; i < c->sheetCount; i++)
        {
            if (c->sheets[i].texture == texture)
            {
                return static_cast<int>(i);
            }
        }
        if (c->sheetCount >= kMaxSheets)
        {
            return -1;
        }
        typedef void (*InitTexObjFn)(GXTexObj*, void*, u16, u16, GXTexFmt, GXTexWrapMode, GXTexWrapMode, GXBool);
        typedef void (*InitTexObjLodFn)(GXTexObj*, GXTexFilter, GXTexFilter, f32, f32, f32, GXBool, GXBool,
                                        GXAnisotropy);
        TexSheet& s = c->sheets[c->sheetCount];
        s.texture = texture;
        reinterpret_cast<InitTexObjFn>(0x801F28F0)(&s.obj, const_cast<void*>(texture), texWidth, texHeight,
                                                   static_cast<GXTexFmt>(texFormat), GX_CLAMP, GX_CLAMP, false);
        reinterpret_cast<InitTexObjLodFn>(0x801F2B44)(&s.obj, static_cast<GXTexFilter>(c->glyphMinF),
                                                      static_cast<GXTexFilter>(c->glyphMagF), 0.0f, 0.0f, 0.0f, false,
                                                      false, GX_ANISO_1);
        return static_cast<int>(c->sheetCount++);
    }

    static void ensureGlyph(u8 code)
    {
        GlyphCache* c = s_glyphCache;
        u8 idx = static_cast<u8>(code - kGlyphFirst);
        if ((c->haveGlyph[idx >> 3] & (1 << (idx & 7))) != 0)
        {
            return;
        }
        u32 rawWords[6];
        u8* raw = reinterpret_cast<u8*>(rawWords);
        typedef void (*GetGlyph)(void* font, void* glyph, u16 code);
        GetGlyph get = *reinterpret_cast<GetGlyph*>(*reinterpret_cast<u32*>(kSystemFont) + 0x50);
        get(kSystemFont, raw, code);
        Glyph& g = c->glyphs[idx];
        const void* texture = *reinterpret_cast<const void* const*>(raw + 0);
        g.left = static_cast<s8>(raw[4]);
        u8 gw = raw[5];
        g.width = gw;
        g.advance = static_cast<s8>(raw[6]);
        u8 gh = raw[7];
        g.height = gh;
        u32 texFormat = *reinterpret_cast<const u32*>(raw + 8);
        u16 tw = *reinterpret_cast<const u16*>(raw + 12);
        u16 th = *reinterpret_cast<const u16*>(raw + 14);
        u16 cx = *reinterpret_cast<const u16*>(raw + 16);
        u16 cy = *reinterpret_cast<const u16*>(raw + 18);
        g.u0 = glyphTexCoord(cx, tw);
        g.v0 = glyphTexCoord(cy, th);
        g.u1 = glyphTexCoord(cx + gw, tw);
        g.v1 = glyphTexCoord(cy + gh, th);
        g.sheetIdx = gw != 0 ? static_cast<s8>(findOrInitSheet(texture, texFormat, tw, th)) : -1;
        g.pad = 0;
        c->haveGlyph[idx >> 3] |= static_cast<u8>(1 << (idx & 7));
    }

    static void bindSheet(int sheetIdx)
    {
        GlyphCache* c = s_glyphCache;
        volatile u32* loaded = reinterpret_cast<volatile u32*>(0x80497EC8);
        const TexSheet& s = c->sheets[sheetIdx];
        u32 texAddr = reinterpret_cast<u32>(s.texture);
        if (loaded[0] == 0 && loaded[1] == texAddr && loaded[2] == c->glyphMinF && loaded[3] == c->glyphMagF)
        {
            return;
        }
        typedef void (*LoadTexObjFn)(const GXTexObj*, GXTexMapID);
        reinterpret_cast<LoadTexObjFn>(0x801F2FB0)(&s.obj, GX_TEXMAP0);
        loaded[0] = 0;
        loaded[1] = texAddr;
        loaded[2] = c->glyphMinF;
        loaded[3] = c->glyphMagF;
    }

    static inline void emitGlyphQuad(const Glyph& g, float curX, float y, Rgba color, float sx, float sy, float mx,
                                     float mw)
    {
        float x0 = curX + mw * (sx * (static_cast<float>(g.left) * mx));
        float x1 = x0 + mw * (sx * (static_cast<float>(g.width) * mx));
        float y1 = y + sy * (static_cast<float>(g.height) * mx);
        WGPIPE.f = x0;
        WGPIPE.f = y;
        WGPIPE.f = 0.0f;
        WGPIPE.ui = color;
        WGPIPE.us = g.u0;
        WGPIPE.us = g.v0;
        WGPIPE.f = x1;
        WGPIPE.f = y;
        WGPIPE.f = 0.0f;
        WGPIPE.ui = color;
        WGPIPE.us = g.u1;
        WGPIPE.us = g.v0;
        WGPIPE.f = x1;
        WGPIPE.f = y1;
        WGPIPE.f = 0.0f;
        WGPIPE.ui = color;
        WGPIPE.us = g.u1;
        WGPIPE.us = g.v1;
        WGPIPE.f = x0;
        WGPIPE.f = y1;
        WGPIPE.f = 0.0f;
        WGPIPE.ui = color;
        WGPIPE.us = g.u0;
        WGPIPE.us = g.v1;
    }

    static float drawBatched(ms::CharWriter& w, float x, float y, const char* str, Rgba color, float sx, float sy)
    {
        syncGlyphCache(w);
        float mx = w.m_104;
        float mw = w.m_unknownFontWidthModifier;
        float curX = x;
        const char* p = str;
        while (*p != '\0')
        {
            if (static_cast<u32>(static_cast<u8>(*p) - kGlyphFirst) >= kGlyphCodes)
            {
                w.SetTextColor(nw4r::ut::Color(color));
                w.SetScale(sx, sy);
                w.SetCursor(curX, y, 0.0f);
                w.Print(static_cast<u8>(*p));
                curX = w.GetCursorX();
                p++;
                continue;
            }
            // Group up to kBatchChars ASCII glyphs by cached texture sheet so
            // multi-page fonts switch textures at most once per page per string.
            float xs[kBatchChars];
            u8 codes[kBatchChars];
            u16 sheetCounts[kMaxSheets];
            u32 sheetMask = 0;
            int count = 0;
            while (*p != '\0' && count < kBatchChars)
            {
                u8 c = static_cast<u8>(*p);
                if (static_cast<u32>(c - kGlyphFirst) >= kGlyphCodes)
                {
                    break;
                }
                u8 idx = static_cast<u8>(c - kGlyphFirst);
                ensureGlyph(c);
                const Glyph& g = s_glyphCache->glyphs[idx];
                if (g.width != 0)
                {
                    int sh = g.sheetIdx;
                    if (sh < 0)
                    {
                        break;
                    }
                    if ((sheetMask & (1u << sh)) == 0)
                    {
                        sheetMask |= (1u << sh);
                        sheetCounts[sh] = 0;
                    }
                    xs[count] = curX;
                    codes[count] = idx;
                    sheetCounts[sh]++;
                    count++;
                }
                curX += mw * (sx * (static_cast<float>(g.advance) * mx));
                p++;
            }
            if (count == 0)
            {
                if (*p != '\0' && static_cast<u32>(static_cast<u8>(*p) - kGlyphFirst) < kGlyphCodes)
                {
                    // Sheet table full: fall back to CharWriter::Print for this char.
                    w.SetTextColor(nw4r::ut::Color(color));
                    w.SetScale(sx, sy);
                    w.SetCursor(curX, y, 0.0f);
                    w.Print(static_cast<u8>(*p));
                    curX = w.GetCursorX();
                    p++;
                }
                continue;
            }
            // Draw the currently loaded sheet first (if present) to skip a GXLoadTexObj.
            volatile u32* loaded = reinterpret_cast<volatile u32*>(0x80497EC8);
            int firstSheet = -1;
            if (loaded[0] == 0)
            {
                u32 activeTex = loaded[1];
                for (u32 sh = 0; sh < s_glyphCache->sheetCount; sh++)
                {
                    if ((sheetMask & (1u << sh)) != 0
                        && reinterpret_cast<u32>(s_glyphCache->sheets[sh].texture) == activeTex)
                    {
                        firstSheet = static_cast<int>(sh);
                        break;
                    }
                }
            }
            for (int pass = 0; sheetMask != 0; pass++)
            {
                int sh = firstSheet;
                if (pass > 0 || sh < 0)
                {
                    sh = 0;
                    while ((sheetMask & (1u << sh)) == 0)
                    {
                        sh++;
                    }
                }
                sheetMask &= ~(1u << sh);
                bindSheet(sh);
                GXBegin(GX_QUADS, GX_VTXFMT0, static_cast<u16>(sheetCounts[sh] * 4));
                for (int i = 0; i < count; i++)
                {
                    const Glyph& g = s_glyphCache->glyphs[codes[i]];
                    if (g.sheetIdx == sh)
                    {
                        emitGlyphQuad(g, xs[i], y, color, sx, sy, mx, mw);
                    }
                }
            }
        }
        return curX;
    }
#else
    // The menu module and PC host tests hand characters to CharWriter::Print.
    static void print(ms::CharWriter& w, u8 code)
    {
        w.Print(code);
    }
#endif

    float lineHeight(float scale)
    {
        if (!fontLoaded())
        {
            return 0.0f;
        }
        ms::CharWriter& w = writer();
        w.m_font = kSystemFont;
        w.SetScale(scale, scale);
        return w.GetFontHeight();
    }

    // Hardware GX TEV alpha-slope tightening for Brawl's I4 bitmap fonts
    // (0x80497E44 SystemFont & 0x80497DA4 DisplayFont without edge pass).
    // Bilinear filtering smears a 1-texel alpha ramp over 3-4 pixels at 2x-4x
    // internal resolution; Stage 0 maps TEXA -> clamp(2 * TEXA - 0.5, 0, 1)
    // (preserving the exact 0.5 stroke midpoint and smooth AA on 480p/480i
    // while halving bilinear halo width at high IR), then Stage 1 modulates
    // by vertex alpha (RASA).
    static inline void applyCrispTextTev()
    {
#ifdef __MWERKS__
        GXSetNumTevStages(2);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, TRUE, GX_TEVPREV);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
        GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_1, TRUE, GX_TEVPREV);

        GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
        GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, TRUE, GX_TEVPREV);
        GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_RASA, GX_CA_ZERO);
        GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, TRUE, GX_TEVPREV);
#endif
    }

    float text(float x, float y, const char* text, Rgba color, float scaleX, float scaleY)
    {
        if (!fontLoaded())
        {
            return x;
        }
        ms::CharWriter& w = writer();
        w.m_font = kSystemFont;
        // The writer's setup covers the TEV stages, blending and its vertex
        // format; the rest (matrix, culling, alpha test) comes from ours.
        // Texts drawn one after another share it.
        if (!textStateKept() || TM_PERF_LEGACY())
        {
            prepare();
            w.SetupGX();
            GXSetZMode(false, GX_ALWAYS, false);
            applyCrispTextTev();
            keepTextState();
        }
#ifdef TM_MODULE_MAIN
        TM_PERF_COUNT(kCountTexts, 1);
        const char* first = text;
#endif
#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
        if (s_glyphCache != NULL && !w.m_isFixedWidth && w._76[0] == 0 && w.m_edgeWidth == 0.0f && !TM_PERF_LEGACY())
        {
            float endX = drawBatched(w, x, y, text, color, scaleX * xScale(), scaleY);
            while (*text != '\0')
            {
                text++;
            }
            TM_PERF_COUNT(kCountChars, text - first);
            return endX;
        }
        // Rgba is 0xRRGGBBAA, the byte order of nw4r::ut::Color's u32 form.
        w.SetTextColor(nw4r::ut::Color(color));
        w.SetScale(scaleX * xScale(), scaleY);
        w.SetCursor(x, y, 0.0f);
        for (; *text != '\0'; text++)
        {
            w.Print(static_cast<u8>(*text));
        }
#else
        w.SetTextColor(nw4r::ut::Color(color));
        w.SetScale(scaleX * xScale(), scaleY);
        w.SetCursor(x, y, 0.0f);
        for (; *text != '\0'; text++)
        {
            print(w, static_cast<u8>(*text));
        }
#endif
#ifdef TM_MODULE_MAIN
        TM_PERF_COUNT(kCountChars, text - first);
#endif
        return w.GetCursorX();
    }

    float text(float x, float y, const char* str, Rgba color, float scale)
    {
        return text(x, y, str, color, scale, scale);
    }

    float displayText(float x, float y, const char* str, Rgba topColor, Rgba botColor, float scaleX, float scaleY,
                      float edgeWidth, Rgba edgeColor)
    {
        bool haveDisp = displayFontLoaded();
        bool haveSys = fontLoaded();
        if ((!haveDisp && !haveSys) || str == NULL || *str == '\0')
        {
            return x;
        }
        // Cap-height ratio: DisplayFont caps are 27px starting at +2px;
        // SystemFont caps are 15px starting at +5px. Scaling SystemFont by
        // 27/15 = 1.8 and shifting y by (2 - 5 * 1.8) * scaleY = -7 * scaleY
        // aligns capitals to the exact same top and bottom pixels.
        static const float kSysMul = 1.8f;
        static const float kSysYShift = -7.0f;

        ms::CharWriter& w = writer();
        bool firstDisp = haveDisp && displayHasChar(static_cast<u8>(*str));
        w.m_font = (firstDisp || !haveSys) ? kDisplayFont : kSystemFont;
        // Edge-outlined displayText relies on CharWriter's native TEV setup, so
        // re-run SetupGX whenever edgeWidth > 0 and drop cached text state after.
        if (edgeWidth > 0.0f || !textStateKept() || TM_PERF_LEGACY())
        {
            prepare();
            w.SetupGX();
            GXSetZMode(false, GX_ALWAYS, false);
            if (edgeWidth <= 0.0f)
            {
                applyCrispTextTev();
                keepTextState();
            }
            else
            {
                forgetState();
            }
        }
        w.SetTextColor(nw4r::ut::Color(topColor));
        if (botColor != topColor)
        {
            w.m_colorRect.m_bottomLeft = nw4r::ut::Color(botColor);
            w.m_colorRect.m_bottomRight = nw4r::ut::Color(botColor);
        }
        if (edgeWidth > 0.0f)
        {
            w.SetEdge(edgeWidth, nw4r::ut::Color(edgeColor));
        }

        float curX = x;
        float sxBase = scaleX * xScale();
        int activeFont = -1; // 1 = display, 0 = system
        for (const char* p = str; *p != '\0'; p++)
        {
            u8 c = static_cast<u8>(*p);
            int wantDisp = (haveDisp && displayHasChar(c)) ? 1 : 0;
            if (!wantDisp && !haveSys)
            {
                continue;
            }
            if (wantDisp != activeFont)
            {
                activeFont = wantDisp;
                if (wantDisp)
                {
                    w.m_font = kDisplayFont;
                    w.SetScale(sxBase, scaleY);
                    w.SetCursor(curX, y, 0.0f);
                }
                else
                {
                    w.m_font = kSystemFont;
                    w.SetScale(sxBase * kSysMul, scaleY * kSysMul);
                    w.SetCursor(curX, y + kSysYShift * scaleY, 0.0f);
                }
            }
            w.Print(c);
            curX = w.GetCursorX();
        }
        if (edgeWidth > 0.0f)
        {
            w.SetEdge(0.0f, nw4r::ut::Color(0));
        }
        w.m_font = kSystemFont;
        return curX;
    }

    float displayText(float x, float y, const char* str, Rgba color, float scale)
    {
        return displayText(x, y, str, color, color, scale, scale, 0.0f, 0);
    }

    float displayTextWidth(const char* str, float scale)
    {
        const float offScreen = -2000.0f;
        return displayText(offScreen, offScreen, str, 0, 0, scale, scale, 0.0f, 0) - offScreen;
    }

#ifdef TM_MODULE_MAIN
#if !defined(__MWERKS__)
    // Widths measured lately (used on host tests; on Wii the glyph cache
    // computes ASCII widths directly without touching GX).
    enum { kWidthCache = 32, kWidthText = 48 };
    struct CachedWidth {
        float scale;
        float xScale;
        float width;
        char text[kWidthText];
    };
    static CachedWidth s_cache[kWidthCache];
    static u32 s_cacheNext = 0;

    static bool sameText(const char* a, const char* b)
    {
        for (; *a == *b; a++, b++)
        {
            if (*a == '\0')
            {
                return true;
            }
        }
        return false;
    }
#endif

    float textWidth(const char* str, float scale)
    {
#ifdef __MWERKS__
        if (s_glyphCache != NULL && !TM_PERF_LEGACY())
        {
            if (!fontLoaded())
            {
                return 0.0f;
            }
            ms::CharWriter& w = writer();
            if (!w.m_isFixedWidth)
            {
                syncGlyphCache(w);
                float sx = scale * xScale();
                float mx = w.m_104;
                float mw = w.m_unknownFontWidthModifier;
                float width = 0.0f;
                bool allAscii = true;
                for (const char* p = str; *p != '\0'; p++)
                {
                    u8 c = static_cast<u8>(*p);
                    if (static_cast<u32>(c - kGlyphFirst) >= kGlyphCodes)
                    {
                        allAscii = false;
                        break;
                    }
                    ensureGlyph(c);
                    width += mw * (sx * (static_cast<float>(s_glyphCache->glyphs[c - kGlyphFirst].advance) * mx));
                }
                if (allAscii)
                {
                    return width;
                }
            }
        }
#endif
#if !defined(__MWERKS__)
        u32 len = 0;
        while (str[len] != '\0' && len < kWidthText)
        {
            len++;
        }
        bool cacheable = len < kWidthText && fontLoaded() && !TM_PERF_LEGACY();
        if (cacheable)
        {
            for (u32 i = 0; i < kWidthCache; i++)
            {
                const CachedWidth& c = s_cache[i];
                if (c.scale == scale && c.xScale == xScale() && c.text[0] != '\0' && sameText(c.text, str))
                {
                    return c.width;
                }
            }
        }
#ifdef TM_MODULE_MAIN
        TM_PERF_COUNT(kCountWidths, 1);
#endif
        const float offScreen = -2000.0f;
        float width = text(offScreen, offScreen, str, 0, scale) - offScreen;
        if (cacheable && len > 0)
        {
            CachedWidth& c = s_cache[s_cacheNext];
            s_cacheNext = (s_cacheNext + 1) % kWidthCache;
            c.scale = scale;
            c.xScale = xScale();
            c.width = width;
            for (u32 i = 0; i <= len; i++)
            {
                c.text[i] = str[i];
            }
        }
        return width;
#else
        const float offScreen = -2000.0f;
        return text(offScreen, offScreen, str, 0, scale) - offScreen;
#endif
    }
#else
    // The menu module measures few texts a frame: no cache.
    float textWidth(const char* str, float scale)
    {
        const float offScreen = -2000.0f;
        return text(offScreen, offScreen, str, 0, scale) - offScreen;
    }
#endif

#if !(defined(TM_MODULE_MAIN) && defined(__MWERKS__))
    static float* widthSlots()
    {
        return s_widths;
    }
#endif

    void textRight(int slot, float right, float y, const char* str, Rgba color, float scale)
    {
        float* widths = widthSlots();
        if (widths == NULL || slot < 0 || slot >= kTextSlots)
        {
            return;
        }
        float x = right - widths[slot];
        widths[slot] = text(x, y, str, color, scale) - x;
    }

    void textCentered(int slot, float centre, float y, const char* str, Rgba color, float scale)
    {
        float* widths = widthSlots();
        if (widths == NULL || slot < 0 || slot >= kTextSlots)
        {
            return;
        }
        float x = centre - widths[slot] * 0.5f;
        widths[slot] = text(x, y, str, color, scale) - x;
    }

    void textBox(int slot, float x, float y, const char* str, Rgba color, Rgba box, float scale, bool centre)
    {
        float* widths = widthSlots();
        if (widths == NULL || slot < 0 || slot >= kTextSlots)
        {
            return;
        }
        const float pad = 6.0f;
        float w = widths[slot];
        float left = centre ? x - w * 0.5f : x;
        if (w > 0.0f)
        {
            fillRect(left - pad, y - pad * 0.5f, w + 2 * pad, lineHeight(scale) + pad, box);
        }
        widths[slot] = text(left, y, str, color, scale) - left;
    }

    void forgetWidths()
    {
        float* widths = widthSlots();
        if (widths == NULL)
        {
            return;
        }
        for (int i = 0; i < kTextSlots; i++)
        {
            widths[i] = 0.0f;
        }
    }

    void textAttach()
    {
#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
        if (s_glyphCache == NULL)
        {
            s_glyphCache = static_cast<GlyphCache*>(game::allocNetworkZero(static_cast<u32>(sizeof(GlyphCache))));
        }
#endif
    }

    void textDetach()
    {
#if defined(TM_MODULE_MAIN) && defined(__MWERKS__)
        game::freeNetworkSlot(reinterpret_cast<void**>(&s_glyphCache));
#endif
    }
}
}

#if defined(TM_PERF) && defined(TM_MODULE_MAIN)
#include <OS/OSTime.h>

namespace tm {
namespace gfx {
    // Times 200 characters printed off screen, 200 glyph lookups alone (the
    // font's virtual call that Print makes first), and 50 shape setups.
    void perfTextCalibrate(u32* t)
    {
        t[0] = t[1] = t[2] = 0;
        if (!fontLoaded())
        {
            return;
        }
        begin();
        ms::CharWriter& w = writer();
        w.m_font = kSystemFont;
        w.SetScale(0.5f, 0.5f);
        w.SetCursor(-2000.0f, -2000.0f, 0.0f);
        prepare();
        w.SetupGX();
        u32 t0 = OSGetTick();
        for (int i = 0; i < 200; i++)
        {
            w.Print('A');
        }
        t[0] = OSGetTick() - t0;
        typedef void (*GetGlyph)(void* font, void* glyph, u16 code);
        GetGlyph glyph = *reinterpret_cast<GetGlyph*>(*reinterpret_cast<u32*>(kSystemFont) + 80);
        u32 out[16];
        t0 = OSGetTick();
        for (int i = 0; i < 200; i++)
        {
            glyph(kSystemFont, out, 'A');
        }
        t[1] = OSGetTick() - t0;
        t0 = OSGetTick();
        for (int i = 0; i < 50; i++)
        {
            forgetState();
            prepare();
        }
        t[2] = OSGetTick() - t0;
        end();
    }
}
}
#endif
