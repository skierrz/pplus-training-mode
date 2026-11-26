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
        P_OLIMAR,
        P_COUNT
    };
    enum { kCategoryCount = 7 };
    TM_STATIC_ASSERT(P_COUNT <= kMaxPages, tm_pages_fit_menu_state);

#ifdef __MWERKS__
#define TM_PAGE_QUAL
#else
#define TM_PAGE_QUAL const
#endif

    extern TM_PAGE_QUAL Page kGeneral;
    extern TM_PAGE_QUAL Page kScenarios;
    extern TM_PAGE_QUAL Page kCpuOptions;
    extern TM_PAGE_QUAL Page kRecording;
    extern const Page kDisplayOptions;
    extern TM_PAGE_QUAL Page kOsdMenu;
    extern TM_PAGE_QUAL Page kStageCast;
    extern TM_PAGE_QUAL Page kControls;
    extern const Page kDebug;
    extern TM_PAGE_QUAL Page kComboTrainer;
    extern TM_PAGE_QUAL Page kComboMeters;
    extern TM_PAGE_QUAL Page kTechOptions;
    extern TM_PAGE_QUAL Page kAdvancedCounter;
    extern TM_PAGE_QUAL Page kSlotManagement;
    extern TM_PAGE_QUAL Page kAlterInputs;
    extern TM_PAGE_QUAL Page kHmnChances;
    extern TM_PAGE_QUAL Page kCpuChances;
    extern TM_PAGE_QUAL Page kHmnOverlays;
    extern TM_PAGE_QUAL Page kCpuOverlays;
    extern TM_PAGE_QUAL Page kHmnInfo;
    extern TM_PAGE_QUAL Page kCpuInfo;
    extern TM_PAGE_QUAL Page kLedgedashHud;
    extern const Page kFighterSwap;
    extern TM_PAGE_QUAL Page kTheme;
    extern TM_PAGE_QUAL Page kReversalSlots;
    extern TM_PAGE_QUAL Page kCpuDi;
    extern TM_PAGE_QUAL Page kCpuCounters;
    extern TM_PAGE_QUAL Page kCpuDefense;
    extern TM_PAGE_QUAL Page kCounterPool;
    extern TM_PAGE_QUAL Page kOlimar;

#ifdef __MWERKS__
    struct RowSpec {
        u8 kind;
        u8 flags;
        u16 off;
        s8 lo;
        s8 def;
        s16 hi;
        u8 dataIdx; // 0xFF = NULL
        u8 cbIdx;   // 0 = NULL
        u8 gateIdx; // 0 = NULL
        u8 pad;
    };

    // dataIdx: 0xFF = NULL; 0xF0..0xFE = extraData[dataIdx - 0xF0]; else pageStr(strOffs[dataIdx]).
    void fillPageRows(Page& page, Option* buf, int count, const RowSpec* specs, u16 namesOff,
                      const u16* strOffs, const void* const* extraData,
                      void (*const* cbTbl)(), bool (*const* gateTbl)());
    void pagesAttach();
    void pagesDetach();
    void initCpuPages(Option* buf);
    void detachCpuPages();
    void initAdvancedCounterPage(Option* buf);
    void initOverlayPages(Option* buf);
    void initInfoPages(Option* buf);
    void initStageCastPage(Option* buf);
    void detachStageCastPage();
    void initScenariosPage(Option* buf);
    void initOsdMenuPage(Option* buf);
    void initComboTrainerPages(Option* buf);
    void initTechOptionsPage(Option* buf);
    void initRecordingPage(Option* buf);
    void detachRecordingPage();
    void initGeneralPage(Option* buf);
    void initControlsPage(Option* buf);
    void initThemePage(Option* buf);
    void initAlterInputsPage(Option* buf);
    void detachAlterInputsPage();
    void initSlotManagementPage(Option* buf);
    void initSlotChancesPages(Option* buf);
    void initLedgedashHudPage(Option* buf);
    void initOlimarPage(Option* buf);
#endif

    // Indexed by PageId (registry.cpp).
    extern const Page* const kPages[P_COUNT];

#ifdef __MWERKS__
    inline const char* packedLabelAt(const char* p, int idx)
    {
        if (p == NULL)
        {
            static const char kEmpty[1] = {0};
            return kEmpty;
        }
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
        static const char kEmpty[1] = {0};
        return p != NULL ? p[idx] : kEmpty;
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
