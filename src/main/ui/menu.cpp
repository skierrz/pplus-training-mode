#include "main/ui/menu.h"

#include <stddef.h>
#include <cstdio>
#include <string.h>
#include <gf/gf_pad_status.h>

#include "main/cpu/lab_data.h"
#include "main/cpu/options.h"
#include "main/extra.h"
#include "main/game/code_menu.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/lab.h"
#include "main/record/record.h"
#include "main/settings.h"
#include "main/ui/menu_theme_core.h"
#include "main/ui/pages/olimar.h"
#include "main/ui/pages/pages.h"
#include "main/ui/pages/scenarios.h"
#include "main/ui/style.h"
#include "shared/game/menu_sound.h"
#include "shared/log.h"

namespace tm {
namespace ui {
    using namespace style;

    static int s_activeRow = 0;
    static u8 s_scratch[kScratchBytes];
    static bool s_rail = false;
    static bool s_allowShowInactive = false;
    bool g_menuQuiet = false;
    bool g_showInactiveRows = false;

    static bool allowShowInactive(const MenuDef& def, int pageId)
    {
        return g_showInactiveRows && (def.categoryCount == 0 || pageId == pages::P_STAGE_CAST);
    }
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
        return (def.categoryCount > 0 && state.depth > 0) ? kLabSubVisibleRows : kLabVisibleRows;
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
        return !s_allowShowInactive && (o.flags & kFlagHideWhenDisabled) != 0 && o.enabled != NULL && !o.enabled();
    }

    static bool selectable(const Option& o)
    {
        if (hidden(o))
        {
            return false;
        }
        bool disabled = o.enabled != NULL && !o.enabled();
        if (disabled && !(s_allowShowInactive && (o.flags & kFlagHideWhenDisabled) != 0))
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
        if (id == pages::P_OLIMAR)
        {
            pages::olimarSyncFromSettings();
        }
        s_rail = isRailStyle(def);
        s_allowShowInactive = allowShowInactive(def, id);
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
        state.resetConfirmPage = 0;
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
        state.resetConfirmPage = 0;
        enterPage(state, def, pageId);
    }

    static bool isLabMenu(const MenuDef& def)
    {
        return def.values != NULL && def.values() == reinterpret_cast<u8*>(&main::settings());
    }

    static bool resettableOption(const Option& o)
    {
        return stored(o) && (o.flags & kFlagScratch) == 0 && active(o);
    }

#pragma dont_inline on
    static bool pageHasResettableOptions(const MenuDef& def, int pageId)
    {
        if (static_cast<u32>(pageId) >= def.pageCount)
        {
            return false;
        }
        if (pageId == pages::P_CPU_COUNTERS && isLabMenu(def)
            && main::settings().cpu.options.control == cpu::kControlLab)
        {
            return true;
        }
        bool savedRail = s_rail;
        bool savedAllow = s_allowShowInactive;
        s_rail = isRailStyle(def);
        s_allowShowInactive = allowShowInactive(def, pageId);
        const Page& p = pageAt(def, pageId);
        bool found = false;
        for (int i = 0; i < p.count; i++)
        {
            if (resettableOption(p.options[i]))
            {
                found = true;
                break;
            }
        }
        s_rail = savedRail;
        s_allowShowInactive = savedAllow;
        return found;
    }

    static bool resetMenuOption(const MenuDef& def, int pageId, int row)
    {
        const Page& p = pageAt(def, pageId);
        if (row < 0 || row >= p.count)
        {
            return false;
        }
        const Option& o = p.options[row];
        if (!resettableOption(o))
        {
            return false;
        }
        bool ok = isLabMenu(def) ? main::resetSettingsOption(pageId, row) : defaultOption(def, o);
        if (ok && o.onChange != NULL)
        {
            s_activeRow = row;
            o.onChange();
        }
        return ok;
    }

    static int stepPageReset(const MenuDef& def, MenuState& state, u8 targetPg, bool byOpt)
    {
        if (!pageHasResettableOptions(def, targetPg))
        {
            state.resetConfirmPage = 0;
            return -1;
        }
        if ((state.resetConfirmPage & 0x7Fu) != static_cast<u8>(targetPg + 1))
        {
            state.resetConfirmPage = static_cast<u8>((targetPg + 1) | (byOpt ? 0x80u : 0u));
            return game::kSoundMove;
        }
        state.resetConfirmPage = 0;
        if (isLabMenu(def))
        {
            if (!main::resetSettingsPage(targetPg))
            {
                return -1;
            }
            switch (targetPg)
            {
            case pages::P_GENERAL:
                lab::frameAdvanceChanged();
                lab::applyPercent(main::kSideHuman);
                break;
            case pages::P_CONTROLS:
                game::codeMenuSync();
                break;
            case pages::P_CPU:
                lab::applyPercent(main::kSideCpu);
                cpu::controlledByChanged();
                break;
            case pages::P_CPU_COUNTERS:
                main::settings().cpu.options.multiReversal = 0;
                s_scratch[kCounterModeScratchOffset] = 0xFFu;
                // fall through
            case pages::P_ADVANCED_COUNTER:
                cpu::advancedHitsDefaults(main::settings().cpuTables);
                cpu::advancedHitChanged();
                break;
            case pages::P_OSD_MENU:
                lab::osdSettingsChanged();
                break;
            case pages::P_STAGE_CAST:
                main::extraStageOptionsChanged();
                main::extraCharacterRng();
                break;
            case pages::P_OLIMAR:
            {
                main::FighterMechanicsSettings& fm = main::settings().fighterMechanics;
                fm.olimarPikmin = 0;
                fm.olimarMaturity = 0;
                fm.olimarPullAndWhistle = main::kOlimarPullPoolAll;
                fm.olimarColorMatAndMode = 0;
                fm.olimarStartLineup = 0;
                fm.olimarDpadPresets = 0;
                pages::olimarSyncFromSettings();
                main::extraCharacterRng();
                break;
            }
            case pages::P_RECORDING:
                if (record::positionsSaved())
                {
                    record::hmnModeChanged();
                    record::cpuModeChanged();
                    record::mirroredChanged();
                }
                // fall through
            case pages::P_SLOTS:
                record::clearDeleteConfirm();
                break;
            case pages::P_HMN_CHANCES:
            case pages::P_CPU_CHANCES:
                record::chanceChanged(targetPg == pages::P_HMN_CHANCES ? main::kSideHuman : main::kSideCpu, -1);
                break;
            default:
                break;
            }
            return game::kSoundConfirm;
        }
        const Page& p = pageAt(def, targetPg);
        bool any = false;
        for (int i = 0; i < p.count; i++)
        {
            const Option& o = p.options[i];
            if (!stored(o) || (o.flags & kFlagScratch) != 0)
            {
                continue;
            }
            int before = optionValue(def, o);
            if (defaultOption(def, o))
            {
                any = true;
                if (o.onChange != NULL && optionValue(def, o) != before)
                {
                    s_activeRow = i;
                    o.onChange();
                }
            }
        }
        return any ? game::kSoundConfirm : -1;
    }
#pragma dont_inline reset

    static bool isDamageOption(int pageId, const Option& o)
    {
        return o.kind == kKindInt && (o.flags & kFlagScratch) == 0
               && ((pageId == pages::P_GENERAL && o.offset == offsetof(main::Settings, general.playerPercent))
                   || (pageId == pages::P_CPU && o.offset == offsetof(main::Settings, cpu.percent)));
    }

#pragma dont_inline on
    // Shared active-override / training-aid highlight predicate across Toggle,
    // Choice, and Int: vanilla-P+ ON/default options stay neutral at vanilla and
    // highlight on deviation; active training aids/overrides highlight whenever
    // enabled (!= Off / != None), even when active by default.
    static bool optionHighlighted(const MenuDef& def, int pageId, const Option& o)
    {
        if (!hasValue(o) || (o.flags & kFlagUnavailable) != 0)
        {
            return false;
        }
        int v = optionValue(def, o);
        if (!isLabMenu(def))
        {
            return v != o.def;
        }
        if ((o.flags & kFlagScratch) != 0)
        {
            if (pageId == pages::P_CPU_COUNTERS && o.offset == kCounterModeScratchOffset)
            {
                return v != 0;
            }
            return v != o.def;
        }
        u16 off = o.offset;
        if (off == offsetof(main::Settings, general.playerInfiniteShields)
            || off == offsetof(main::Settings, general.ledgeHud)
            || off == offsetof(main::Settings, cpu.options.infiniteShields)
            || off == offsetof(main::Settings, cpu.options.counterGround)
            || off == offsetof(main::Settings, cpu.options.counterAir)
            || off == offsetof(main::Settings, cpu.options.counterShield)
            || off == offsetof(main::Settings, osd.techSparkle)
            || (pageId == pages::P_OSD_MENU && o.kind == kKindToggle))
        {
            return v != 0;
        }
        if (off == offsetof(main::Settings, cpu.options.tdi))
        {
            return v != cpu::kTdiNone;
        }
        if (off == offsetof(main::Settings, display.diVectorMode))
        {
            return v != main::kDiVectorNone;
        }
        return v != o.def;
    }

    static bool pageHasOverride(const MenuDef& def, int pageId, u8 depth = 0)
    {
        if (static_cast<u32>(pageId) >= def.pageCount || depth >= 3)
        {
            return false;
        }
        bool savedRail = s_rail;
        bool savedAllow = s_allowShowInactive;
        s_rail = isRailStyle(def);
        s_allowShowInactive = allowShowInactive(def, pageId);
        const Page& p = pageAt(def, pageId);
        bool found = false;
        for (int i = 0; i < p.count; i++)
        {
            const Option& o = p.options[i];
            if (!active(o))
            {
                continue;
            }
            if (o.kind == kKindMenu)
            {
                if (o.max != pageId && pageHasOverride(def, o.max, static_cast<u8>(depth + 1)))
                {
                    found = true;
                    break;
                }
            }
            else if (optionHighlighted(def, pageId, o))
            {
                found = true;
                break;
            }
        }
        s_rail = savedRail;
        s_allowShowInactive = savedAllow;
        return found;
    }
#pragma dont_inline reset

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

    // True if the value changed. When `wrap` is true on kKindChoice, cycles
    // forward wrapping back to 0 while skipping dropped/unavailable choices.
    static bool change(const MenuDef& def, const Option& o, int delta, bool wrap = false)
    {
        int v = optionValue(def, o);
        int next = o.kind == kKindToggle ? !v : clampTo(o, v + delta);
        if (o.kind == kKindChoice && delta != 0)
        {
            if (wrap)
            {
                int count = o.max + 1;
                next = v;
                if (count > 1)
                {
                    int base = (v >= 0 && v < count) ? v : 0;
                    for (int s = 1; s < count; s++)
                    {
                        int cand = (base + s) % count;
                        const char* lbl = choiceLabel(o, cand);
                        if (!droppedLabel(lbl) && !unavailableLabel(lbl))
                        {
                            next = cand;
                            break;
                        }
                    }
                }
            }
            else
            {
                // Step over retired or unavailable choices.
                int step = delta > 0 ? 1 : -1;
                const char* lbl;
                while (next != v && (lbl = choiceLabel(o, next)) != NULL
                       && (droppedLabel(lbl) || unavailableLabel(lbl)))
                {
                    int further = clampTo(o, next + step);
                    next = further == next ? v : further;
                }
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
        return optionValue(def, o) != v;
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
            if (state.resetConfirmPage != 0 && in.b)
            {
                state.resetConfirmPage = 0;
                menuSound(game::kSoundMove);
                return true;
            }
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
                    state.resetConfirmPage = 0;
                    sound = game::kSoundMove;
                    if (state.railCursor < def.categoryCount)
                    {
                        switchCategory(def, state, state.railCursor, true);
                    }
                }
                else if (in.up || in.down)
                {
                    state.resetConfirmPage = 0;
                }
                if (in.a)
                {
                    state.resetConfirmPage = 0;
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
                else if ((in.resetOpt || in.resetPage) && !in.up && !in.down)
                {
                    bool hasPageBtn =
                        state.controllerType == gfPadType::GCC || state.controllerType == gfPadType::WII_CLASSIC;
                    bool fireReset = hasPageBtn ? in.resetPage : (in.resetPage || in.resetOpt);
                    if (fireReset && state.railCursor < def.categoryCount)
                    {
                        sound = stepPageReset(def, state, def.categoryPages[state.railCursor], !hasPageBtn);
                    }
                    else
                    {
                        state.resetConfirmPage = 0;
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
            s_allowShowInactive = allowShowInactive(def, shown);
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

        if (state.resetConfirmPage != 0 && in.b)
        {
            state.resetConfirmPage = 0;
            menuSound(game::kSoundMove);
            return true;
        }

        u8 pageId = state.stack[state.depth];
        s_allowShowInactive = allowShowInactive(def, pageId);
        const Page& p = pageAt(def, pageId);
        u8& cur = state.cursor[pageId];
        bool otherInput = in.up != 0 || in.down != 0 || in.left != 0 || in.right != 0 || in.a;
        if (otherInput)
        {
            state.resetConfirmPage = 0;
        }
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
                else if ((o.kind == kKindToggle || o.kind == kKindChoice) && change(def, o, 1, o.kind == kKindChoice))
                {
                    sound = game::kSoundMove;
                }
                else if (isDamageOption(pageId, o))
                {
                    setOptionValue(def, o, 0);
                    if (o.onChange != NULL)
                    {
                        o.onChange();
                    }
                    sound = game::kSoundConfirm;
                }
            }
            else if (!otherInput && !in.b)
            {
                if (in.resetOpt)
                {
                    state.resetConfirmPage = 0;
                    if (resetMenuOption(def, pageId, cur))
                    {
                        sound = game::kSoundConfirm;
                    }
                }
                else if (in.resetPage)
                {
                    sound = stepPageReset(def, state, pageId, false);
                }
            }
            s_rail = isRailStyle(def);
            s_allowShowInactive = allowShowInactive(def, pageId);
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
                s_allowShowInactive = allowShowInactive(def, parentId);
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
        s_allowShowInactive = allowShowInactive(def, shown);
        fixScroll(pageAt(def, shown), state.cursor[shown], state.scroll[shown], visibleRowLimit(def, state));
        return true;
    }

    bool menuJump(const MenuDef& def, MenuState& state, const u8* path, int count)
    {
        MenuState next = state;
        s_rail = isRailStyle(def);
        next.depth = 0;
        next.railFocus = false;
        next.resetConfirmPage = 0;
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
            s_allowShowInactive = allowShowInactive(def, from);
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

    static const MenuPalette& activePalette()
    {
        const main::Settings& s = main::settings();
        return menuPalette(s.size ? s.osd.menuAccent : static_cast<u8>(main::kMenuAccentTeal));
    }

    static void fillBox(float x43, float y, float w43, float h, gfx::Rgba color)
    {
        float ms = menuScale();
        gfx::fillRect(320.0f + (x43 - 320.0f) * ms, y, w43 * ms, h, color);
    }

    static void gradBox(float x43, float y, float w43, float h, gfx::Rgba top, gfx::Rgba bot)
    {
        float ms = menuScale();
        gfx::fillGradV(320.0f + (x43 - 320.0f) * ms, y, w43 * ms, h, top, bot);
    }

    static void bevelBox(float x43, float y, float w43, float h, const BevelCols& c)
    {
        float ms = menuScale();
        drawBevelBar(320.0f + (x43 - 320.0f) * ms, y, w43 * ms, h, c.top, c.bot, c.sheen, c.edge);
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
        float ms = menuScale();
        drawFitText(320.0f + (x43 - 320.0f) * ms, y, str, color, scale, maxW * ms, centre);
    }

    // DisplayFont squeeze helper; when `titled` is true, renders the metallic
    // white-silver gradient + dark edge used on panel headers.
    static void fitDisplay(float x43, float capTop, const char* str, gfx::Rgba col, float cap, float maxW, u8 flags)
    {
        float ms = menuScale();
        float scale = displayScaleForCap(cap);
        float width = gfx::displayTextWidth(str, scale);
        float limit = maxW * ms;
        float sx = scale;
        if (width > limit && width > 0.0f)
        {
            sx = scale * limit / width;
            width = limit;
        }
        float x = 320.0f + (x43 - 320.0f) * ms;
        bool centre = (flags & 1) != 0;
        bool titled = (flags & 2) != 0;
        gfx::displayText(centre ? x - width * 0.5f : x, displayYForCap(capTop, scale), str,
                         titled ? kLabTitleTop : col, titled ? kLabTitleBot : col, sx, scale,
                         titled ? 1.2f : 0.0f, titled ? kLabTitleEdge : 0);
    }

    static void drawLabScrollBar(int count, int scroll, int visible, float topY, float height, const MenuPalette& pal)
    {
        drawScrollBarAt(kLabScrollLeft, topY, height, count, scroll, visible, pal.divider, pal.accentTop,
                        pal.accentBot);
    }

    struct CtrlSpec {
        u8 roundBits; // bit 0:start, 1:z, 2:cat, 3:tabR, 4:zPurple, 5:resetRound
        const char* txtA;
        const char* txtB;
        const char* txtStart;
        const char* txtZ;
        const char* txtCat;
        const char* txtTabL;
        const char* txtTabR;
        const char* txtResetOpt;
        const char* txtResetPage;
    };

    static const CtrlSpec& ctrlSpec(u8 ct)
    {
        static const CtrlSpec kSpecs[4] = {
            {0x30, "A", "B", "START", "Z", "L/R", "L", "R", "X", "Y"},
            {0x0F, "2", "1", "+", "-", "A", NULL, "A", NULL, NULL},
            {0x29, "A", "B", "+", "Z", "C/2", "C", "2", "-", NULL},
            {0x21, "A", "B", "+", "ZL/ZR", "L/R", "L", "R", "X", "Y"}
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
        u8 theme;
        bool round;
        const char* badge;
        const char* label;
    };

    static void pushFooter(FooterItem* items, int& n, u8 theme, bool round, const char* badge, const char* label)
    {
        FooterItem& it = items[n++];
        it.theme = theme;
        it.round = round;
        it.badge = badge;
        it.label = label;
    }

    static const char* pageTitle(const MenuDef& def, int id)
    {
        if (id == pages::P_SCENARIOS)
        {
            if (pages::practiceMenuState()->activeDrill != 0)
            {
                return "Drill";
            }
            if (pages::scenarioMenuState()->preset != pages::kPresetOff)
            {
                return "Scenario";
            }
        }
        return pageAt(def, id).title;
    }

    static void drawBreadcrumb(const MenuDef& def, const MenuState& state, float x43, float yCenter, float cap,
                               float maxW, const MenuPalette& pal, bool onRibbon = false)
    {
        char trail[96];
        size_t pos = 0;
        trail[0] = '\0';
        for (int d = 0; d < state.depth; d++)
        {
            const char* t = pageTitle(def, state.stack[d]);
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
        const char* leaf = pageTitle(def, state.stack[state.depth]);
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
        if (onRibbon)
        {
            if (trail[0] != '\0')
            {
                gfx::displayText(x, ty, trail, pal.activeTabText, pal.activeTabText, sx, scale, 0.0f, 0);
            }
            gfx::displayText(x + prefixW, ty, leaf, pal.activeTabText, pal.activeTabText, sx, scale, 0.0f, 0);
        }
        else
        {
            if (trail[0] != '\0')
            {
                gfx::displayText(x, ty, trail, pal.mutedText, pal.mutedText, sx, scale, 0.0f, 0);
            }
            gfx::displayText(x + prefixW, ty, leaf, kLabTitleTop, kLabTitleBot, sx, scale, 1.0f, kLabTitleEdge);
        }
    }

    static void drawSubmenuChevron(float right43, float rowY, gfx::Rgba color)
    {
        float midY = rowY + kLabRowH * 0.5f;
        float r = cx(right43);
        gfx::fillTri(r - gw(5.0f), midY, r - gw(11.0f), midY - 4.0f, r - gw(11.0f), midY + 4.0f, color);
    }

    static void drawOverrideDot(float x43, float cy, float size, bool onGold, bool onAccent, const MenuPalette& pal)
    {
        gfx::Rgba col = onGold ? kLabGoldInk : (onAccent ? pal.activeTabText : pal.accentTop);
        float sw = gw(size);
        gfx::fillRect(cx(x43) - sw * 0.5f, cy - size * 0.5f, sw, size, col);
    }

    static bool diPickerActiveFor(u8 pageId, const MenuState& state)
    {
        return pageId == pages::P_CPU_DI && !state.railFocus && s_scratch[kDiPickActiveScratchOffset] != 0;
    }

    static bool railUtilityFocused(const MenuDef& def, const MenuState& state)
    {
        return isRailStyle(def) && state.railFocus && state.railCursor >= def.categoryCount
               && state.railCursor < def.categoryCount + def.utilityCount;
    }

    static void drawDescStrip(const MenuDef& def, const MenuState& state, u8 pageId, int cur, const Page& p,
                              const MenuPalette& pal)
    {
        float iw = kLabInnerRight - kLabInnerLeft;
        fillBox(kLabInnerLeft, kLabDescTop, iw, 2.0f, pal.divider);
        gradBox(kLabInnerLeft, kLabDescTop + 2.0f, iw, kLabFooterTop - (kLabDescTop + 2.0f), pal.descTop,
                pal.descBot);

        float scale = scaleForCap(kLabDescCap);
        float y = textYForCap(kLabDescTop + 2.0f + (kLabFooterTop - kLabDescTop - 2.0f - kLabDescCap) * 0.5f, scale);
        if (state.resetConfirmPage != 0)
        {
            u8 confirmPg = static_cast<u8>((state.resetConfirmPage & 0x7Fu) - 1);
            if (confirmPg < def.pageCount)
            {
                const CtrlSpec& cs = ctrlSpec(state.controllerType);
                bool byOpt = (state.resetConfirmPage & 0x80u) != 0;
                const char* btn = (byOpt || cs.txtResetPage == NULL) ? cs.txtResetOpt : cs.txtResetPage;
                if (btn == NULL)
                {
                    btn = "Y";
                }
                char confirmBuf[96];
                snprintf(confirmBuf, sizeof(confirmBuf), "Press %s again to reset %s to defaults.", btn,
                         pageTitle(def, confirmPg));
                fitText(kLabInnerLeft + kFibPad, y, confirmBuf, kLabFocusGold, scale, 536.0f, false);
                return;
            }
        }

        const char* desc = NULL;
        if (diPickerActiveFor(pageId, state))
        {
            desc = "Tilt control stick to aim DI, then press A to lock or B to cancel.";
        }
        else if (isRailStyle(def) && state.railFocus)
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
        fitText(kLabInnerLeft + kFibPad, y, desc, pal.descText, scale, 536.0f, false);
    }

    static void drawFooterStrip(const MenuDef& def, const MenuState& state, u8 pageId, const Page& p, int cur,
                                const MenuPalette& pal)
    {
        float iw = kLabInnerRight - kLabInnerLeft;
        fillBox(kLabInnerLeft, kLabFooterTop, iw, 2.0f, pal.divider);
        gradBox(kLabInnerLeft, kLabFooterTop + 2.0f, iw, kLabInnerBottom - (kLabFooterTop + 2.0f), kLabFooterTopCol,
                kLabFooterBotCol);

        const CtrlSpec& cs = ctrlSpec(state.controllerType);
        bool railMode = isRailStyle(def);
        s_rail = railMode;
        s_allowShowInactive = allowShowInactive(def, pageId);
        bool diPicking = diPickerActiveFor(pageId, state);

        FooterItem items[7];
        int n = 0;
        if (diPicking)
        {
            pushFooter(items, n, 0, true, cs.txtA, "Lock Angle");
            pushFooter(items, n, 1, true, cs.txtB, "Cancel");
        }
        else if (state.resetConfirmPage != 0)
        {
            bool byOpt = (state.resetConfirmPage & 0x80u) != 0;
            const char* txt = (byOpt || cs.txtResetPage == NULL) ? cs.txtResetOpt : cs.txtResetPage;
            if (txt == NULL)
            {
                txt = "Y";
            }
            pushFooter(items, n, 2, (cs.roundBits & 0x20) != 0, txt, "Confirm Reset");
            pushFooter(items, n, 1, true, cs.txtB, "Cancel");
        }
        else
        {
            bool showA = state.railFocus || (p.count > 0 && cur < p.count && active(p.options[cur]));
            const char* aLabel = "Select";
            if (showA && !state.railFocus)
            {
                const Option& o = p.options[cur];
                if (isDamageOption(pageId, o))
                {
                    aLabel = "Reset 0%";
                }
                else if (o.kind == kKindToggle)
                {
                    aLabel = "Toggle";
                }
                else if (o.kind == kKindChoice)
                {
                    aLabel = "Cycle";
                }
                else if (o.kind == kKindMenu)
                {
                    aLabel = "Open";
                }
                else if (o.kind == kKindInfo || o.kind == kKindInt)
                {
                    showA = false;
                }
            }
            const char* bLabel =
                state.depth > 0
                    ? "Back"
                    : (railMode ? (state.railFocus ? "Close" : "Categories")
                                : (!state.openStart ? "Close" : NULL));

            if (showA)
            {
                pushFooter(items, n, 0, true, cs.txtA, aLabel);
            }
            if (bLabel != NULL)
            {
                pushFooter(items, n, 1, true, cs.txtB, bLabel);
            }
            if (state.openStart)
            {
                pushFooter(items, n, 2, (cs.roundBits & 1) != 0, cs.txtStart, "Close");
            }
            pushFooter(items, n, (cs.roundBits & 0x10) ? 3 : 2, (cs.roundBits & 2) != 0, cs.txtZ,
                       state.openStart ? "Brawl Pause" : "Close");
            if (def.categoryCount > 1)
            {
                pushFooter(items, n, 2, (cs.roundBits & 4) != 0, cs.txtCat,
                           isTabsStyle(def) ? "Tab" : "Category");
            }
            bool resetRound = (cs.roundBits & 0x20) != 0;
            if (railMode && state.railFocus)
            {
                const char* railResetTxt = cs.txtResetPage != NULL ? cs.txtResetPage : cs.txtResetOpt;
                if (state.railCursor < def.categoryCount && railResetTxt != NULL
                    && pageHasResettableOptions(def, def.categoryPages[state.railCursor]))
                {
                    pushFooter(items, n, 2, resetRound, railResetTxt, "Reset Page");
                }
            }
            else
            {
                if (cs.txtResetOpt != NULL && p.count > 0 && cur < p.count && resettableOption(p.options[cur])
                    && optionValue(def, p.options[cur]) != p.options[cur].def)
                {
                    pushFooter(items, n, 2, resetRound, cs.txtResetOpt, "Reset");
                }
                if (cs.txtResetPage != NULL && pageHasResettableOptions(def, pageId))
                {
                    pushFooter(items, n, 2, resetRound, cs.txtResetPage, "Reset Page");
                }
            }
        }

        bool dense = n >= 6;
        float footerMidY = kLabFooterTop + 2.0f + (kLabInnerBottom - kLabFooterTop - 4.0f) * 0.5f;
        float curX = cx(dense ? (kLabInnerLeft + kFibTight) : (kLabInnerLeft + kFibPad));
        float badgeH = dense ? 14.0f : 16.0f;
        u8 baseFlags = static_cast<u8>(kBadgeTightGap | (dense ? kBadgeDense : 0));
        for (int i = 0; i < n; i++)
        {
            const FooterItem& it = items[i];
            curX = drawFooterBadge(curX, footerMidY, badgeH, it.badge, it.label, it.theme,
                                   static_cast<u8>(baseFlags | (it.round ? kBadgeRound : 0)), pal.mutedText);
        }
    }

    static void drawRailChrome(const MenuDef& def, const MenuState& state, const MenuPalette& pal)
    {
        float railW = kRailRight - kLabInnerLeft;
        float paneW = kLabInnerRight - kRailPaneLeft;
        drawHeaderBand(kRailHeaderBottom, pal);

        float bodyH = kLabDescTop - kRailBodyTop;
        gradBox(kLabInnerLeft, kRailBodyTop, railW, bodyH, pal.railTop, pal.railBot);
        fillBox(kRailRight, kRailBodyTop, kFibHair, bodyH, pal.divider);
        gradBox(kRailPaneLeft, kRailBodyTop, paneW, bodyH, pal.baseTop, pal.baseBot);
        fillBox(kLabInnerLeft + kFibGap, kRailUtilDivY, railW - 2.0f * kFibGap, kFibHair, pal.divider);

        // Flush vertical left edge on top emerald-teal header ribbon + // stripes.
        // On subpages, widen the ribbon and draw the breadcrumb inline (matching 03-mockup-lab-menu).
        bool utilFoc = railUtilityFocused(def, state);
        float hdrH = kRailHeaderBottom - kLabInnerTop;
        float ribbonRight = (!utilFoc && state.depth > 0) ? 472.0f : 292.0f;
        drawHeaderRibbon(kLabInnerLeft, ribbonRight, kLabInnerTop, hdrH, pal.accentTop, pal.accentBot);

        // Flush vertical left edge on active left-rail category/utility bar (slanted right edge).
        float itemRight = kLabInnerLeft + railW - kFibTight;
        for (u8 i = 0; i < def.categoryCount; i++)
        {
            float y = kRailItemTop + i * kRailItemPitch;
            bool focused = state.railFocus && state.railCursor == i;
            bool activeCat = !state.railFocus && state.category == i;
            if (focused)
            {
                drawGoldBar(kLabInnerLeft, itemRight, y, kRailItemH, 8.0f, true, itemRight - kFibTight);
            }
            else if (activeCat)
            {
                drawSlantBar(kLabInnerLeft, itemRight, y, kRailItemH, true, pal.accentTop, pal.accentBot, 0);
            }
            if (pageHasOverride(def, def.categoryPages[i]))
            {
                drawOverrideDot(166.0f, y + kRailItemH * 0.5f, 5.0f, focused, activeCat, pal);
            }
        }
        for (u8 u = 0; u < def.utilityCount; u++)
        {
            if (state.railFocus && state.railCursor == def.categoryCount + u)
            {
                drawGoldBar(kLabInnerLeft, itemRight, kRailUtilTop + u * kRailItemPitch, kRailItemH, 8.0f, true,
                            itemRight - kFibTight);
            }
        }

        float headerMid = (kLabInnerTop + kRailHeaderBottom) * 0.5f;
        float ribbonMaxW = ribbonRight - (kLabInnerLeft + kFibPad) - 30.0f;
        if (!utilFoc && state.depth > 0)
        {
            float crumbCap = state.depth >= 2 ? 11.0f : kTypeHeading;
            drawBreadcrumb(def, state, kLabInnerLeft + kFibPad, headerMid, crumbCap, ribbonMaxW, pal, true);
        }
        else
        {
            const char* hdr = utilFoc ? def.utilities[state.railCursor - def.categoryCount].name
                                      : pageTitle(def, state.stack[0]);
            fitDisplay(kLabInnerLeft + kFibPad, headerMid - kTypeHeading * 0.5f, hdr, pal.activeTabText, kTypeHeading,
                       ribbonMaxW, 0);
        }
        float verScale = scaleForCap(kTypeMicro);
        float verW = boldWidth(TM_VERSION_MARK, verScale);
        boldText(cx(kLabInnerRight - kFibGap) - verW, textYForCap(headerMid - kTypeMicro * 0.5f, verScale),
                 TM_VERSION_MARK, pal.mutedText, verScale, verScale);

        // Crisp Rodin system typography on left rail (matching 03-mockup-lab-menu-pplus).
        float railScale = scaleForCap(kRailCap);
        float capOff = (kRailItemH - kRailCap) * 0.5f;
        for (u8 i = 0; i < def.categoryCount; i++)
        {
            bool focused = state.railFocus && state.railCursor == i;
            gfx::Rgba col = focused ? kLabGoldInk
                                    : (!state.railFocus && state.category == i ? pal.activeTabText : pal.tabInactiveText);
            fitText(kRailTextX, textYForCap(kRailItemTop + i * kRailItemPitch + capOff, railScale),
                    pageTitle(def, def.categoryPages[i]), col, railScale, 102.0f, false);
        }
        for (u8 u = 0; u < def.utilityCount; u++)
        {
            bool confirming = def.utilities[u].label != NULL && def.utilities[u].label[0] != '\0';
            bool focused = state.railFocus && state.railCursor == def.categoryCount + u;
            const char* label = confirming ? def.utilities[u].label : def.utilities[u].name;
            gfx::Rgba col = focused ? (confirming ? kLabConfirmInkOnGold : kLabGoldInk)
                                    : (confirming ? kLabFocusGold : pal.mutedText);
            fitText(kRailTextX, textYForCap(kRailUtilTop + u * kRailItemPitch + capOff, railScale), label, col,
                    railScale, kRailTextMaxW, false);
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
            fillBox(kLabInnerLeft, kTabsCrumbBottom, innerW, kFibHair, pal.divider);
            paneTop = kLabSubBodyTop;
        }
        gradBox(kLabInnerLeft, paneTop, innerW, kLabDescTop - paneTop, pal.baseTop, pal.baseBot);

        // Proportional 7-tab widths spanning x 76..564 (sum 458 + 6*5 gaps = 488),
        // sized for 100% natural Brawl Display Font width at kTabsCap (9.0f).
        static const float kTabWidths[7] = {38.0f, 86.0f, 40.0f, 86.0f, 66.0f, 50.0f, 92.0f};
        float tabY = 36.0f;
        float tabH = kFibRowH;
        float tabMidY = tabY + tabH * 0.5f;
        const CtrlSpec& cs = ctrlSpec(state.controllerType);
        float pillW24 = gw(24.0f);
        if (cs.txtTabL != NULL)
        {
            drawGlossyPillBadge(cx(57.5f) - pillW24 * 0.5f, tabMidY - 9.0f, pillW24, 18.0f, kLabBtnLightTop,
                                kLabBtnLightBot);
        }
        float rPillW = gw((cs.roundBits & 8) != 0 ? 18.0f : 24.0f);
        drawGlossyPillBadge(cx(582.5f) - rPillW * 0.5f, tabMidY - 9.0f, rPillW, 18.0f, kLabBtnLightTop,
                            kLabBtnLightBot);

        BevelCols accentBevel = {pal.accentTop, pal.accentBot, pal.accentSheen, pal.accentEdge};
        BevelCols tabBevel = {pal.tabTop, pal.tabBot, pal.bevelLight, pal.bevelDark};
        float tx = 76.0f;
        for (u8 i = 0; i < def.categoryCount && i < 7; i++)
        {
            float tw = kTabWidths[i];
            bool activeTab = i == state.category;
            bevelBox(tx, tabY, tw, tabH, activeTab ? accentBevel : tabBevel);
            if (pageHasOverride(def, def.categoryPages[i]))
            {
                drawOverrideDot(tx + tw - 5.0f, tabY + 5.0f, 4.0f, false, activeTab, pal);
            }
            tx += tw + kFibTight;
        }

        float capScale = scaleForCap(kTypeMeta);
        float capY = textYForCap(tabMidY - kTypeMeta * 0.5f, capScale);
        if (cs.txtTabL != NULL)
        {
            fitText(57.5f, capY, cs.txtTabL, kLabBtnDarkText, capScale, 24.0f, true);
        }
        fitText(582.5f, capY, cs.txtTabR, kLabBtnDarkText, capScale, 24.0f, true);

        float labelTop = tabMidY - kTabsCap * 0.5f;
        tx = 76.0f;
        for (u8 i = 0; i < def.categoryCount && i < 7; i++)
        {
            float tw = kTabWidths[i];
            int catPage = def.categoryPages[i];
            const char* label = NULL;
            if (catPage == pages::P_SCENARIOS
                && (pages::practiceMenuState()->activeDrill != 0
                    || pages::scenarioMenuState()->preset != pages::kPresetOff))
            {
                label = pageTitle(def, catPage);
            }
            else
            {
                label = (def.tabLabels != NULL && def.tabLabels[i] != NULL) ? def.tabLabels[i] : pageTitle(def, catPage);
            }
            gfx::Rgba col = i == state.category ? pal.activeTabText : pal.tabInactiveText;
            fitDisplay(tx + tw * 0.5f, labelTop, label, col, kTabsCap, tw - 6.0f, 1);
            tx += tw + kFibTight;
        }

        if (state.depth > 0)
        {
            drawBreadcrumb(def, state, kTabsNameX, (kTabsBodyTop + kTabsCrumbBottom) * 0.5f, kTypeMeta, 520.0f, pal);
        }
    }

    static void drawPlainChrome(const Page& p, const MenuPalette& pal)
    {
        drawHeaderBand(kRailHeaderBottom, pal);
        gradBox(kLabInnerLeft, kRailBodyTop, kLabInnerRight - kLabInnerLeft, kLabDescTop - kRailBodyTop, pal.baseTop,
                pal.baseBot);
        fitDisplay(kLabInnerLeft + kFibPad, (kLabInnerTop + kRailHeaderBottom) * 0.5f - kTypeHeading * 0.5f, p.title,
                   0, kTypeHeading, 536.0f, 2);
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
        s_allowShowInactive = allowShowInactive(def, pageId);
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
            {kRailRowTop, kRailSubRowTop, kRailScrollH, kRailSubScrollH, kRailHlLeft, kRailHlRight, kRailNameX,
             kRailNameMaxW, kRailPillLeft, kRailPillRight, kRailValCenter, kRailValMaxW},
            {kTabsRowTop, kTabsSubRowTop, kTabsScrollH, kTabsSubScrollH, kTabsHlLeft, kTabsHlRight, kTabsNameX,
             kTabsNameMaxW, kTabsPillLeft, kTabsPillRight, kTabsValCenter, kTabsValMaxW}
        };
        const PaneGeom& g = kGeom[tabsMode ? 1 : 0];
        float rowTop = state.depth > 0 ? g.subRowTop : g.rowTop;
        float scrollH = state.depth > 0 ? g.subScrollH : g.scrollH;

        const MenuPalette& pal = activePalette();

        // 1. Outer frame + layout chrome.
        drawFrameBorder(kLabLeft, kLabRight, kLabTop, kLabBottom, kLabBorder, kLabBorderColor);
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

        // 2. Option rows (or clean utility action summary card when a left-rail utility is highlighted).
        bool utilFocused = railUtilityFocused(def, state);
        bool diPicking = diPickerActiveFor(pageId, state);
        if (utilFocused)
        {
            // Flat right-pane action prompt (no nested bordered card, no repeated title/description).
            int u = state.railCursor - def.categoryCount;
            const MenuAction& act = def.utilities[u];
            bool confirming = act.label != NULL && act.label[0] != '\0';
            const CtrlSpec& cs = ctrlSpec(state.controllerType);
            float paneCenter = (kRailPaneLeft + kLabInnerRight) * 0.5f;
            float paneMaxW = (kLabInnerRight - kRailPaneLeft) - 2.0f * kFibPad;
            float midY = (kRailBodyTop + kLabDescTop) * 0.5f;

            fillBox(paneCenter - 48.0f, midY - 26.0f, 96.0f, kFibHair, confirming ? kLabFocusGold : pal.divider);

            char prompt[64];
            snprintf(prompt, sizeof(prompt),
                     confirming ? "Press %s again to confirm." : "Press %s to run this action.", cs.txtA);
            float pScale = scaleForCap(kTypeBody);
            float badgeH = 18.0f;
            float bw = gw(badgeH);
            float gap = gw(kFibGap);
            float pw = gfx::textWidth(prompt, pScale);
            float avail = w(paneMaxW) - bw - gap;
            float drawW = pw > avail ? avail : pw;
            float startX = cx(paneCenter) - (bw + gap + drawW) * 0.5f;
            drawGlossyRoundBadge(startX + bw * 0.5f, midY, badgeH * 0.5f, kLabBtnATop, kLabBtnABot);
            float gScale = scaleForCap(kTypeMeta);
            float gW = gfx::textWidth(cs.txtA, gScale);
            gfx::text(startX + (bw - gW) * 0.5f, textYForCap(midY - kTypeMeta * 0.5f, gScale), cs.txtA,
                      kLabBtnDarkText, gScale);
            drawFitText(startX + bw + gap, textYForCap(midY - kTypeBody * 0.5f, pScale), prompt,
                        confirming ? kLabFocusGold : kText, pScale, avail);

            fillBox(paneCenter - 48.0f, midY + 24.0f, 96.0f, kFibHair, confirming ? kLabFocusGold : pal.divider);
        }
        else
        {
            for (int r = 0; r < visN; r++)
            {
                int i = visIdx[r];
                const Option& o = p.options[i];
                float y = rowTop + r * kLabRowPitch;
                bool selected = !state.railFocus && i == cur;
                bool on = active(o) && (!diPicking || selected);
                bool grey = false;
                const char* value = valueText(def, o, buf, sizeof(buf), grey);
                bool hasVal = value != NULL && value[0] != '\0';
                if (selected)
                {
                    float stripeX = hasVal ? (g.pillLeft + 4.5f) : (g.hlRight - 18.0f);
                    drawGoldBar(g.hlLeft, g.hlRight, y, kLabRowH, 8.5f, false, stripeX);
                }
                else
                {
                    drawSlantBar(g.hlLeft, g.hlRight, y, kLabRowH, false, kLabRowSlateTop, kLabRowSlateBot,
                                 kLabRowSlateEdge);
                }
                if (hasVal)
                {
                    bool isHi = optionHighlighted(def, pageId, o);
                    drawValuePill(g.pillLeft, g.pillRight, y, kLabRowH, isHi, on, selected);
                    if (selected && hasValue(o) && on)
                    {
                        drawValueArrows(g.pillLeft, g.pillRight, y, kLabRowH, isHi ? kLabFocusGold : kLabGoldInk);
                    }
                }
                else if (o.kind == kKindMenu)
                {
                    gfx::Rgba chevCol = selected ? (on ? kLabGoldInk : kLabGoldInkMuted)
                                                 : (on ? pal.mutedText : kTextDisabled);
                    if (on && pageHasOverride(def, o.max))
                    {
                        drawOverrideDot(g.hlRight - 26.0f, y + kLabRowH * 0.5f, 5.0f, selected, false, pal);
                    }
                    drawSubmenuChevron(g.hlRight - 10.0f, y, chevCol);
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
                bool selected = !state.railFocus && i == cur;
                bool on = active(o) && (!diPicking || selected);
                bool grey = false;
                gfx::Rgba nameCol = selected ? (on ? kLabGoldInk : kLabGoldInkMuted) : (on ? kText : kTextDisabled);
                fitText(g.nameX, y, o.name, nameCol, scale, g.nameMaxW, false);
                const char* value = valueText(def, o, buf, sizeof(buf), grey);
                if (value != NULL && value[0] != '\0')
                {
                    bool isHi = optionHighlighted(def, pageId, o);
                    gfx::Rgba valCol =
                        (on && !grey) ? ((selected && !isHi) ? kLabGoldInk : kLabWhiteText) : kTextDisabled;
                    fitText(g.valCenter, y, value, valCol, scale, g.valMaxW - 14.0f, true);
                }
            }

            // 4. Optional page-specific overlay from extra (e.g. Fighter Swap stock icon).
            main::extraDrawPageOverlay(pageId, railMode);
        }

        // 5. Shared 1-line description strip, unified vector button footer, and bottom inner shadow.
        drawDescStrip(def, state, pageId, cur, p, pal);
        drawFooterStrip(def, state, pageId, p, cur, pal);
        float iw = kLabInnerRight - kLabInnerLeft;
        fillBox(kLabInnerLeft, kLabInnerBottom - 2.0f, iw, 2.0f, pal.bevelDark);
    }
}
}
