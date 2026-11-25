#include "main/experiments/padding.h"

#include "shared/log.h"

#ifndef TM_PAD_BYTES
#define TM_PAD_BYTES 0
#endif

namespace tm {
namespace experiments {
#if TM_PAD_BYTES > 0
    // Non-zero first byte: the whole array stays in .rodata, in the file.
    static const u8 kFiller[TM_PAD_BYTES] = {1};
    static volatile u32 s_sink = 0;

    void logPadding()
    {
        TM_LOG("padding: %u bytes at %08x\n", static_cast<u32>(sizeof(kFiller)), kFiller);
    }

    void touchPadding(u32 call)
    {
        s_sink += kFiller[call % sizeof(kFiller)];
    }
#else
    void logPadding()
    {
    }

    void touchPadding(u32)
    {
    }
#endif
}
}
