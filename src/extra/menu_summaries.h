#pragma once

#include <types.h>

namespace tm {
namespace extra {
    // Formats a compact (<= 15 chars) live state summary for kKindMenu rows
    // pointing to `targetPageId`. Returns `buf` or NULL when no summary applies.
    const char* menuSubmenuSummary(u8 targetPageId, char* buf, u32 cap);
}
}
