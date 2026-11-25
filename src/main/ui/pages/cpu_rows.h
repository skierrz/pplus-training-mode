#pragma once

#include "main/ui/pages/pages.h"

// Rows of the CPU and Tech pages that apply only while CPU Control is Lab.
namespace tm {
namespace ui {
namespace pages {
    bool cpuLabControl();

    enum { kCounterGroundSize = 51, kCounterAirSize = 29, kCounterShieldSize = 44 };
    extern const char* kCounterGround[kCounterGroundSize];
    extern const char* const kCounterAir[kCounterAirSize];
    extern const char* kCounterShield[kCounterShieldSize];
    // Enables/disables fighter-specific DJC/Float counter labels for the active CPU.
    bool counterLabels();
}
}
}

#define TM_CPU_TOGGLE(name, f, def) \
    TM_ROW(::tm::ui::kKindToggle, TM_HALF(f), TM_FIELD(f), 0, 1, def, name, NULL, NULL, ::tm::ui::pages::cpuLabControl)
#define TM_CPU_INT(name, f, lo, hi, def, format)                                                        \
    TM_ROW(::tm::ui::kKindInt, TM_HALF(f) | TM_SIGNED(lo), TM_FIELD(f), lo, hi, def, name, format, NULL, \
           ::tm::ui::pages::cpuLabControl)
#define TM_CPU_CHOICE(name, f, labels, def)                                                                   \
    TM_ROW(::tm::ui::kKindChoice, TM_HALF(f), TM_FIELD(f), 0, TM_COUNT(labels) - 1, def, name, labels, NULL, \
           ::tm::ui::pages::cpuLabControl)
