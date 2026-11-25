#pragma once

#include <types.h>

// Stand-in (PC build): records cache maintenance calls in host::fakes.
void DCFlushRange(void* at, u32 bytes);
void ICInvalidateRange(void* at, u32 bytes);
