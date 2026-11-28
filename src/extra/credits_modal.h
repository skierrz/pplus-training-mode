#pragma once

#include <types.h>

#include "main/extra.h"

// In-match Credits modal hosted in the extra module (src/extra/) so match
// module (src/main/) size overhead stays near zero.
namespace tm {
namespace extra {
    void creditsModalAttach();
    void creditsModalDetach();
    bool creditsModalVisible();
    void creditsModalDraw();
    const char* creditsMenuDesc();

    // Single-entrypoint dispatch for main::ExtraHooks::creditsHook.
    // Lower 8 bits of `op` select main::CreditsOp; bits 8..15 carry controllerType.
    u32 creditsModalHook(u32 op, const main::MenuPad* pad);
}
}
