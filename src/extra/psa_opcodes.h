#pragma once

#include <types.h>

// Live PSA AnimCmd module prefix and opcode name lookup plus typed
// acAnimCmdArgument formatting (credited to fudgepop01; see CREDITS.md).
namespace tm {
namespace extra {
    void psaOpcodesAttach();
    void psaOpcodesDetach();

    const char* psaModulePrefix(u8 mod);
    const char* psaOpcodeName(u32 cmdHeader);
    void psaFormatOpcodeName(u32 cmdHeader, char* out, int outSize);
    void psaFormatArg(u32 type, u32 content, char* out, int outSize);
}
}
