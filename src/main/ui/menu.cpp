#include "main/ui/menu.h"

#include <cstdio>
#include <string.h>
#include <gf/gf_pad_status.h>

#include "main/extra.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/menu_theme_core.h"
#include "main/ui/style.h"
#include "shared/game/menu_sound.h"
#include "shared/log.h"
#include "shared/ui_icons.h"

namespace tm {
namespace ui {
    using namespace style;

    static int s_activeRow = 0;
    static u8 s_scratch[kScratchBytes];
    static bool s_rail = false;
    bool g_menuQuiet = false;
    bool g_showInactiveRows = false;
#ifdef __MWERKS__
    u8 g_counterAvailMask = 0;
#endif

    void menuSound(int id)
    {
        if (!g_menuQuiet)
        {
            TM_LOG("ui: sound %d (handle %d)\n", id, game::playMenuSound(id));
        }
    }

    // Only the extra module calls it: kept from the linker's stripping.
#pragma force_active on
    u8* menuScratch()
    {
        return s_scratch;
    }
#pragma force_active reset

    static u8* valueAt(const MenuDef& def, const Option& o)
    {
        return ((o.flags & kFlagScratch) ? s_scratch : def.values()) + o.offset;
    }

    int activeRow()
    {
        return s_activeRow;
    }

    static const Page& pageAt(const MenuDef& def, int id)
    {
        return *def.pages[id];
    }

    static bool isTabsStyle(const MenuDef& def)
    {
        return def.categoryCount > 0 && main::settings().controls.menuStyle == main::kMenuStyleTabs;
    }

    static bool isRailStyle(const MenuDef& def)
    {
        return def.categoryCount > 0 && main::settings().controls.menuStyle != main::kMenuStyleTabs;
    }

    static int visibleRowLimit(const MenuDef& def, const MenuState& state)
    {
        return (isTabsStyle(def) && state.depth > 0) ? kLabSubVisibleRows : kLabVisibleRows;
    }

    int currentPage(const MenuState& state)
    {
        return state.stack[state.depth];
    }

    const Option* currentOption(const MenuDef& def, const MenuState& state)
    {
        int id = currentPage(state);
        const Page& p = pageAt(def, id);
        return state.cursor[id] < p.count ? &p.options[state.cursor[id]] : NULL;
    }

    int optionValue(const MenuDef& def, const Option& o)
    {
        if (o.flags & kFlagUnavailable)
        {
            return o.def;
        }
        const u8* p = valueAt(def, o);
        if (o.flags & kFlagBit)
        {
            return (*p & static_cast<u8>(o.min)) != 0 ? 1 : 0;
        }
        if (o.flags & kFlagHalf)
        {
            u16 v = *reinterpret_cast<const u16*>(p);
            return (o.flags & kFlagSigned) ? static_cast<s16>(v) : v;
        }
        return (o.flags & kFlagSigned) ? static_cast<s8>(*p) : *p;
    }

    void setOptionValue(const MenuDef& def, const Option& o, int value)
    {
        if (o.flags & kFlagUnavailable)
        {
            return;
        }
        u8* p = valueAt(def, o);
        if (o.flags & kFlagBit)
        {
            u8 mask = static_cast<u8>(o.min);
            if (value != 0)
            {
                *p |= mask;
            }
            else
            {
                *p &= static_cast<u8>(~mask);
            }
            return;
        }
        if (o.flags & kFlagHalf)
        {
            *reinterpret_cast<u16*>(p) = static_cast<u16>(value);
        }
        else
        {
            *p = static_cast<u8>(value);
        }
    }

    bool hasValue(const Option& o)
    {
        return static_cast<u32>(o.kind - kKindChoice) <= (kKindToggle - kKindChoice);
    }

    static bool stored(const Option& o)
    {
        return hasValue(o) && !(o.flags & kFlagUnavailable);
    }

    static int clampTo(const Option& o, int v)
    {
        int lo = 0;
        int hi = 1;
        if (!(o.flags & kFlagBit))
        {
            if (o.kind == kKindInt)
            {
                lo = (o.flags & kFlagSigned) ? static_cast<int>(static_cast<s8>(o.min)) : static_cast<int>(o.min);
            }
            if (o.kind != kKindToggle)
            {
                hi = o.max;
            }
        }
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static void forEachStored(const MenuDef& def, bool reset)
    {
        for (int pg = 0; pg < def.pageCount; pg++)
        {
            const Page& p = *def.pages[pg];
            for (int i = 0; i < p.count; i++)
            {
                const Option& o = p.options[i];
                if (stored(o))
                {
                    setOptionValue(def, o, clampTo(o, reset ? o.def : optionValue(def, o)));
                }
            }
        }
    }

    void clampAll(const MenuDef& def)
    {
        forEachStored(def, false);
    }

    void defaultAll(const MenuDef& def)
    {
        forEachStored(def, true);
        g_showInactiveRows = false;
        s_scratch[kShowInactiveScratchOffset] = 0;
    }

#pragma dont_inline on
    bool defaultOption(const MenuDef& def, const Option& o)
    {
        if (!stored(o) || (o.flags & kFlagScratch))
        {
            return false;
        }
        setOptionValue(def, o, clampTo(o, o.def));
        return true;
    }

    static void defaultInPages(const MenuDef& def, int pg, int endPg, u32 start, u32 end)
    {
        for (; pg < endPg; pg++)
        {
            const Page& p = *def.pages[pg];
            for (int i = 0; i < p.count; i++)
            {
                const Option& o = p.options[i];
                u32 off = o.offset;
                u32 width = (o.flags & kFlagHalf) ? 2u : 1u;
                if (off >= start && off + width <= end)
                {
                    defaultOption(def, o);
                }
            }
        }
    }
#pragma dont_inline reset

    bool defaultPage(const MenuDef& def, int pageId)
    {
        if (static_cast<u32>(pageId) >= def.pageCount)
        {
            return false;
        }
        defaultInPages(def, pageId, pageId + 1, 0, 0x20000u);
        return true;
    }

    void defaultRange(const MenuDef& def, u16 offset, u16 size)
    {
        defaultInPages(def, 0, def.pageCount, offset, static_cast<u32>(offset) + size);
    }

    bool droppedLabel(const char* label)
    {
        return label != NULL && label[0] == TM_DROPPED_LABEL[0];
    }

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static const char* choiceLabel(const Option& o, int idx)
    {
#ifdef __MWERKS__
        if (o.min != 0)
        {
            const char* p = static_cast<const char*>(o.data);
            while (idx > 0)
            {
                while (*p++ != '\0') {}
                idx--;
            }
            return p;
        }
#endif
        return static_cast<const char* const*>(o.data)[idx];
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    void retireDropped(const MenuDef& def)
    {
        for (int pg = 0; pg < def.pageCount; pg++)
        {
            const Page& p = pageAt(def, pg);
            for (int i = 0; i < p.count; i++)
            {
                const Option& o = p.options[i];
                if (o.kind != kKindChoice || !stored(o) || (o.flags & kFlagScratch))
                {
                    continue;
                }
                int v = optionValue(def, o);
                if (v >= 0 && v <= o.max && droppedLabel(choiceLabel(o, v)))
                {
                    setOptionValue(def, o, o.def);
                }
            }
        }
    }

    // Rows omitted while unused, Rail-only-duplicate utilities, or dynamically
    // compacted while disabled.
    static bool hidden(const Option& o)
    {
        if ((o.flags & kFlagHidden) && o.name == NULL)
        {
            return true;
        }
        if (s_rail && (o.flags & kFlagTabsOnly) != 0)
        {
            return true;
        }
        return !g_showInactiveRows && (o.flags & kFlagHideWhenDisabled) != 0 && o.enabled != NULL && !o.enabled();
    }

    static bool selectable(const Option& o)
    {
        if (hidden(o))
        {
            return false;
        }
        bool disabled = o.enabled != NULL && !o.enabled();
        if (disabled && !(g_showInactiveRows && (o.flags & kFlagHideWhenDisabled) != 0))
        {
            return false;
        }
        return !(kSkipUnavailableRows && (o.flags & kFlagUnavailable));
    }

    // Its value can change / A does something.
    static bool active(const Option& o)
    {
        return selectable(o) && (o.enabled == NULL || o.enabled()) && !(o.flags & kFlagUnavailable);
    }

    static int nextRow(const Page& p, int from, int dir)
    {
        for (int i = from + dir; i >= 0 && i < p.count; i += dir)
        {
            if (selectable(p.options[i]))
            {
                return i;
            }
        }
        return -1;
    }

    static int nearestSelectable(const Page& p, int from)
    {
        if (from >= 0 && from < p.count && selectable(p.options[from]))
        {
            return from;
        }
        int back = nextRow(p, from < p.count ? from : p.count, -1);
        if (back >= 0)
        {
            return back;
        }
        int fwd = nextRow(p, from >= 0 ? from : -1, 1);
        return fwd >= 0 ? fwd : (from >= 0 && from < p.count ? from : 0);
    }

    static int visibleCount(const Page& p)
    {
        int n = 0;
        for (int i = 0; i < p.count; i++)
        {
            if (!hidden(p.options[i]))
            {
                n++;
            }
        }
        return n;
    }

    static int visibleOrdinal(const Page& p, int rawIdx)
    {
        int ord = 0;
        for (int i = 0; i < rawIdx && i < p.count; i++)
        {
            if (!hidden(p.options[i]))
            {
                ord++;
            }
        }
        return ord;
    }

    static void enterPage(MenuState& state, const MenuDef& def, int id)
    {
        s_rail = isRailStyle(def);
        int first = nextRow(pageAt(def, id), -1, 1);
        state.cursor[id] = static_cast<u8>(first < 0 ? 0 : first);
        state.scroll[id] = 0;
    }

    void menuReset(const MenuDef& def, MenuState& state)
    {
        g_showInactiveRows = false;
        s_scratch[kShowInactiveScratchOffset] = 0;
        state.depth = 0;
        state.category = 0;
        state.railCursor = 0;
        state.railFocus = false;
        state.openStart = true;
        state.controllerType = 0;
        state.stack[0] = def.categoryCount > 0 ? def.categoryPages[0] : 0;
        for (int i = 0; i < kMaxPages; i++)
        {
            state.cursor[i] = 0;
            state.scroll[i] = 0;
        }
        enterPage(state, def, state.stack[0]);
    }

    // Keep the highlighted row at least kScrollMargin visible rows inside the
    // visible window, clamped at the top and bottom of the visible list.
    static void fixScroll(const Page& p, u8 cur, u8& scroll, int visRows)
    {
        int total = visibleCount(p);
        int s = scroll;
        if (total <= visRows)
        {
            s = 0;
        }
        else
        {
            int ord = visibleOrdinal(p, cur);
            if (ord - s < kScrollMargin)
            {
                s = ord - kScrollMargin;
            }
            if (ord - s > visRows - 1 - kScrollMargin)
            {
                s = ord - (visRows - 1 - kScrollMargin);
            }
            int last = total - visRows;
            s = s < 0 ? 0 : (s > last ? last : s);
        }
        scroll = static_cast<u8>(s);
    }

    static void switchCategory(const MenuDef& def, MenuState& state, u8 cat, bool keepRailFocus)
    {
        for (int d = 0; d <= state.depth; d++)
        {
            u8 left = state.stack[d];
            state.cursor[left] = 0;
            state.scroll[left] = 0;
        }
        state.category = cat;
        state.depth = 0;
        u8 pageId = def.categoryPages[cat];
        state.stack[0] = pageId;
        state.railCursor = cat;
        state.railFocus = keepRailFocus;
        enterPage(state, def, pageId);
    }

    // True if the value changed.
    static bool change(const MenuDef& def, const Option& o, int delta)
    {
        int v = optionValue(def, o);
        int next = o.kind == kKindToggle ? !v : clampTo(o, v + delta);
        if (o.kind == kKindChoice && delta != 0)
        {
            // Step over retired or unavailable choices.
            int step = delta > 0 ? 1 : -1;
            const char* lbl;
            while (next != v && (lbl = choiceLabel(o, next)) != NULL
                   && (droppedLabel(lbl) || lbl[0] == TM_UNAVAILABLE_LABEL[0]
#ifdef __MWERKS__
                       || (lbl[0] == TM_DJC_LABEL[0] && (g_counterAvailMask & 1) == 0)
                       || (lbl[0] == TM_FLOAT_LABEL[0] && (g_counterAvailMask & 2) == 0)
#endif
                       ))
            {
                int further = clampTo(o, next + step);
                next = further == next ? v : further;
            }
        }
        if (next != v)
        {
            setOptionValue(def, o, next);
            if (o.onChange != NULL)
            {
                o.onChange();
            }
        }
        return next != v;
    }

    bool menuStep(const MenuDef& def, MenuState& state, const MenuInput& in)
    {
        int sound = -1;
        s_rail = isRailStyle(def);
        // L / R category or tab switch.
        if (def.categoryCount > 1 && (in.prevCat || in.nextCat))
        {
            u8 cat = state.category;
            if (in.prevCat)
            {
                cat = static_cast<u8>((cat + def.categoryCount - 1) % def.categoryCount);
            }
            else
            {
                cat = static_cast<u8>((cat + 1) % def.categoryCount);
            }
            switchCategory(def, state, cat, false);
            menuSound(game::kSoundMove);
            return true;
        }

        // Left category rail navigation.
        if (s_rail && state.railFocus)
        {
            int totalRail = def.categoryCount + def.utilityCount;
            if (totalRail > 0)
            {
                if (state.railCursor >= totalRail)
                {
                    state.railCursor = 0;
                }
                u8 from = state.railCursor;
                if (in.up && state.railCursor > 0)
                {
                    state.railCursor--;
                }
                if (in.down && state.railCursor + 1 < totalRail)
                {
                    state.railCursor++;
                }
                if (state.railCursor != from)
                {
                    sound = game::kSoundMove;
                    if (state.railCursor < def.categoryCount)
                    {
                        switchCategory(def, state, state.railCursor, true);
                    }
                }
                if (in.right && state.railCursor < def.categoryCount)
                {
                    switchCategory(def, state, state.railCursor, false);
                    sound = game::kSoundMove;
                }
                else if (in.a)
                {
                    if (state.railCursor < def.categoryCount)
                    {
                        switchCategory(def, state, state.railCursor, false);
                        sound = game::kSoundConfirm;
                    }
                    else
                    {
                        int u = state.railCursor - def.categoryCount;
                        if (u >= 0 && u < def.utilityCount && def.utilities[u].onSelect != NULL)
                        {
                            def.utilities[u].onSelect();
                            sound = game::kSoundConfirm;
                        }
                    }
                }
            }
            if (sound >= 0)
            {
                menuSound(sound);
            }
            if (in.b)
            {
                return false;
            }
            u8 shown = state.stack[state.depth];
            s_rail = isRailStyle(def);
            if (!s_rail)
            {
                state.railFocus = false;
            }
            fixScroll(pageAt(def, shown), state.cursor[shown], state.scroll[shown], visibleRowLimit(def, state));
            return true;
        }

        if (!s_rail)
        {
            state.railFocus = false;
        }

        u8 pageId = state.stack[state.depth];
        const Page& p = pageAt(def, pageId);
        u8& cur = state.cursor[pageId];
        if (p.count > 0)
        {
            if (cur >= p.count || !selectable(p.options[cur]))
            {
                cur = static_cast<u8>(nearestSelectable(p, cur));
            }
            u8 from = cur;
            if (in.up)
            {
                int n = nextRow(p, cur, -1);
                cur = static_cast<u8>(n >= 0 ? n : cur);
            }
            if (in.down)
            {
                int n = nextRow(p, cur, 1);
                cur = static_cast<u8>(n >= 0 ? n : cur);
            }
            const Option& o = p.options[cur];
            bool on = active(o);
            int delta = in.right - in.left;
            if (cur != from || (delta != 0 && on && hasValue(o) && change(def, o, delta)))
            {
                sound = game::kSoundMove;
            }
            if (in.a && !on)
            {
                sound = game::kSoundError;
            }
            else if (in.a)
            {
                if (o.kind == kKindMenu && state.depth + 1 < kMaxDepth && o.max >= 0 && o.max < def.pageCount)
                {
                    state.depth++;
                    state.stack[state.depth] = static_cast<u8>(o.max);
                    enterPage(state, def, o.max);
                    sound = game::kSoundConfirm;
                }
                else if (o.kind == kKindFunc && o.onChange != NULL)
                {
                    s_activeRow = cur;
                    o.onChange();
                    sound = game::kSoundConfirm;
                }
                else if (o.kind == kKindToggle && change(def, o, 1))
                {
                    sound = game::kSoundMove;
                }
            }
            s_rail = isRailStyle(def);
            if (!s_rail)
            {
                state.railFocus = false;
            }
            // If an onChange callback hid, disabled, or changed Menu Style, snap cleanly.
            if (state.stack[state.depth] == pageId && (cur >= p.count || !selectable(p.options[cur])))
            {
                cur = static_cast<u8>(nearestSelectable(p, cur));
            }
        }
        bool top = false;
        if (in.b)
        {
            if (state.depth > 0)
            {
                u8 left = state.stack[state.depth];
                state.cursor[left] = 0;
                state.scroll[left] = 0;
                state.depth--;
                u8 parentId = state.stack[state.depth];
                const Page& parent = pageAt(def, parentId);
                if (parent.count > 0
                    && (state.cursor[parentId] >= parent.count || !selectable(parent.options[state.cursor[parentId]])))
                {
                    state.cursor[parentId] = static_cast<u8>(nearestSelectable(parent, state.cursor[parentId]));
                }
                sound = game::kSoundMove;
            }
            else if (s_rail)
            {
                u8 left = state.stack[0];
                state.cursor[left] = 0;
                state.scroll[left] = 0;
                state.railFocus = true;
                state.railCursor = state.category;
                sound = game::kSoundMove;
            }
            else
            {
                top = true;
            }
        }
        if (sound >= 0)
        {
            menuSound(sound);
        }
        if (top)
        {
            return false;
        }
        u8 shown = state.stack[state.depth];
        s_rail = isRailStyle(def);
        fixScroll(pageAt(def, shown), state.cursor[shown], state.scroll[shown], visibleRowLimit(def, state));
        return true;
    }

    bool menuJump(const MenuDef& def, MenuState& state, const u8* path, int count)
    {
        MenuState next = state;
        s_rail = isRailStyle(def);
        next.depth = 0;
        next.railFocus = false;
        int start = 0;
        if (def.categoryCount > 0)
        {
            u8 cat = 0;
            if (count > 0)
            {
                for (u8 c = 0; c < def.categoryCount; c++)
                {
                    if (def.categoryPages[c] == path[0])
                    {
                        cat = c;
                        start = 1;
                        break;
                    }
                }
            }
            next.category = cat;
            next.railCursor = cat;
            next.stack[0] = def.categoryPages[cat];
            enterPage(next, def, next.stack[0]);
        }
        else
        {
            next.category = 0;
            next.railCursor = 0;
            next.stack[0] = 0;
        }
        for (int k = start; k < count; k++)
        {
            u8 from = next.stack[next.depth];
            const Page& p = pageAt(def, from);
            int row = -1;
            for (int i = 0; i < p.count && row < 0; i++)
            {
                const Option& o = p.options[i];
                if (o.kind == kKindMenu && o.max == path[k])
                {
                    row = i;
                }
            }
            if (row < 0 || !active(p.options[row]) || next.depth + 1 >= kMaxDepth || path[k] >= def.pageCount)
            {
                return false;
            }
            next.cursor[from] = static_cast<u8>(row);
            fixScroll(p, next.cursor[from], next.scroll[from], visibleRowLimit(def, next));
            next.depth++;
            next.stack[next.depth] = path[k];
            enterPage(next, def, path[k]);
        }
        state = next;
        return true;
    }

    // ---- drawing ----------------------------------------------------------

    static bool unavailableLabel(const char* s)
    {
        if (s == NULL)
        {
            return false;
        }
#ifdef __MWERKS__
        if (s[0] == TM_DJC_LABEL[0]) return (g_counterAvailMask & 1) == 0;
        if (s[0] == TM_FLOAT_LABEL[0]) return (g_counterAvailMask & 2) == 0;
#endif
        return s[0] == TM_UNAVAILABLE_LABEL[0];
    }

    static const char* labelText(const char* s)
    {
        if (s != NULL && (s[0] == TM_UNAVAILABLE_LABEL[0] || s[0] == TM_DROPPED_LABEL[0]
#ifdef __MWERKS__
                          || s[0] == TM_DJC_LABEL[0] || s[0] == TM_FLOAT_LABEL[0]
#endif
                          ))
        {
            return s + 1;
        }
        return s;
    }

    // The value column's text, or NULL for none (then no box either).
    static const char* valueText(const MenuDef& def, const Option& o, char* buf, u32 size, bool& greyValue)
    {
        int v = optionValue(def, o);
        greyValue = false;
        switch (o.kind)
        {
        case kKindInt:
            snprintf(buf, size, o.data != NULL ? static_cast<const char*>(o.data) : "%d", v);
            return buf;
        case kKindToggle:
            return v ? "On" : "Off";
        case kKindChoice:
        {
            const char* label = choiceLabel(o, v);
            greyValue = unavailableLabel(label);
            return labelText(label);
        }
        case kKindFunc:
        case kKindInfo:
            return static_cast<const char*>(o.data);
        default:
            return NULL;
        }
    }

#pragma dont_inline on
    struct BevelCols {
        gfx::Rgba top, bot, sheen, edge;
    };
    static const BevelCols kGoldBevel = {kLabGoldTop, kLabGoldBot, kLabGoldSheen, kLabGoldEdge};

    static const MenuPalette& activePalette()
    {
        const main::Settings& s = main::settings();
        return menuPalette(s.size ? s.osd.menuAccent : static_cast<u8>(main::kMenuAccentTeal));
    }

    // Button themes: 0=A green, 1=B red, 2=light silver, 3=Z purple.
    static const gfx::Rgba kBtnTheme[4][3] = {
        {kLabBtnATop, kLabBtnABot, kLabActiveTabText},
        {kLabBtnBTop, kLabBtnBBot, kLabWhiteText},
        {kLabBtnLightTop, kLabBtnLightBot, kLabBtnDarkText},
        {kLabBtnZTop, kLabBtnZBot, kLabWhiteText}
    };

    enum { kNoIcon = 0xFF, kEmblemCentre = 1, kEmblemTab = 2 };

    static void fillBox(float x43, float y, float w43, float h, gfx::Rgba color)
    {
        gfx::fillRect(cx(x43), y, w(w43), h, color);
    }

    static void gradBox(float x43, float y, float w43, float h, gfx::Rgba top, gfx::Rgba bot)
    {
        gfx::fillGradV(cx(x43), y, w(w43), h, top, bot);
    }

    static void bevelBox(float x43, float y, float w43, float h, const BevelCols& c)
    {
        float x = cx(x43);
        float bw = w(w43);
        gfx::fillGradV(x, y, bw, h, c.top, c.bot);
        gfx::fillRect(x, y, bw, 2.0f, c.sheen);
        gfx::fillRect(x, y + h - 2.0f, bw, 2.0f, c.edge);
    }

    static void drawGoldBar(float x43, float w43, float y, float h, float cursorSize)
    {
        bevelBox(x43, y, w43, h, kGoldBevel);
        float midY = y + h * 0.5f;
        float iconH = cursorSize + 2.0f;
        if (!main::extraDrawUiIcon(kIconCursorRight, cx(x43 + 2.0f), midY - iconH * 0.5f, w(iconH), iconH, 0xFFFFFFFF))
        {
            drawCursorTri(cx(x43 + 3.0f), midY, cursorSize);
        }
    }

    static void drawHeaderBand(float bottomY, const MenuPalette& pal)
    {
        float innerW = kLabInnerRight - kLabInnerLeft;
        gradBox(kLabInnerLeft, kLabInnerTop, innerW, bottomY - kLabInnerTop, pal.headerTop, pal.headerBot);
        fillBox(kLabInnerLeft, bottomY, innerW, 2.0f, pal.divider);
    }

    // Squeezes long strings horizontally to fit `maxW` (in 4:3 units).
    static void fitText(float x43, float y, const char* str, gfx::Rgba color, float scale, float maxW, bool centre)
    {
        float width = boldWidth(str, scale);
        float limit = w(maxW);
        float sx = scale;
        if (width > limit && width > 0.0f)
        {
            sx = scale * limit / width;
            width = limit;
        }
        float x = cx(x43);
        boldText(centre ? x - width * 0.5f : x, y, str, color, sx, scale);
    }

    // DisplayFont squeeze helper; when `titled` is true, renders the metallic
    // white-silver gradient + dark edge used on panel headers.
    static void fitDisplay(float x43, float capTop, const char* str, gfx::Rgba col, float cap, float maxW, u8 flags)
    {
        float scale = displayScaleForCap(cap);
        float width = gfx::displayTextWidth(str, scale);
        float limit = w(maxW);
        float sx = scale;
        if (width > limit && width > 0.0f)
        {
            sx = scale * limit / width;
            width = limit;
        }
        float x = cx(x43);
        bool centre = (flags & 1) != 0;
        bool titled = (flags & 2) != 0;
        gfx::displayText(centre ? x - width * 0.5f : x, displayYForCap(capTop, scale), str,
                         titled ? kLabTitleTop : col, titled ? kLabTitleBot : col, sx, scale,
                         titled ? 1.2f : 0.0f, titled ? kLabTitleEdge : 0);
    }

    // Draws a native Nintendo button emblem via extra (single or paired), or
    // falls back to a sculpted glossy GX button badge.
    static float drawEmblem(u8 icon1, u8 icon2, float x, float midY, float fbW, u8 theme, u8 flags, bool& usedIcon)
    {
        float maxH = (flags & kEmblemTab) ? 18.0f : 17.0f;
        float xs = gfx::xScale();
        u8 ids[2] = {icon1, icon2};
        float total = 0.0f;
        for (int k = 0; k < 2 && ids[k] != kNoIcon; k++)
        {
            float asp = uiIconAspect(ids[k]);
            float h = maxH;
            float ww = h * asp;
            if (ww > 26.0f)
            {
                ww = 26.0f;
                h = ww / asp;
            }
            float iw = ww * xs;
            float drawX = (k == 0 && (flags & kEmblemCentre)) ? x - iw * 0.5f : x + total;
            if (!main::extraDrawUiIcon(ids[k], drawX, midY - h * 0.5f, iw, h, 0xFFFFFFFF))
            {
                if (k == 0)
                {
                    float fbH = (flags & kEmblemTab) ? 18.0f : 16.0f;
                    float bx = (flags & kEmblemCentre) ? x - fbW * 0.5f : x;
                    drawGlossyPillBadge(bx, midY - fbH * 0.5f, fbW, fbH, kBtnTheme[theme][0], kBtnTheme[theme][1]);
                    usedIcon = false;
                    return fbW;
                }
                break;
            }
            total += iw + (k == 0 && ids[1] != kNoIcon ? 2.0f * xs : 0.0f);
        }
        usedIcon = true;
        return total;
    }

    static void drawPillBox(float left43, float right43, float y, bool isOn, bool on)
    {
        static const gfx::Rgba kPillCols[4][3] = {
            {kLabPillOffMutedTop, kLabPillOffMutedBot, kLabPillOffSheen},
            {kLabPillOffTop, kLabPillOffBot, kLabPillOffSheen},
            {kLabPillOnMutedTop, kLabPillOnMutedBot, kLabPillOnSheen},
            {kLabPillOnTop, kLabPillOnBot, kLabPillOnSheen}
        };
        const gfx::Rgba* c = kPillCols[(isOn ? 2 : 0) + (on ? 1 : 0)];
        float xs = gfx::xScale();
        float x = 320.0f + (left43 - 320.0f) * xs;
        float pw = (right43 - left43) * xs;
        float pad = 1.5f * xs;
        gfx::fillRect(x, y, pw, kLabRowH, kLabPillBorder);
        gfx::fillGradV(x + pad, y + 2.0f, pw - 2.0f * pad, kLabRowH - 4.0f, c[0], c[1]);
        gfx::fillRect(x + pad, y + 2.0f, pw - 2.0f * pad, 2.0f, c[2]);
    }

    static void drawLabFrame()
    {
        float wPanel = kLabRight - kLabLeft;
        float innerH = kLabBottom - kLabTop - 2.0f * kLabBorder;
        fillBox(kLabLeft, kLabTop, wPanel, kLabBorder, kLabBorderColor);
        fillBox(kLabLeft, kLabBottom - kLabBorder, wPanel, kLabBorder, kLabBorderColor);
        fillBox(kLabLeft, kLabTop + kLabBorder, kLabBorder, innerH, kLabBorderColor);
        fillBox(kLabRight - kLabBorder, kLabTop + kLabBorder, kLabBorder, innerH, kLabBorderColor);
    }

    static void drawLabScrollBar(int count, int scroll, int visible, float topY, float height, const MenuPalette& pal)
    {
        if (count <= visible)
        {
            return;
        }
        float xs = gfx::xScale();
        float left = 320.0f + (kLabScrollLeft - 320.0f) * xs;
        float width = kLabScrollW * xs;
        gfx::fillRect(left, topY, width, height, pal.divider);
        float inner = height - 4.0f;
        float length = inner * visible / count;
        if (length < 14.0f)
        {
            length = 14.0f;
        }
        int maxScroll = count - visible;
        if (scroll > maxScroll)
        {
            scroll = maxScroll;
        }
        float thumbY = topY + 2.0f + (inner - length) * scroll / maxScroll;
        gfx::fillGradV(left + xs, thumbY, width - 2.0f * xs, length, pal.accentTop, pal.accentBot);
    }

    struct CtrlSpec {
        u8 a, b, start, z1, z2, cat1, cat2, tabL, tabR;
        u8 roundBits; // bit 0:start, 1:z, 2:cat, 3:tabR, 4:zPurple
        const char* txtA;
        const char* txtB;
        const char* txtStart;
        const char* txtZ;
        const char* txtCat;
        const char* txtTabL;
        const char* txtTabR;
    };

    static const CtrlSpec& ctrlSpec(u8 ct)
    {
        static const CtrlSpec kSpecs[4] = {
            {kIconGcA, kIconGcB, kIconGcStart, kIconGcZ, kNoIcon, kIconGcL, kIconGcR, kIconGcL, kIconGcR, 0x10,
             "A", "B", "Start", "Z", "L/R", "L", "R"},
            {kIconWii2, kIconWii1, kIconWiiPlus, kIconWiiMinus, kNoIcon, kIconWiiA, kNoIcon, kNoIcon, kIconWiiA, 0x0F,
             "2", "1", "+", "-", "A", NULL, "A"},
            {kIconWiiA, kIconWiiB, kIconWiiPlus, kIconNunZ, kNoIcon, kIconNunC, kIconWii2, kIconNunC, kIconWii2, 0x09,
             "A", "B", "+", "Z", "C/2", "C", "2"},
            {kIconClA, kIconClB, kIconWiiPlus, kIconClZL, kIconClZR, kIconClL, kIconClR, kIconClL, kIconClR, 0x01,
             "A", "B", "+", "ZL/ZR", "L/R", "L", "R"}
        };
        if (ct == gfPadType::WIIMOTE)
        {
            return kSpecs[1];
        }
        if (ct == gfPadType::NUNCHUK)
        {
            return kSpecs[2];
        }
        if (ct == gfPadType::WII_CLASSIC)
        {
            return kSpecs[3];
        }
        return kSpecs[0];
    }

    struct FooterItem {
        u8 icon1;
        u8 icon2;
        u8 theme;
        bool round;
        const char* badge;
        const char* label;
    };

    static void pushFooter(FooterItem* items, int& n, u8 i1, u8 i2, u8 theme, bool round, const char* badge,
                           const char* label)
    {
        FooterItem& it = items[n++];
        it.icon1 = i1;
        it.icon2 = i2;
        it.theme = theme;
        it.round = round;
        it.badge = badge;
        it.label = label;
    }

    static void drawBreadcrumb(const MenuDef& def, const MenuState& state, float x43, float yCenter, float cap,
                               float maxW, const MenuPalette& pal)
    {
        char trail[96];
        size_t pos = 0;
        trail[0] = '\0';
        for (int d = 0; d < state.depth; d++)
        {
            const char* t = pageAt(def, state.stack[d]).title;
            int wrote = snprintf(trail + pos, sizeof(trail) - pos, "%s > ", t);
            if (wrote > 0 && pos + static_cast<size_t>(wrote) < sizeof(trail))
            {
                pos += static_cast<size_t>(wrote);
            }
            else
            {
                break;
            }
        }
        const char* leaf = pageAt(def, state.stack[state.depth]).title;
        float scale = displayScaleForCap(cap);
        float prefixW = trail[0] != '\0' ? gfx::displayTextWidth(trail, scale) : 0.0f;
        float leafW = gfx::displayTextWidth(leaf, scale);
        float total = prefixW + leafW;
        float limit = w(maxW);
        float sx = scale;
        if (total > limit && total > 0.0f)
        {
            sx = scale * limit / total;
            prefixW *= limit / total;
        }
        float ty = displayYForCap(yCenter - cap * 0.5f, scale);
        float x = cx(x43);
        if (trail[0] != '\0')
        {
            gfx::displayText(x, ty, trail, pal.mutedText, pal.mutedText, sx, scale, 0.0f, 0);
        }
        gfx::displayText(x + prefixW, ty, leaf, kLabTitleTop, kLabTitleBot, sx, scale, 1.0f, kLabTitleEdge);
    }

    static void drawHorizArrows(float pillLeft43, float pillRight43, float rowY)
    {
        float xs = gfx::xScale();
        float midY = rowY + kLabRowH * 0.5f;
        float l = 320.0f + (pillLeft43 - 320.0f) * xs;
        float r = 320.0f + (pillRight43 - 320.0f) * xs;
        gfx::fillTri(l + 5.0f * xs, midY, l + 11.5f * xs, midY - 4.5f, l + 11.5f * xs, midY + 4.5f, kLabFocusGold);
        gfx::fillTri(r - 5.0f * xs, midY, r - 11.5f * xs, midY - 4.5f, r - 11.5f * xs, midY + 4.5f, kLabFocusGold);
    }

    static void drawSubmenuChevron(float right43, float rowY, gfx::Rgba color)
    {
        float xs = gfx::xScale();
        float midY = rowY + kLabRowH * 0.5f;
        float r = 320.0f + (right43 - 320.0f) * xs;
        gfx::fillTri(r - 5.0f * xs, midY, r - 11.0f * xs, midY - 4.0f, r - 11.0f * xs, midY + 4.0f, color);
    }

    static void drawDescStrip(const MenuDef& def, const MenuState& state, u8 pageId, int cur, const Page& p,
                              const MenuPalette& pal)
    {
        float iw = kLabInnerRight - kLabInnerLeft;
        fillBox(kLabInnerLeft, kLabDescTop, iw, 2.0f, pal.divider);
        gradBox(kLabInnerLeft, kLabDescTop + 2.0f, iw, kLabFooterTop - (kLabDescTop + 2.0f), pal.descTop,
                pal.descBot);

        const char* desc = NULL;
        if (isRailStyle(def) && state.railFocus)
        {
            desc = main::extraOptionDesc(def.pageCount, state.railCursor);
        }
        else if (p.count > 0 && cur < p.count)
        {
            desc = main::extraOptionDesc(pageId, static_cast<u8>(cur));
#ifndef __MWERKS__
            if (desc == NULL)
            {
                desc = p.options[cur].desc;
            }
#endif
        }
        if (desc == NULL || desc[0] == '\0')
        {
            desc = "Use D-Pad or Stick to select and adjust lab options.";
        }
        float scale = scaleForCap(kLabDescCap);
        float y = textYForCap(kLabDescTop + 2.0f + (kLabFooterTop - kLabDescTop - 2.0f - kLabDescCap) * 0.5f, scale);
        fitText(48.0f, y, desc, pal.descText, scale, 544.0f, false);
    }

    static void drawFooterStrip(const MenuDef& def, const MenuState& state, const Page& p, int cur,
                                const MenuPalette& pal)
    {
        float iw = kLabInnerRight - kLabInnerLeft;
        fillBox(kLabInnerLeft, kLabFooterTop, iw, 2.0f, pal.divider);
        gradBox(kLabInnerLeft, kLabFooterTop + 2.0f, iw, kLabInnerBottom - (kLabFooterTop + 2.0f), kLabFooterTopCol,
                kLabFooterBotCol);

        const CtrlSpec& cs = ctrlSpec(state.controllerType);
        bool railMode = isRailStyle(def);
        s_rail = railMode;
        bool showA = state.railFocus || (p.count > 0 && cur < p.count && active(p.options[cur]));

        const char* aLabel = "Select";
        if (!state.railFocus && p.count > 0 && cur < p.count)
        {
            u8 k = p.options[cur].kind;
            aLabel = k == kKindToggle ? "Toggle" : (k == kKindMenu ? "Open" : "Select");
        }
        const char* bLabel =
            state.depth > 0 ? "Back" : (railMode && !state.railFocus ? "Rail" : (!state.openStart ? "Close" : NULL));

        FooterItem items[5];
        int n = 0;
        if (showA)
        {
            pushFooter(items, n, cs.a, kNoIcon, 0, true, cs.txtA, aLabel);
        }
        if (bLabel != NULL)
        {
            pushFooter(items, n, cs.b, kNoIcon, 1, true, cs.txtB, bLabel);
        }
        if (state.openStart)
        {
            pushFooter(items, n, cs.start, kNoIcon, 2, (cs.roundBits & 1) != 0, cs.txtStart, "Close");
        }
        pushFooter(items, n, cs.z1, cs.z2, (cs.roundBits & 0x10) ? 3 : 2, (cs.roundBits & 2) != 0, cs.txtZ,
                   state.openStart ? "Brawl Pause" : "Close");
        if (def.categoryCount > 1)
        {
            pushFooter(items, n, cs.cat1, cs.cat2, 2, (cs.roundBits & 4) != 0, cs.txtCat,
                       isTabsStyle(def) ? "Tab" : "Category");
        }

        float scale = scaleForCap(kLabFooterCap);
        float footerMidY = kLabFooterTop + 2.0f + (kLabInnerBottom - kLabFooterTop - 4.0f) * 0.5f;
        float textY = textYForCap(footerMidY - kLabFooterCap * 0.5f, scale);
        float xs = gfx::xScale();
        float curX = 320.0f + (48.0f - 320.0f) * xs;
        float minBadgeW = 16.0f * xs;

        float badgeX[5], badgeW[5], labelX[5];
        bool usedIcon[5];
        for (int i = 0; i < n; i++)
        {
            const FooterItem& it = items[i];
            badgeX[i] = curX;
            float fbW = it.round ? minBadgeW : (boldWidth(it.badge, scale) + 10.0f * xs);
            if (fbW < minBadgeW)
            {
                fbW = minBadgeW;
            }
            badgeW[i] = drawEmblem(it.icon1, it.icon2, curX, footerMidY, fbW, it.theme, 0, usedIcon[i]);
            labelX[i] = curX + badgeW[i] + 5.0f * xs;
            curX = labelX[i] + boldWidth(it.label, scale) + 13.0f * xs;
        }
        for (int i = 0; i < n; i++)
        {
            if (!usedIcon[i])
            {
                float bw = boldWidth(items[i].badge, scale);
                boldText(badgeX[i] + (badgeW[i] - bw) * 0.5f, textY, items[i].badge, kBtnTheme[items[i].theme][2],
                         scale, scale);
            }
            boldText(labelX[i], textY, items[i].label, pal.mutedText, scale, scale);
        }
    }

    static void drawRailChrome(const MenuDef& def, const MenuState& state, const MenuPalette& pal)
    {
        float railW = kRailRight - kLabInnerLeft;
        drawHeaderBand(kRailHeaderBottom, pal);

        float bodyH = kLabDescTop - kRailBodyTop;
        gradBox(kLabInnerLeft, kRailBodyTop, railW, bodyH, pal.railTop, pal.railBot);
        fillBox(kRailRight, kRailBodyTop, 1.5f, bodyH, pal.divider);
        gradBox(kRailPaneLeft, kRailBodyTop, kLabInnerRight - kRailPaneLeft, bodyH, pal.baseTop, pal.baseBot);
        fillBox(47.0f, kRailUtilDivY, 141.0f, 2.0f, pal.divider);

        BevelCols accentBevel = {pal.accentTop, pal.accentBot, pal.accentSheen, pal.accentEdge};
        float itemX = kLabInnerLeft + 3.0f;
        float itemW = railW - 6.0f;
        for (u8 i = 0; i < def.categoryCount; i++)
        {
            float y = kRailItemTop + i * kRailItemPitch;
            if (state.railFocus && state.railCursor == i)
            {
                drawGoldBar(itemX, itemW, y, kRailItemH, 8.5f);
            }
            else if (!state.railFocus && state.category == i)
            {
                bevelBox(itemX, y, itemW, kRailItemH, accentBevel);
            }
        }
        for (u8 u = 0; u < def.utilityCount; u++)
        {
            if (state.railFocus && state.railCursor == def.categoryCount + u)
            {
                drawGoldBar(itemX, itemW, kRailUtilTop + u * kRailItemPitch, kRailItemH, 8.5f);
            }
        }

        float headerMid = (kLabInnerTop + kRailHeaderBottom) * 0.5f;
        if (state.depth == 0)
        {
            fitDisplay(48.0f, headerMid - 6.25f, pageAt(def, state.stack[0]).title, 0, 12.5f, 390.0f, 2);
            float subScale = displayScaleForCap(8.5f);
            const char* tag = "Training Lab";
            float tagW = gfx::displayTextWidth(tag, subScale);
            gfx::displayText(cx(592.0f) - tagW, displayYForCap(headerMid - 4.25f, subScale), tag, pal.mutedText,
                             subScale);
        }
        else
        {
            drawBreadcrumb(def, state, 48.0f, headerMid, 10.0f, 540.0f, pal);
        }

        float capOff = (kRailItemH - kRailCap) * 0.5f;
        for (u8 i = 0; i < def.categoryCount; i++)
        {
            bool focused = state.railFocus && state.railCursor == i;
            gfx::Rgba col = focused ? kLabGoldInk
                                    : (!state.railFocus && state.category == i ? pal.activeTabText : pal.mutedText);
            fitDisplay(kRailTextX, kRailItemTop + i * kRailItemPitch + capOff,
                       pageAt(def, def.categoryPages[i]).title, col, kRailCap, kRailTextMaxW, 0);
        }
        for (u8 u = 0; u < def.utilityCount; u++)
        {
            bool confirming = def.utilities[u].label != NULL && def.utilities[u].label[0] != '\0';
            bool focused = state.railFocus && state.railCursor == def.categoryCount + u;
            const char* label = confirming ? def.utilities[u].label : def.utilities[u].name;
            gfx::Rgba col = focused ? (confirming ? 0x7A0C00FF : kLabGoldInk)
                                    : (confirming ? kLabFocusGold : pal.mutedText);
            fitDisplay(kRailTextX, kRailUtilTop + u * kRailItemPitch + capOff, label, col, kRailCap, kRailTextMaxW, 0);
        }
    }

    static void drawTabsChrome(const MenuDef& def, const MenuState& state, const MenuPalette& pal)
    {
        float innerW = kLabInnerRight - kLabInnerLeft;
        drawHeaderBand(kTabsBarBottom, pal);

        float paneTop = kTabsBodyTop;
        if (state.depth > 0)
        {
            gradBox(kLabInnerLeft, kTabsBodyTop, innerW, kTabsCrumbBottom - kTabsBodyTop, pal.descTop,
                    pal.descBot);
            fillBox(kLabInnerLeft, kTabsCrumbBottom, innerW, 2.0f, pal.divider);
            paneTop = kTabsCrumbBottom + 2.0f;
        }
        gradBox(kLabInnerLeft, paneTop, innerW, kLabDescTop - paneTop, pal.baseTop, pal.baseBot);

        // Proportional 7-tab widths spanning x 79..561 (sum 458 + 6*4 gaps = 482),
        // sized for 100% natural Brawl Display Font width at kTabsCap (8.5f).
        static const float kTabWidths[7] = {38.0f, 86.0f, 42.0f, 86.0f, 64.0f, 50.0f, 92.0f};
        float tabY = 33.0f;
        float tabH = 22.0f;
        float tabMidY = tabY + tabH * 0.5f;
        const CtrlSpec& cs = ctrlSpec(state.controllerType);
        bool leftUsedIcon = false;
        bool rightUsedIcon = false;
        if (cs.tabL != kNoIcon)
        {
            drawEmblem(cs.tabL, kNoIcon, cx(59.0f), tabMidY, w(26.0f), 2, kEmblemCentre | kEmblemTab, leftUsedIcon);
        }
        bool rRound = (cs.roundBits & 8) != 0;
        drawEmblem(cs.tabR, kNoIcon, cx(581.0f), tabMidY, w(rRound ? 18.0f : 26.0f), 2, kEmblemCentre | kEmblemTab,
                   rightUsedIcon);

        BevelCols accentBevel = {pal.accentTop, pal.accentBot, pal.accentSheen, pal.accentEdge};
        BevelCols tabBevel = {pal.tabTop, pal.tabBot, pal.bevelLight, pal.bevelDark};
        float tx = 79.0f;
        for (u8 i = 0; i < def.categoryCount && i < 7; i++)
        {
            float tw = kTabWidths[i];
            bevelBox(tx, tabY, tw, tabH, i == state.category ? accentBevel : tabBevel);
            tx += tw + 4.0f;
        }

        float capScale = scaleForCap(9.0f);
        float capY = textYForCap(tabMidY - 4.5f, capScale);
        if (cs.txtTabL != NULL && !leftUsedIcon)
        {
            fitText(59.0f, capY, cs.txtTabL, kLabBtnDarkText, capScale, 24.0f, true);
        }
        if (!rightUsedIcon)
        {
            fitText(581.0f, capY, cs.txtTabR, kLabBtnDarkText, capScale, 24.0f, true);
        }

        float labelTop = tabMidY - kTabsCap * 0.5f;
        tx = 79.0f;
        for (u8 i = 0; i < def.categoryCount && i < 7; i++)
        {
            float tw = kTabWidths[i];
            const char* label = (def.tabLabels != NULL && def.tabLabels[i] != NULL)
                                    ? def.tabLabels[i]
                                    : pageAt(def, def.categoryPages[i]).title;
            gfx::Rgba col = i == state.category ? pal.activeTabText : pal.tabInactiveText;
            fitDisplay(tx + tw * 0.5f, labelTop, label, col, kTabsCap, tw - 6.0f, 1);
            tx += tw + 4.0f;
        }

        if (state.depth > 0)
        {
            drawBreadcrumb(def, state, kTabsNameX, (kTabsBodyTop + kTabsCrumbBottom) * 0.5f, 9.5f, 520.0f, pal);
        }
    }

    static void drawPlainChrome(const Page& p, const MenuPalette& pal)
    {
        drawHeaderBand(kRailHeaderBottom, pal);
        gradBox(kLabInnerLeft, kRailBodyTop, kLabInnerRight - kLabInnerLeft, kLabDescTop - kRailBodyTop, pal.baseTop,
                pal.baseBot);
        fitDisplay(48.0f, (kLabInnerTop + kRailHeaderBottom) * 0.5f - 6.25f, p.title, 0, 12.5f, 540.0f, 2);
    }
#pragma dont_inline reset

    void menuDraw(const MenuDef& def, MenuState& state)
    {
        float scale = scaleForCap(kLabRowCap);
        if (gfx::lineHeight(scale) <= 0.0f)
        {
            return;
        }
        u8 pageId = state.stack[state.depth];
        const Page& p = pageAt(def, pageId);
        int cur = state.cursor[pageId];
        int first = state.scroll[pageId];
        int visRows = visibleRowLimit(def, state);
        bool railMode = isRailStyle(def);
        s_rail = railMode;
        bool tabsMode = isTabsStyle(def);
        char buf[32];

        // Gather visible rows in compacted contiguous order starting at `first`.
        int totalVis = 0;
        int visIdx[kLabVisibleRows];
        int visN = 0;
        for (int i = 0; i < p.count; i++)
        {
            if (hidden(p.options[i]))
            {
                continue;
            }
            if (totalVis >= first && visN < visRows)
            {
                visIdx[visN++] = i;
            }
            totalVis++;
        }

        struct PaneGeom {
            float rowTop, subRowTop, scrollH, subScrollH;
            float hlLeft, hlRight, nameX, nameMaxW;
            float pillLeft, pillRight, valCenter, valMaxW;
        };
        static const PaneGeom kGeom[2] = {
            {kRailRowTop, kRailRowTop, kRailScrollH, kRailScrollH, kRailHlLeft, kRailHlRight, kRailNameX,
             kRailNameMaxW, kRailPillLeft, kRailPillRight, kRailValCenter, kRailValMaxW},
            {kTabsRowTop, kTabsSubRowTop, kTabsScrollH, kTabsSubScrollH, kTabsHlLeft, kTabsHlRight, kTabsNameX,
             kTabsNameMaxW, kTabsPillLeft, kTabsPillRight, kTabsValCenter, kTabsValMaxW}
        };
        const PaneGeom& g = kGeom[tabsMode ? 1 : 0];
        float rowTop = state.depth > 0 ? g.subRowTop : g.rowTop;
        float scrollH = state.depth > 0 ? g.subScrollH : g.scrollH;

        const MenuPalette& pal = activePalette();

        // 1. Outer frame + layout chrome.
        drawLabFrame();
        if (railMode)
        {
            drawRailChrome(def, state, pal);
        }
        else if (tabsMode)
        {
            drawTabsChrome(def, state, pal);
        }
        else
        {
            drawPlainChrome(p, pal);
        }

        // 2. Option row metallic gold bar, Brawl red cursor triangle, inset
        //    beveled value pills, gold < > arrows, submenu chevrons, scrollbar.
        for (int r = 0; r < visN; r++)
        {
            int i = visIdx[r];
            const Option& o = p.options[i];
            float y = rowTop + r * kLabRowPitch;
            bool selected = !state.railFocus && i == cur;
            bool on = active(o);
            if (selected)
            {
                drawGoldBar(g.hlLeft, g.hlRight - g.hlLeft, y, kLabRowH, 9.5f);
            }
            bool grey = false;
            const char* value = valueText(def, o, buf, sizeof(buf), grey);
            if (value != NULL && value[0] != '\0')
            {
                bool isOnToggle = o.kind == kKindToggle && optionValue(def, o) != 0;
                drawPillBox(g.pillLeft, g.pillRight, y, isOnToggle, on);
                if (selected && hasValue(o) && on)
                {
                    drawHorizArrows(g.pillLeft, g.pillRight, y);
                }
            }
            else if (o.kind == kKindMenu)
            {
                gfx::Rgba chevCol = selected ? (on ? kLabGoldInk : kLabGoldInkMuted)
                                             : (on ? pal.mutedText : kTextDisabled);
                drawSubmenuChevron(g.hlRight - 4.0f, y, chevCol);
            }
        }
        drawLabScrollBar(totalVis, first, visRows, rowTop, scrollH, pal);

        // 3. Option row labels and value text (System Font 0x80497E44).
        float capOffset = (kLabRowH - kLabRowCap) * 0.5f;
        for (int r = 0; r < visN; r++)
        {
            int i = visIdx[r];
            const Option& o = p.options[i];
            float y = textYForCap(rowTop + r * kLabRowPitch + capOffset, scale);
            bool on = active(o);
            bool selected = !state.railFocus && i == cur;
            bool grey = false;
            gfx::Rgba nameCol = selected ? (on ? kLabGoldInk : kLabGoldInkMuted) : (on ? kText : kTextDisabled);
            fitText(g.nameX, y, o.name, nameCol, scale, g.nameMaxW, false);
            const char* value = valueText(def, o, buf, sizeof(buf), grey);
            if (value != NULL && value[0] != '\0')
            {
                gfx::Rgba valCol = on && !grey ? kLabWhiteText : kTextDisabled;
                fitText(g.valCenter, y, value, valCol, scale, g.valMaxW, true);
            }
        }

        // 4. Optional page-specific overlay from extra (e.g. Fighter Swap stock icon).
        main::extraDrawPageOverlay(pageId, railMode);

        // 5. Shared 1-line description strip, Nintendo button footer strip, and inner bevels.
        drawDescStrip(def, state, pageId, cur, p, pal);
        drawFooterStrip(def, state, p, cur, pal);
        float iw = kLabInnerRight - kLabInnerLeft;
        fillBox(kLabInnerLeft, kLabInnerTop, iw, 2.0f, pal.bevelLight);
        fillBox(kLabInnerLeft, kLabInnerBottom - 2.0f, iw, 2.0f, pal.bevelDark);
    }
}
}
