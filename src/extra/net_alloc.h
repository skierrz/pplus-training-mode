#pragma once

#include <types.h>

// Allocates large data-only working buffers for the extra module from
// Heaps::Network (MEM2, ~1.29 MiB free in offline Training and offline Versus,
// excluded from savestates) so MEM1 OverlayFighter4 stays reserved for code.
namespace tm {
namespace extra {
#ifdef __MWERKS__
    void* allocNetZero(u32 bytes);
    void freeNetRaw(void* ptr);
    void* freeNetNull(void* ptr);
    void logf(const char* fmt, ...);
#else
    inline void* allocNetZero(u32)
    {
        return NULL;
    }
    inline void freeNetRaw(void*)
    {
    }
    inline void* freeNetNull(void*)
    {
        return NULL;
    }
#endif

    template <typename T>
    inline T* allocNetArray(u32 count)
    {
        return static_cast<T*>(allocNetZero(static_cast<u32>(sizeof(T)) * count));
    }

    template <typename T>
    inline void freeNetPtr(T*& ptr)
    {
        ptr = static_cast<T*>(freeNetNull(ptr));
    }
}
}
