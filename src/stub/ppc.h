#pragma once

#include <OS/OSCache.h>
#include <types.h>

// Helpers for reading and patching PowerPC instruction words.
namespace tm {
namespace ppc {
    // I-form branch: opcode 18, 24-bit signed word offset, AA (bit 1), LK (bit 0).
    inline bool isBranch(u32 word) { return (word & 0xFC000003) == 0x48000000; }
    inline bool isBranchLink(u32 word) { return (word & 0xFC000003) == 0x48000001; }

    // Destination of a relative `b`/`bl` stored at `at`.
    inline u32 branchTarget(u32 at, u32 word)
    {
        s32 offset = static_cast<s32>(word & 0x03FFFFFC);
        if (offset & 0x02000000)
        {
            offset -= 0x04000000;
        }
        return at + offset;
    }

    // Makes patched code visible to instruction fetch.
    inline void flushCode(void* at, u32 bytes)
    {
        DCFlushRange(at, bytes);
        ICInvalidateRange(at, bytes);
    }
}
}
