#pragma once

#include <types.h>

// Training lab tools that live in the extra module and are reached from the
// match module through its extra hooks (main/extra.h).
namespace tm {
namespace extra {
    // Alter Inputs: every row's `enabled` and every value row's change.
    bool alterRow();
    void alterEdited();
}
}
