#pragma once

#include <stddef.h>

#include "main/settings.h"
#include "main/ui/menu.h"

// Lab menu pages grouped under 7 top-level categories; registry.cpp lists them
// in PageId order.
namespace tm {
namespace ui {
namespace pages {
    enum PageId {
        P_GENERAL = 0, // Category 0: Lab
        P_SCENARIOS,   // Category 1: Scenarios
        P_CPU,         // Category 2: CPU
        P_RECORDING,   // Category 3: Recording
        P_DISPLAY,     // Category 4: Display
        P_OSD_MENU,    // Category 5: OSDs
        P_STAGE_CAST,  // Category 6: Stage & Cast
        P_CONTROLS,
        P_DEBUG,
        P_COMBO_TRAINER,
        P_COMBO_METERS,
        P_TECH,
        P_ADVANCED_COUNTER,
        P_SLOTS,
        P_ALTER_INPUTS,
        P_HMN_CHANCES,
        P_CPU_CHANCES,
        P_HMN_OVERLAYS,
        P_CPU_OVERLAYS,
        P_HMN_INFO,
        P_CPU_INFO,
        P_LEDGEDASH_HUD,
        P_FIGHTER_SWAP,
        P_THEME,
        P_REVERSALS,
        P_CPU_DI,
        P_CPU_COUNTERS,
        P_CPU_DEFENSE,
        P_COUNTER_POOL,
        P_COUNT
    };
    enum { kCategoryCount = 7 };
    TM_STATIC_ASSERT(P_COUNT <= kMaxPages, tm_pages_fit_menu_state);

#ifdef __MWERKS__
#define TM_PAGE_QUAL
#else
#define TM_PAGE_QUAL const
#endif

    extern const Page kGeneral;
    extern const Page kScenarios;
    extern const Page kCpuOptions;
    extern const Page kRecording;
    extern const Page kDisplayOptions;
    extern const Page kOsdMenu;
    extern TM_PAGE_QUAL Page kStageCast;
    extern const Page kControls;
    extern const Page kDebug;
    extern const Page kComboTrainer;
    extern const Page kComboMeters;
    extern const Page kTechOptions;
    extern const Page kAdvancedCounter;
    extern const Page kSlotManagement;
    extern const Page kAlterInputs;
    extern const Page kHmnChances;
    extern const Page kCpuChances;
    extern const Page kHmnOverlays;
    extern TM_PAGE_QUAL Page kCpuOverlays;
    extern const Page kHmnInfo;
    extern TM_PAGE_QUAL Page kCpuInfo;
    extern const Page kLedgedashHud;
    extern const Page kFighterSwap;
    extern const Page kTheme;
    extern const Page kReversalSlots;
    extern const Page kCpuDi;
    extern const Page kCpuCounters;
    extern const Page kCpuDefense;
    extern TM_PAGE_QUAL Page kCounterPool;

#ifdef __MWERKS__
    void pagesAttach();
    void pagesDetach();
    void initPoolPage(Option* buf);
    void initCpuOverlaysPage(Option* buf);
    void initCpuInfoPage(Option* buf);
    void initStageCastPage(Option* buf);
#endif

    // Indexed by PageId (registry.cpp).
    extern const Page* const kPages[P_COUNT];

#ifdef __MWERKS__
    inline const char* packedLabelAt(const char* p, int idx)
    {
        while (idx > 0)
        {
            while (*p++ != '\0') {}
            idx--;
        }
        return p;
    }
#else
    inline const char* packedLabelAt(const char* const* p, int idx)
    {
        return p[idx];
    }
#endif
    bool never();
}
}
}

#define TM_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define TM_PAGE(title, rows) {title, rows, TM_COUNT(rows), {0, 0, 0}}

// Byte offset and size flag of a main::Settings field.
#define TM_FIELD(f) static_cast<u16>(offsetof(::tm::main::Settings, f))
#define TM_HALF(f) (sizeof(((::tm::main::Settings*)0)->f) == 2 ? ::tm::ui::kFlagHalf : 0)
#define TM_SIGNED(lo) ((lo) < 0 ? ::tm::ui::kFlagSigned : 0)

#ifdef __MWERKS__
#define TM_ROW(kind, flags, offset, lo, hi, def, name, data, onChange, enabled) \
    {kind, flags, offset, static_cast<u8>(lo), static_cast<s8>(def), static_cast<s16>(hi), name, data, onChange, enabled}
// Packed choice label tables: NUL-separated char array on Wii (o.min == 1),
// standard pointer array on host (o.min == 0) so host tests inspect o.data directly.
#define TM_L(s) s "\0"
#define TM_LABELS_BEGIN(name) static const char name[] =
#define TM_LABELS_EXTERN_DECL(name, n) extern const char name[]
#define TM_LABELS_EXTERN_BEGIN(name, n) extern const char name[] =
#define TM_LABELS_END ;
#define TM_CHOICE_LO 1
#define TM_LABELS_ASSERT(name, n, msg)
#else
#define TM_ROW(kind, flags, offset, lo, hi, def, name, data, onChange, enabled) \
    {kind, flags, offset, lo, hi, def, 0, name, NULL, data, onChange, enabled}
#define TM_L(s) s,
#define TM_LABELS_BEGIN(name) static const char* const name[] = {
#define TM_LABELS_EXTERN_DECL(name, n) extern const char* const name[n]
#define TM_LABELS_EXTERN_BEGIN(name, n) extern const char* const name[n] = {
#define TM_LABELS_END };
#define TM_CHOICE_LO 0
#define TM_LABELS_ASSERT(name, n, msg) TM_STATIC_ASSERT(TM_COUNT(name) == (n), msg)
#endif

#define TM_MENU(name, page) \
    TM_ROW(::tm::ui::kKindMenu, 0, 0, 0, ::tm::ui::pages::page, 0, name, NULL, NULL, NULL)
#define TM_MENU_IF(name, page, enabled) \
    TM_ROW(::tm::ui::kKindMenu, 0, 0, 0, ::tm::ui::pages::page, 0, name, NULL, NULL, enabled)
#define TM_TOGGLE(name, f, def, onChange) \
    TM_ROW(::tm::ui::kKindToggle, TM_HALF(f), TM_FIELD(f), 0, 1, def, name, NULL, onChange, NULL)
#define TM_INT(name, f, lo, hi, def, format, onChange) \
    TM_ROW(::tm::ui::kKindInt, TM_HALF(f) | TM_SIGNED(lo), TM_FIELD(f), lo, hi, def, name, format, onChange, NULL)
#define TM_CHOICE(name, f, labels, def, onChange) \
    TM_ROW(::tm::ui::kKindChoice, TM_HALF(f), TM_FIELD(f), 0, TM_COUNT(labels) - 1, def, name, labels, onChange, NULL)
#define TM_CHOICE_P(name, f, labels, count, def, onChange) \
    TM_ROW(::tm::ui::kKindChoice, TM_HALF(f), TM_FIELD(f), TM_CHOICE_LO, (count) - 1, def, name, labels, onChange, NULL)
#define TM_ROW_CHOICE_P(flags, off, count, def, name, labels, onChange, enabled) \
    TM_ROW(::tm::ui::kKindChoice, flags, off, TM_CHOICE_LO, (count) - 1, def, name, labels, onChange, enabled)
#define TM_FUNC(name, onSelect) \
    TM_ROW(::tm::ui::kKindFunc, 0, 0, 0, 0, 0, name, NULL, onSelect, NULL)
