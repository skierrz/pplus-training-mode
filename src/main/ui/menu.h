#pragma once

#include <types.h>

#include "shared/version.h"

// Table-driven lab menu engine. Options are constant records grouped into
// pages; every value lives in one byte or halfword of a caller-owned settings
// struct at the option's offset. Static storage only.
namespace tm {
namespace ui {
    enum OptionKind {
        kKindMenu,   // A opens page `max`
        kKindChoice, // value 0..max, shown as labels[value]; clamps
        kKindInt,    // value min..max, shown with `data` as a printf format; clamps
        kKindToggle, // value 0/1, shown as Off/On
        kKindFunc,   // A calls onChange; `data`, if set, is shown as its value
        kKindInfo    // read-only; `data`, if set, is shown as its value
    };

    enum OptionFlag {
        kFlagHalf = 0x1,        // the value is a halfword (else a byte)
        kFlagSigned = 0x2,      // the value is signed
        kFlagUnavailable = 0x4, // not built yet: grey, shows `def`, never changes
        kFlagHidden = 0x8,      // with no name: not drawn, and the cursor skips it
        kFlagBit = 0x10,        // single-bit toggle in a byte; `min` holds the bitmask
        // The value is not a setting: `offset` is into menuScratch(), bytes
        // a page fills and reads itself (an editor's copy of other data).
        kFlagScratch = 0x20,
        // Omit the row when its enabled() predicate returns false, unless
        // g_showInactiveRows is true (compacts Stage & Cast to active rows).
        kFlagHideWhenDisabled = 0x40,
        // Shown only in Top Tabs mode; omitted in Category Rail mode where
        // the action is already pinned in the left rail utility section.
        kFlagTabsOnly = 0x80
    };

    enum { kScratchBytes = 96, kShowInactiveScratchOffset = 32 };
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    u8* x_menuScratch();
#ifndef TM_EXTRA_THUNK_IMPL
    static inline u8* menuScratch() { return x_menuScratch(); }
#else
    u8* menuScratch();
#endif
#else
    u8* menuScratch();
#endif
    extern bool g_showInactiveRows;

    // A choice label starting with this character is listed but not built yet:
    // drawn grey, without the marker.
#define TM_UNAVAILABLE_LABEL "\001"
    // A choice label starting with this character is retired from menu cycling
    // while keeping stored indices stable; loaded settings holding it reset to
    // the default (retireDropped).
#define TM_DROPPED_LABEL "\002"

    struct Option {
        u8 kind;    // OptionKind
        u8 flags;   // OptionFlag bits
        u16 offset; // byte offset of the value in the settings struct
        s16 min;
        s16 max;    // INT: largest value; CHOICE: count - 1; MENU: page id
        s16 def;    // default value (also what a row not built yet shows)
        u16 reserved;
        const char* name;
        const char* desc;   // unused in match (descriptions live in extra/menu_descriptions.cpp)
        const void* data;   // CHOICE: const char* const* labels; INT: format; FUNC/INFO: value text
        void (*onChange)(); // after the value changed, or when A runs a FUNC
        bool (*enabled)();  // NULL: always; false: grey and skipped by the cursor
    };
    TM_STATIC_ASSERT(sizeof(Option) == 32, tm_option_is_32_bytes);

    struct Page {
        const char* title;
        const Option* options;
        u8 count;
        u8 reserved[3];
    };

    // Directional input after key repeat, plus the face buttons this frame.
    struct MenuInput {
        int up; // steps to move this frame (0 or 1)
        int down;
        int left;
        int right;
        bool a, b;
        bool prevCat, nextCat; // L / R category or tab switch
    };

    // Pinned left-rail utility action (Category Rail bottom section).
    struct MenuAction {
        const char* name;
        const char* label; // optional confirm label (e.g. lab::g_resetLabel)
        void (*onSelect)();
    };

    struct MenuDef {
        const Page* const* pages;
        u8 pageCount;
        // Base of the settings struct the offsets point into.
        u8* (*values)();
        // Optional top-level categories and rail utilities (0/NULL for plain menus).
        u8 categoryCount;
        const u8* categoryPages;
        const char* const* tabLabels;
        u8 utilityCount;
        const MenuAction* utilities;
    };

    enum { kMaxDepth = 6, kMaxPages = 32 };

    struct MenuState {
        u8 depth;
        u8 stack[kMaxDepth];  // page ids, [0] is the top page
        u8 cursor[kMaxPages]; // highlighted row per page (raw option index)
        u8 scroll[kMaxPages]; // first visible row per page (visible ordinal)
        u8 category;          // active top-level category (0..categoryCount-1)
        u8 railCursor;        // highlighted Category Rail item (0..cats+utils-1)
        bool railFocus;       // true while Category Rail holds focus
        bool openStart;       // true when opened with Start (else Z in Pause)
        u8 controllerType;    // gfPadType::PadType of the active owner
    };

    // Skip rows marked kFlagUnavailable during cursor navigation.
    enum { kSkipUnavailableRows = 1 };

    void menuReset(const MenuDef& def, MenuState& state);

    // Applies one frame of input. Returns false if B was pressed on the top
    // page (the caller decides whether that closes the menu).
    bool menuStep(const MenuDef& def, MenuState& state, const MenuInput& in);

    // Rewinds to the top page, then opens `path` (page ids below the top)
    // through its rows; false, changing nothing, if a row on the way is
    // disabled.
    bool menuJump(const MenuDef& def, MenuState& state, const u8* path, int count);

    // Draws the menu in the 2D pass (inside gfx::begin()/end()).
    void menuDraw(const MenuDef& def, MenuState& state);

    // The row whose function A is running (for rows that share one).
    int activeRow();

    // Asks the game for one of its menu sounds (game::MenuSound,
    // shared/game/menu_sound.h) and logs the id. Only menuStep sounds (a move
    // or value change, a page or function, B leaving a page, A on an
    // unavailable row), not menuJump or the value setters, so only a button
    // press does. Silent while g_menuQuiet is set (Frame Decrement's replay).
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    void x_menuSound(int id);
#ifndef TM_EXTRA_THUNK_IMPL
    static inline void menuSound(int id) { x_menuSound(id); }
#else
    void menuSound(int id);
#endif
#else
    void menuSound(int id);
#endif
    extern bool g_menuQuiet;

    // The page shown and the option under the cursor (NULL on an empty page).
    int currentPage(const MenuState& state);
    const Option* currentOption(const MenuDef& def, const MenuState& state);

    bool hasValue(const Option& o);
    int optionValue(const MenuDef& def, const Option& o);
    void setOptionValue(const MenuDef& def, const Option& o, int value);
    // Clamps every stored value of every page into its option's range.
    void clampAll(const MenuDef& def);
    // Sets every stored value of every page to its option's default.
    void defaultAll(const MenuDef& def);
    // Sets one stored non-scratch option to its default (preserving sibling
    // bits on kFlagBit). Returns true if the option was stored and reset.
    bool defaultOption(const MenuDef& def, const Option& o);
    // Sets every stored non-scratch option on `pageId` to its default.
    // Returns false if `pageId` is out of range.
    bool defaultPage(const MenuDef& def, int pageId);
    // Sets every stored non-scratch option whose storage bytes fall inside
    // `[offset, offset + size)` to its default.
    void defaultRange(const MenuDef& def, u16 offset, u16 size);
    // Every stored choice holding a dropped value goes to its default.
    void retireDropped(const MenuDef& def);
    bool droppedLabel(const char* label);
}
}
