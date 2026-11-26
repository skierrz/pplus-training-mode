#include "main/ui/pages/alter_inputs.h"

#include <stddef.h>

#include "main/extra.h"
#include "main/ui/pages/page_strings.h"
#include "main/ui/pages/pages.h"

// Per-frame input editor matching the Brawl/P+ controller model stored by a
// recording slot: stick and C-stick (-80..80), analog L and R (0..150), and
// fighter button flags. Rows disable when the slot is empty or extra is absent.
namespace tm {
namespace ui {
namespace pages {
#define TM_SCRATCH(f) static_cast<u16>(offsetof(AlterCopy, f))
#ifdef __MWERKS__
    extern Page kAlterInputs = {"Alter Inputs", NULL, 0, {0, 0, 0}};
    static Option* s_alterRows = NULL;

    void initAlterInputsPage(Option* buf)
    {
        s_alterRows = buf;
        static const u16 kStrs[2] = {kPs_fmt_d, kPs_alt_kPressed};
        static void (*const kCbs[2])() = {NULL, main::extraAlterEdited};
        static bool (*const kGates[2])() = {NULL, main::extraAlterRow};
        const u8 kAx = kFlagScratch | kFlagHalf | kFlagSigned;
        static const RowSpec kSpecs[16] = {
            {kKindInt,    kFlagScratch | kFlagHalf, TM_SCRATCH(frame), 1, 1, 3600, 0, 0, 1, 0},
            {kKindInt,    kAx, TM_SCRATCH(axis) + 0,  -80, 0, 80,  0, 1, 1, 0},
            {kKindInt,    kAx, TM_SCRATCH(axis) + 2,  -80, 0, 80,  0, 1, 1, 0},
            {kKindInt,    kAx, TM_SCRATCH(axis) + 4,  -80, 0, 80,  0, 1, 1, 0},
            {kKindInt,    kAx, TM_SCRATCH(axis) + 6,  -80, 0, 80,  0, 1, 1, 0},
            {kKindInt,    kAx, TM_SCRATCH(axis) + 8,  0,   0, 150, 0, 1, 1, 0},
            {kKindInt,    kAx, TM_SCRATCH(axis) + 10, 0,   0, 150, 0, 1, 1, 0},
            {kKindChoice, kFlagScratch, TM_SCRATCH(button) + 0, TM_CHOICE_LO, 0, 1, 1, 1, 1, 0},
            {kKindChoice, kFlagScratch, TM_SCRATCH(button) + 1, TM_CHOICE_LO, 0, 1, 1, 1, 1, 0},
            {kKindChoice, kFlagScratch, TM_SCRATCH(button) + 2, TM_CHOICE_LO, 0, 1, 1, 1, 1, 0},
            {kKindChoice, kFlagScratch, TM_SCRATCH(button) + 3, TM_CHOICE_LO, 0, 1, 1, 1, 1, 0},
            {kKindChoice, kFlagScratch, TM_SCRATCH(button) + 4, TM_CHOICE_LO, 0, 1, 1, 1, 1, 0},
            {kKindChoice, kFlagScratch, TM_SCRATCH(button) + 5, TM_CHOICE_LO, 0, 1, 1, 1, 1, 0},
            {kKindChoice, kFlagScratch, TM_SCRATCH(button) + 6, TM_CHOICE_LO, 0, 1, 1, 1, 1, 0},
            {kKindChoice, kFlagScratch, TM_SCRATCH(button) + 7, TM_CHOICE_LO, 0, 1, 1, 1, 1, 0},
            {kKindChoice, kFlagScratch, TM_SCRATCH(button) + 8, TM_CHOICE_LO, 0, 1, 1, 1, 1, 0}
        };
        fillPageRows(kAlterInputs, buf, 16, kSpecs, kPs_alt_names, kStrs, NULL, kCbs, kGates);
    }

    // Called from the extra module; keep from linker dead-stripping.
#pragma force_active on
    Option& alterFrameRow()
    {
        return s_alterRows[0];
    }
#pragma force_active reset
#else
    static const char* const kPressed[] = {"Off", "On"};

#define TM_AXIS(name, k, lo)                                                                                   \
    TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHalf | ::tm::ui::kFlagSigned,          \
           TM_SCRATCH(axis) + 2 * (k), lo, (k) < 4 ? 80 : 150, 0, name, "%d", main::extraAlterEdited,         \
           main::extraAlterRow)
#define TM_BUTTON(name, k)                                                                                     \
    TM_ROW(::tm::ui::kKindChoice, ::tm::ui::kFlagScratch, TM_SCRATCH(button) + (k), 0, 1, 0, name, kPressed,  \
           main::extraAlterEdited, main::extraAlterRow)

    // Non-const: the Frame row's range follows the selected slot length.
    static Option kRows[] = {
        TM_ROW(::tm::ui::kKindInt, ::tm::ui::kFlagScratch | ::tm::ui::kFlagHalf, TM_SCRATCH(frame), 1, 3600, 1,
               "Frame", "%d", NULL, main::extraAlterRow),
        TM_AXIS("Stick X", 0, -80),
        TM_AXIS("Stick Y", 1, -80),
        TM_AXIS("C-Stick X", 2, -80),
        TM_AXIS("C-Stick Y", 3, -80),
        TM_AXIS("Analog L", 4, 0),
        TM_AXIS("Analog R", 5, 0),
        TM_BUTTON("Attack", 0),
        TM_BUTTON("Special", 1),
        TM_BUTTON("Jump", 2),
        TM_BUTTON("Shield", 3),
        TM_BUTTON("Smash", 4),
        TM_BUTTON("C-Stick", 5),
        TM_BUTTON("Up Taunt", 6),
        TM_BUTTON("Side Taunt", 7),
        TM_BUTTON("Down Taunt", 8)
    };
#undef TM_BUTTON
#undef TM_AXIS

    Option& alterFrameRow()
    {
        return kRows[0];
    }

    extern const Page kAlterInputs = TM_PAGE("Alter Inputs", kRows);
#endif
#undef TM_SCRATCH
}
}
}
