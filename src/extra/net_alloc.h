#pragma once

#include <types.h>

class Stage;
class ftEntryManager;
class soModuleAccesser;
class soModuleEnumeration;
struct Vec3f;
class grCollision;

// Allocates large data-only working buffers for the extra module from
// Heaps::Network (MEM2, ~1.29 MiB free in offline Training and offline Versus,
// excluded from savestates) so MEM1 OverlayFighter4 stays reserved for code.
// Also provides single-relocation sora_melee accessors and shared out-of-line
// FP/work-module helpers so extra TUs do not duplicate them.
namespace tm {
namespace extra {
#ifdef __MWERKS__
    void* allocNetZero(u32 bytes);
    void freeNetRaw(void* ptr);
    void* freeNetNull(void* ptr);
    void logf(const char* fmt, ...);

    // Single-relocation sora_melee accessors (collapse mod=27 kept relocs).
    Stage* extraStage();
    ftEntryManager* extraEntryManager();
    const void* extraFtCommonData(int ftKind);
    float extraConstFloat(soModuleAccesser* acc, u32 id);
    s32 extraConstInt(soModuleAccesser* acc, u32 id);
    bool extraRayCheck(Vec3f* pos, Vec3f* dir, Vec3f* hit, Vec3f* normal, bool passThrough, grCollision* col,
                       bool b2, u8 mask);

    // Shared out-of-line PowerPC conversion and work-module helpers.
    float i2f(s32 v);
    float u2f(u32 v);
    s32 f2i(float v);
    float i2fScale(s32 v, float s);
    float minf(float a, float b);
    float maxf(float a, float b);
    float clampf(float v, float lo, float hi);
    float floatAt(const void* base, u32 offset);
    const void* workBank(const soModuleEnumeration* m, u32 which);
    s32 workInt(const soModuleEnumeration* m, u32 which, u32 index);
    float workFloat(const soModuleEnumeration* m, u32 which, u32 index);
    bool workBit(const soModuleEnumeration* m, u32 which, u32 index);
#else
#define extraStage() g_Stage
#define extraEntryManager() g_ftEntryManager
#define extraFtCommonData(kind) g_ftCommonDataAccesser.getData(static_cast<ftKind>(kind))
#define extraConstFloat(acc, id) soValueAccesser::getConstantFloat((acc), (id), 0)
#define extraConstInt(acc, id) soValueAccesser::getConstantInt((acc), (id), 0)
#define extraRayCheck(pos, dir, hit, norm, pt, col, b2, mask) \
    stRayCheck((pos), (dir), (hit), (norm), (pt), (col), (b2), (mask))

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
    inline float i2f(s32 v)
    {
        return static_cast<float>(v);
    }
    inline float u2f(u32 v)
    {
        return static_cast<float>(v);
    }
    inline s32 f2i(float v)
    {
        return static_cast<s32>(v);
    }
    inline float i2fScale(s32 v, float s)
    {
        return static_cast<float>(v) * s;
    }
    inline float minf(float a, float b)
    {
        return a < b ? a : b;
    }
    inline float maxf(float a, float b)
    {
        return a > b ? a : b;
    }
    inline float clampf(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    inline float floatAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const float*>(static_cast<const u8*>(base) + offset);
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
