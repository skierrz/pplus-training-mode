#include "main/ui/pages/alter_inputs.h"

#include <stddef.h>

#include "main/extra.h"
#include "main/ui/pages/pages.h"

// Per-frame input editor matching the Brawl/P+ controller model stored by a
// recording slot: stick and C-stick (-80..80), analog L and R (0..150), and
// fighter button flags. Rows disable when the slot is empty or extra is absent.
namespace tm {
namespace ui {
namespace pages {
    static const char* const kPressed[] = {"Off", "On"};

#define TM_SCRATCH(f) static_cast<u16>(offsetof(AlterCopy, f))
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
#undef TM_SCRATCH

    // Called from the extra module; keep from linker dead-stripping.
#pragma force_active on
    Option& alterFrameRow()
    {
        return kRows[0];
    }
#pragma force_active reset

    extern const Page kAlterInputs = TM_PAGE("Alter Inputs", kRows);
}
}
}
