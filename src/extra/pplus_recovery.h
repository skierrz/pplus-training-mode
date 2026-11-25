#pragma once

#include <types.h>

#include "main/cpu/fighter_input.h"
#include "main/cpu/view.h"

class Fighter;

// Runtime wrapper for the 44-fighter PPlusAI recovery engine (src/extra/).
namespace tm {
namespace extra {
    void pplusRecoveryAttach();
    void pplusRecoveryReset(int slot);

    // Drives one frame of PPlusAI recovery into `out`. Returns true while the
    // recovery engine owns the frame, false once the CPU is back on stage.
    bool pplusRecoveryStep(const cpu::Owner& who, const cpu::View& v, Fighter* f, cpu::Frame& out, u32 lastButtons);
}
}
