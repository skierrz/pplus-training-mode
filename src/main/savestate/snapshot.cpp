#include "main/savestate/snapshot.h"

#include <OS/OSCache.h>
#include <OS/OSInterrupt.h>
#include <OS/OSLink.h>
#include <OS/OSTime.h>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <gf/gf_pad_system.h>
#include <gm/gm_global.h>
#include <sr/sr_common.h>
#include <types.h>

#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "main/savestate/trace.h"
#include "shared/log.h"
#include "shared/perf.h"

extern "C" void* memcpy(void* dst, const void* src, unsigned long n);
extern "C" void GXDrawDone();
extern "C" void GXInvalidateVtxCache();
extern "C" void GXInvalidateTexAll();

// The region set, what a Training match's state is made of in memory, is
// found at save time from the game's heap table and module list plus fixed
// ranges. What each class copies and why: docs/internals.md, "The
// savestate's region set".
namespace tm {
namespace savestate {
    enum {
        kHeapCount = 0x47,
        // gfMemoryPool, as shared/game/heap.cpp reads it: +0x4 start, +0x8
        // end, +0x20 -> sentinel block whose +0x8 starts the free list
        // (address order). The pool object sits at the start of its range
        // and is 0x80 bytes, its sentinel block header included; blocks
        // tile the rest: 0x20-byte header, +0x4 size including it.
        kPoolStart = 0x4,
        kPoolEnd = 0x8,
        kPoolSentinel = 0x20,
        kPoolObject = 0x80,
        kBlockSize = 0x4,
        kBlockNext = 0x8,
        kBlockHeader = 0x20,
        kArchiveMagic = 0x41524300, // "ARC\0"
        kMaxChunks = 2,
        kRecordHeader = 8,  // per segment in the slot: address, length
        kMinRoom = kRecordHeader + 0x40,
        kMaxExcluded = 8,
        // Chunked real-Wii restore/capture bounds (Broadway L1D is 32 KiB):
        // 16 KiB copy+dcbst tiles stay L1-hot; 64 KiB IRQ yields keep
        // MSR[EE]=0 spans well under the 3-5 ms AX audio DMA period.
        kRestoreTileBytes = 16384,
        kIrqYieldBytes = 65536,
        // gfPadSystem +0x30: pad-thread samples not yet consumed; the loop
        // runs that many game steps (1-3) after reading the system pads
        // (0x800172AC-0x800172CC, in fn_80016F8C).
        kPadPendingSamples = 0x30,
        // GameGlobal -> gmGlobalModeMelee: +0x1A stage kind, +0x98 four
        // 0x5C-byte player records (+0 character kind, +1 state, +5 costume
        // file); read in the emulator tests.
        kModeStage = 0x1A,
        kModePlayers = 0x98,
        kModePlayerSize = 0x5C,
        kPlayers = 4
    };

    static OSModuleInfo* const* const kModuleHead = reinterpret_cast<OSModuleInfo* const*>(0x800030C8);
    // The match's frame counter, +1 per simulated game step (g_GameFrame
    // +0x4, a GameGlobal heap block created at boot; the address is not a
    // symbol; both measured, setup not recorded).
    static const volatile u32* const kGameFrame = reinterpret_cast<const volatile u32*>(0x901812A4);

    enum HeapClass { kClassGameplay, kClassFighterInstance, kClassResource };

    struct HeapRule {
        u8 id;
        u8 cls;
    };

    static const HeapRule kRules[] = {
        {Heaps::System, kClassGameplay},
        {Heaps::Effect, kClassGameplay},
        {Heaps::FighterEffect, kClassGameplay},
        {Heaps::StageInstance, kClassGameplay},
        {Heaps::ItemInstance, kClassGameplay},
        {Heaps::Physics, kClassGameplay},
        {Heaps::InfoInstance, kClassGameplay},
        {Heaps::GameGlobal, kClassGameplay},
        {Heaps::GlobalMode, kClassGameplay},
        {Heaps::Tmp, kClassGameplay},
        {Heaps::MenuInstance, kClassGameplay},
        {Heaps::Fighter1Instance, kClassFighterInstance},
        {Heaps::Fighter2Instance, kClassFighterInstance},
        {Heaps::Fighter3Instance, kClassFighterInstance},
        {Heaps::Fighter4Instance, kClassFighterInstance},
        {Heaps::Fighter1Resource, kClassResource},
        {Heaps::Fighter2Resource, kClassResource},
        {Heaps::Fighter3Resource, kClassResource},
        {Heaps::Fighter4Resource, kClassResource},
        {Heaps::Fighter1Resource2, kClassResource},
        {Heaps::Fighter2Resource2, kClassResource},
        {Heaps::Fighter3Resource2, kClassResource},
        {Heaps::Fighter4Resource2, kClassResource},
        {Heaps::FighterTechqniq, kClassResource},
        {Heaps::FighterKirbyResource1, kClassResource},
        {Heaps::FighterKirbyResource2, kClassResource},
        {Heaps::FighterKirbyResource3, kClassResource},
        {Heaps::StageResource, kClassResource},
        {Heaps::ItemResource, kClassResource},
        {Heaps::ItemExtraResource, kClassResource},
        {Heaps::PokemonResource, kClassResource},
        {Heaps::AssistFigureResource, kClassResource},
        {Heaps::CommonResource, kClassResource}
    };
    // Names matching kRules in g_HeapInfos order (NUL-separated).
    static const char kRuleNames[] =
        "System\0Effect\0FighterEffect\0StageInstance\0ItemInstance\0Physics\0InfoInstance\0"
        "GameGlobal\0GlobalMode\0Tmp\0MenuInstance\0"
        "Fighter1Instance\0Fighter2Instance\0Fighter3Instance\0Fighter4Instance\0"
        "Fighter1Resoruce\0Fighter2Resoruce\0Fighter3Resoruce\0Fighter4Resoruce\0"
        "Fighter1Resoruce2\0Fighter2Resoruce2\0Fighter3Resoruce2\0Fighter4Resoruce2\0"
        "FighterTechqniq\0FighterKirbyResource1\0FighterKirbyResource2\0FighterKirbyResource3\0"
        "StageResoruce\0IteamResource\0ItemExtraResource\0PokemonResource\0AssistFigureResource\0CommonResource\0";
    enum { kRuleCount = sizeof(kRules) / sizeof(kRules[0]) };

    // The slot lives in these heaps, which a match with two fighters leaves
    // empty (each holds one fighter's files when there are three or four).
    static const u8 kSlotHeaps[kMaxChunks] = {Heaps::Fighter4Resource, Heaps::Fighter3Resource};

    struct Range {
        u32 addr;
        u32 len;
    };

    // Match state in the DOL's data (RSBE01 rev 2 symbols) and P+'s areas.
    // All but the task id counter are ranges of Brawl Online's rollback
    // lists (gp-v*.json, dolphin/Data/Sys/Rollback), same length (the
    // AreaManager's shorter here). Pad, controller, OS, sound, GX and file
    // IO state are left out on purpose.
    static const u32 kFixedAddr[] = {
        0x805A00B8, // g_mtRand
        0x805A0420, // second mtRand
        0x8059FF58, // libc rand state
        0x804977B4, // g_mtPrngLogManager
        0x8059C668, // task id counter
        0x807824D4, // a Project+ variable in sora_melee .text
        0x80494E00, // scene/model render records
        0x804953B0, // s_gfSlowManager
        0x805A0080, // gfSlowManager s_needsUpdate
        0x8059C690, // gfSlowManager s_maxSlowRate
        0x8049DEDC, // camera subject list head
        0x8049E020,
        0x8049E570, // ground collision list heads
        0x8049EDD8, // nw4r::ef EffectSystem
        0x804A3DE0,
        0x804A3E70, // nw4r::ef resource list
        0x804A3EC0,
        0x804A3F20,
        0x804A3F60,
        0x804A3FE0,
        0x804A4580,
        0x804A4680,
        0x804A56C0,
        0x804A5700,
        0x804A5740,
        0x804A7F40,
        0x804A9A80,
        0x804AA640,
        0x804AA6E0,
        0x804AA740,
        0x804ADB20,
        0x804ADC80,
        0x804AFA80,
        0x805A0000, // .sbss main-thread globals
        0x805A0100,
        0x805A0280,
        0x805A0340,
        0x805A03C0,
        0x8059C480, // .sdata main-thread globals
        0x8059E900,
        0x805B8A00, // gfTaskScheduler
        0x805B6D20, // gfCameraManager
        0x805B69E0, // camera quake controller
        0x805B6200, // AreaManager
        0x80584080, // Project+ hold-to-pause / scratch / per-port records
        0x805A7400,
        0x935F0000
    };
    static const u16 kFixedLen[] = {
        0x8, 0x8, 0x4, 0xC, 0x4, 0x4, 0x3C0, 0x20, 0x8, 0x8, 0xC, 0x40, 0x30, 0x5068,
        0x60, 0x20, 0x40, 0x20, 0x20, 0x20, 0x40, 0x20, 0x20, 0x20, 0x20, 0x40, 0xB20,
        0x40, 0x40, 0x2E0, 0x140, 0x100, 0x20, 0x40, 0x80, 0x80, 0x40, 0x40, 0x40, 0x40,
        0x140, 0x740, 0x280, 0x80, 0x20, 0x2000, 0x2000
    };
    enum { kFixedCount = sizeof(kFixedAddr) / sizeof(kFixedAddr[0]) };
    TM_STATIC_ASSERT(kFixedCount == sizeof(kFixedLen) / sizeof(kFixedLen[0]), tm_fixed_lens_match);

    // Inside the ranges above but never copied: the four ranges the newest
    // of Brawl Online's lists (gp-v29.json) excludes.
    static const Range kExcluded[] = {
        {0x80611F40, 0x20},  // GX FIFO ring tail
        {0x805B7660, 0x20},  // a thread stack's bottom
        {0x804A3E90, 0x578}, // nw4r::ef draw strategies
        {0x805B5034, 0x20}   // frame buffer slot bookkeeping
    };
    TM_STATIC_ASSERT(sizeof(kExcluded) / sizeof(kExcluded[0]) <= kMaxExcluded, tm_excluded_fit);

    // A Project+ variable inside code: written as data, but invalidated from
    // the instruction cache too after a load in case a fetch ever reads it.
    static const u32 kCodeWord = 0x807824D4;
    // RSBE01 rev 2 Revolution OS scheduler lock: hardware ISRs (AI/AX audio
    // DMA, VI, SI, IOS IPC) still fire during 64 KiB IRQ yields, while no
    // OSThread (such as gfPadSystem) can preempt and touch half-copied heaps.
    static s32 (*const kOSDisableScheduler)() = reinterpret_cast<s32 (*)()>(0x801E0760);
    static s32 (*const kOSEnableScheduler)() = reinterpret_cast<s32 (*)()>(0x801E079C);

    struct Chunk {
        u32 addr;
        u32 len;
    };

    // Two slots share the buffers (kSlotQuick, kSlotRecording): each lies in
    // [lo[c], hi[c]) of chunk c, one after the other in chunk order.
    struct Slot {
        bool valid;
        u32 lo[kMaxChunks];
        u32 hi[kMaxChunks];
        u32 tailChunk; // first record of the atomic kFixed tail
        u32 tailPos;
        u32 checksum;
        u32 layout;   // heaps and modules where they were
        u32 archives; // the resource heaps' loaded files
        u8 setup[kPlayers * 3 + 2];
    };

    static Chunk s_chunks[kMaxChunks];
    static u32 s_chunkCount = 0;
    static Slot s_slot[kSlotCount];
    static SlotInfo s_info[kSlotCount];
    static Range s_own[kMaxOwnExclusions]; // buffers of ours inside heaps of the set
    static bool s_dropped = false;

    static inline void yieldIrq(BOOL& level, u32& irqStart, u32& maxIrqOff)
    {
#ifdef TM_PERF
        u32 delta = OSGetTick() - irqStart;
        if (delta > maxIrqOff)
        {
            maxIrqOff = delta;
        }
#else
        (void)irqStart;
        (void)maxIrqOff;
#endif
        OSRestoreInterrupts(level);
        level = OSDisableInterrupts();
#ifdef TM_PERF
        irqStart = OSGetTick();
#endif
    }

    static u32 field(u32 address, u32 offset)
    {
        return *reinterpret_cast<const u32*>(address + offset);
    }

    static u32 poolOf(u32 id)
    {
        u32 pool = reinterpret_cast<u32>(g_HeapInfos[id].m_memoryPool);
        return game::isRam(reinterpret_cast<void*>(pool)) ? pool : 0;
    }

    static bool chunkHeap(u32 id)
    {
        for (u32 i = 0; i < s_chunkCount; i++)
        {
            if (id == kSlotHeaps[i])
            {
                return true;
            }
        }
        return false;
    }

    // Copies `blocks` 128-byte chunks between 8-byte-aligned buffers using
    // volatile 64-bit FPRs (f0-f7). PowerPC lfd/stfd transfer raw 64-bit
    // patterns without altering FPSCR; MSR[FP] stays enabled on Brawl's main
    // thread while interrupts are disabled.
    static asm void copy128(register void* dst, register const void* src, register u32 blocks)
    {
        nofralloc
        mtctr   r5
    loop:
        lfd     f0, 0(r4)
        lfd     f1, 8(r4)
        lfd     f2, 16(r4)
        lfd     f3, 24(r4)
        lfd     f4, 32(r4)
        lfd     f5, 40(r4)
        lfd     f6, 48(r4)
        lfd     f7, 56(r4)
        stfd    f0, 0(r3)
        stfd    f1, 8(r3)
        stfd    f2, 16(r3)
        stfd    f3, 24(r3)
        stfd    f4, 32(r3)
        stfd    f5, 40(r3)
        stfd    f6, 48(r3)
        stfd    f7, 56(r3)
        lfd     f0, 64(r4)
        lfd     f1, 72(r4)
        lfd     f2, 80(r4)
        lfd     f3, 88(r4)
        lfd     f4, 96(r4)
        lfd     f5, 104(r4)
        lfd     f6, 112(r4)
        lfd     f7, 120(r4)
        stfd    f0, 64(r3)
        stfd    f1, 72(r3)
        stfd    f2, 80(r3)
        stfd    f3, 88(r3)
        stfd    f4, 96(r3)
        stfd    f5, 104(r3)
        stfd    f6, 112(r3)
        stfd    f7, 120(r3)
        addi    r4, r4, 128
        addi    r3, r3, 128
        bdnz+   loop
        blr
    }

    // Writes modified cache lines back to main RAM for GPU visibility (dcbst,
    // keeping lines warm in L1), unrolled 8 lines (256 bytes) per loop.
    // Caller issues one barrier (DCFlushRange) after all ranges finish.
    static asm void storeCacheRangeNoSync(register u32 addr, register u32 len)
    {
        nofralloc
        cmplwi  r4, 0
        blelr-
        clrlwi  r5, r3, 27
        add     r4, r4, r5
        addi    r4, r4, 31
        srwi    r4, r4, 5
        srwi.   r5, r4, 3
        beq-    tail
        mtctr   r5
        li      r6, 32
        li      r7, 64
        li      r8, 96
        li      r9, 128
        li      r10, 160
        li      r11, 192
        li      r12, 224
    loop8:
        dcbst   0, r3
        dcbst   r6, r3
        dcbst   r7, r3
        dcbst   r8, r3
        dcbst   r9, r3
        dcbst   r10, r3
        dcbst   r11, r3
        dcbst   r12, r3
        addi    r3, r3, 256
        bdnz+   loop8
    tail:
        clrlwi. r4, r4, 29
        beqlr-
        mtctr   r4
    loop1:
        dcbst   0, r3
        addi    r3, r3, 32
        bdnz+   loop1
        blr
    }

    // The DOL's memcpy (0x80004338) copies bytes; nearly every segment is
    // 8-byte aligned (slots pad records to 8 bytes), so 64-bit FPR loads and
    // stores cut the memory instructions in half vs 32-bit GPR copies.
    static void copy(void* dst, const void* src, u32 n)
    {
        u32 d = reinterpret_cast<u32>(dst);
        u32 s = reinterpret_cast<u32>(src);
        if (((d | s | n) & 3) != 0)
        {
            memcpy(dst, src, n);
            return;
        }
#ifdef TM_PERF
        if (!TM_PERF_LEGACY())
#endif
        {
            if (n >= 128 && ((d ^ s) & 7) == 0)
            {
                if ((d & 7) != 0)
                {
                    *reinterpret_cast<u32*>(d) = *reinterpret_cast<const u32*>(s);
                    d += 4;
                    s += 4;
                    n -= 4;
                }
                u32 blocks = n >> 7;
                if (blocks != 0)
                {
                    copy128(reinterpret_cast<void*>(d), reinterpret_cast<const void*>(s), blocks);
                    u32 done = blocks << 7;
                    d += done;
                    s += done;
                    n -= done;
                }
                double* to64 = reinterpret_cast<double*>(d);
                const double* from64 = reinterpret_cast<const double*>(s);
                u32 dwords = n >> 3;
                for (; dwords > 0; dwords--)
                {
                    *to64++ = *from64++;
                }
                d = reinterpret_cast<u32>(to64);
                s = reinterpret_cast<u32>(from64);
                n &= 7u;
            }
            else if (n >= 8 && ((d | s) & 7) == 0)
            {
                double* to64 = reinterpret_cast<double*>(d);
                const double* from64 = reinterpret_cast<const double*>(s);
                u32 dwords = n >> 3;
                for (; dwords > 0; dwords--)
                {
                    *to64++ = *from64++;
                }
                d = reinterpret_cast<u32>(to64);
                s = reinterpret_cast<u32>(from64);
                n &= 7u;
            }
        }
        u32* to = reinterpret_cast<u32*>(d);
        const u32* from = reinterpret_cast<const u32*>(s);
        u32 words = n >> 2;
        for (; words >= 8; words -= 8, to += 8, from += 8)
        {
            u32 a = from[0], b = from[1], c = from[2], e = from[3];
            u32 f = from[4], g = from[5], h = from[6], i = from[7];
            to[0] = a;
            to[1] = b;
            to[2] = c;
            to[3] = e;
            to[4] = f;
            to[5] = g;
            to[6] = h;
            to[7] = i;
        }
        for (; words > 0; words--)
        {
            *to++ = *from++;
        }
    }

    static u32 mix(u32 h, u32 v)
    {
        return (h ^ v) * 16777619u;
    }

    // The slot's check: every record's address and length, and one data
    // word in kSampleWords. The headers are what the load trusts to write
    // memory with interrupts off; a stray write over the data is caught
    // when it spans a sampled word. A full pass cost more than the copy
    // itself (9 ms of a 15 ms load, Mario and Fox on Final Destination).
    enum { kSampleWords = 64 };

    static inline u32 hashRecord(u32 h, u32 addr, u32 n, const u32* data)
    {
        h = ((h << 1) | (h >> 31)) + addr;
        h = ((h << 1) | (h >> 31)) + n;
        u32 words = n >> 2;
        u32 i = 0;
        for (; i + 3 * kSampleWords < words; i += 4 * kSampleWords)
        {
            h = ((h << 1) | (h >> 31)) + data[i];
            h = ((h << 1) | (h >> 31)) + data[i + kSampleWords];
            h = ((h << 1) | (h >> 31)) + data[i + 2 * kSampleWords];
            h = ((h << 1) | (h >> 31)) + data[i + 3 * kSampleWords];
        }
        for (; i < words; i += kSampleWords)
        {
            h = ((h << 1) | (h >> 31)) + data[i];
        }
        return h;
    }

    static bool overlapsExcluded(u32 lo, u32 hi)
    {
        u32 fixed = sizeof(kExcluded) / sizeof(kExcluded[0]);
        for (u32 i = 0; i < fixed + kMaxOwnExclusions; i++)
        {
            const Range& e = i < fixed ? kExcluded[i] : s_own[i - fixed];
            if (e.len != 0 && e.addr < hi && e.addr + e.len > lo)
            {
                return true;
            }
        }
        return false;
    }

    // ---- the slot writer ----------------------------------------------------

    struct Writer {
        Slot* slot;      // whose lo/hi the writer fills (NULL when measuring)
        u32 limitChunk;  // never writes at or past (limitChunk, limitPos)
        u32 limitPos;
        u32 chunk;
        u32 pos;
        u32 pendingLo;
        u32 pendingHi;
        bool overflow;
        bool measureOnly; // count bytes, copy nothing
        bool needV3Walk;  // fallback if an excluded range or 512+ free blocks hit a fighter heap
        u32 bytes;
        u32 segments;
        u32 checksum;
        u32 fighterFreeBytes;
        BOOL irqLevel;
        bool yieldEnabled;
        u32 sinceYield;
        u32 irqStart;
        u32 maxIrqOffTicks;
    };

    static void emit(Writer& w, u32 addr, u32 len)
    {
        if (w.measureOnly)
        {
            w.bytes += len;
            return;
        }
        while (len > 0 && !w.overflow)
        {
            if (w.chunk >= s_chunkCount || w.chunk > w.limitChunk)
            {
                w.overflow = true;
                return;
            }
            const Chunk& c = s_chunks[w.chunk];
            u32 end = w.chunk == w.limitChunk ? w.limitPos : c.len;
            u32 room = end > w.pos ? end - w.pos : 0;
            if (room < kMinRoom)
            {
                w.slot->hi[w.chunk] = w.pos;
                w.chunk++;
                w.pos = 0;
                continue;
            }
            u32 n = len < room - kRecordHeader ? len : (room - kRecordHeader) & ~7u;
            u32* head = reinterpret_cast<u32*>(c.addr + w.pos);
            head[0] = addr;
            head[1] = n;
            if (!w.yieldEnabled)
            {
                copy(head + 2, reinterpret_cast<const void*>(addr), n);
            }
            else
            {
                u32 done = 0;
                while (done < n)
                {
                    u32 step = n - done;
                    u32 budget = w.sinceYield < kIrqYieldBytes ? (kIrqYieldBytes - w.sinceYield) : kIrqYieldBytes;
                    if (step > budget)
                    {
                        step = budget;
                    }
                    if (done + step < n)
                    {
                        step &= ~7u;
                        if (step == 0)
                        {
                            step = n - done;
                        }
                    }
                    copy(reinterpret_cast<u8*>(head + 2) + done,
                         reinterpret_cast<const u8*>(addr) + done, step);
                    done += step;
                    w.sinceYield += step;
                    if (w.sinceYield >= kIrqYieldBytes)
                    {
                        yieldIrq(w.irqLevel, w.irqStart, w.maxIrqOffTicks);
                        w.sinceYield = 0;
                    }
                }
            }
            u32 padded = (n + 7u) & ~7u;
            if (padded > n)
            {
                u8* tail = reinterpret_cast<u8*>(head + 2) + n;
                for (u32 z = 0; z < padded - n; z++)
                {
                    tail[z] = 0;
                }
            }
            w.checksum = hashRecord(w.checksum, addr, n, head + 2);
            w.pos += kRecordHeader + padded;
            w.bytes += n;
            w.segments++;
            addr += n;
            len -= n;
        }
    }

    // [lo, hi) minus the excluded ranges.
    static void emitExcluding(Writer& w, u32 lo, u32 hi)
    {
        u32 cur = lo;
        u32 fixed = sizeof(kExcluded) / sizeof(kExcluded[0]);
        while (cur < hi)
        {
            u32 next = hi;
            bool skipped = false;
            for (u32 i = 0; i < fixed + kMaxOwnExclusions; i++)
            {
                const Range& e = i < fixed ? kExcluded[i] : s_own[i - fixed];
                u32 elo = e.addr;
                u32 ehi = elo + e.len;
                if (e.len == 0)
                {
                    continue;
                }
                if (elo <= cur && cur < ehi)
                {
                    cur = ehi;
                    skipped = true;
                    break;
                }
                if (elo > cur && elo < next)
                {
                    next = elo;
                }
            }
            if (!skipped)
            {
                emit(w, cur, next - cur);
                cur = next;
            }
        }
    }

    static void flush(Writer& w)
    {
        if (w.pendingHi > w.pendingLo)
        {
            emitExcluding(w, w.pendingLo, w.pendingHi);
        }
        w.pendingLo = w.pendingHi = 0;
    }

    // Adds [addr, addr+len), joined with the previous range when they touch.
    static void add(Writer& w, u32 addr, u32 len)
    {
        if (len == 0)
        {
            return;
        }
        if (addr == w.pendingHi)
        {
            w.pendingHi += len;
            return;
        }
        flush(w);
        w.pendingLo = addr;
        w.pendingHi = addr + len;
    }

    // ---- heaps and modules ----------------------------------------------------

    // Walks one heap's blocks; with `w` adds them by its class, else only
    // checks the chain. Archive blocks of resource heaps go into *archives.
    static bool walkHeap(Writer* w, u32 id, u32 cls, u32* archives)
    {
        u32 pool = poolOf(id);
        if (pool == 0 || chunkHeap(id))
        {
            return true;
        }
        u32 start = field(pool, kPoolStart);
        u32 end = field(pool, kPoolEnd);
        if (start != pool || end <= start + kPoolObject)
        {
            return false;
        }
        game::HeapUsage usage;
        if (cls == kClassFighterInstance && !g_probe.rulesV3 && game::heapUsage(id, usage) && usage.allocated != 0)
        {
            if (w != NULL)
            {
                add(*w, start, end - start);
                if (!overlapsExcluded(start, end) && usage.freeBlocks < 512)
                {
                    w->fighterFreeBytes += usage.freeBytes;
                }
                else
                {
                    w->needV3Walk = true;
                }
            }
            return true;
        }
        if (w != NULL)
        {
            add(*w, pool, kPoolObject);
        }
        u32 nextFree = field(field(pool, kPoolSentinel), kBlockNext);
        u32 b = start + kPoolObject;
        while (b < end)
        {
            u32 size = field(b, kBlockSize);
            if (size < kBlockHeader || (size & 0x1F) != 0 || size > end - b)
            {
                return false;
            }
            if (b == nextFree)
            {
                if (w != NULL)
                {
                    add(*w, b, kBlockHeader);
                }
                nextFree = field(b, kBlockNext);
            }
            else if (cls == kClassResource && field(b, kBlockHeader) == kArchiveMagic)
            {
                if (archives != NULL)
                {
                    *archives = mix(mix(mix(*archives, b), size), field(b, kBlockHeader + 4));
                }
            }
            else if (w != NULL)
            {
                add(*w, b, size);
            }
            b += size;
        }
        return true;
    }

    static bool sectionHolds(OSModuleInfo* m, u32 address)
    {
        OSSectionInfo* secs = reinterpret_cast<OSSectionInfo*>(m->sectionInfoOffset);
        for (u32 i = 0; i < m->numSections && i < 32; i++)
        {
            u32 lo = secs[i].offset & ~OS_SECTIONINFO_EXEC;
            if (lo != 0 && address >= lo && address < lo + secs[i].size)
            {
                return true;
            }
        }
        return false;
    }

    // Our own modules: the stub, the module this code is in, and the extra
    // module loaded next to it (ids follow the stub's: match, menu, extra;
    // the Makefile's *_ID).
    static bool ours(OSModuleInfo* m)
    {
        return (m->id >= TM_STUB_MODULE_ID && m->id <= TM_STUB_MODULE_ID + 3)
               || sectionHolds(m, reinterpret_cast<u32>(&s_slot[0]));
    }

    // The data sections (.data and .bss: sections 5 and 6) of every loaded
    // module but ours; `layout` collects where they are.
    static void walkModules(Writer* w, u32* layout)
    {
        OSModuleInfo* m = *kModuleHead;
        for (u32 count = 0; game::isRam(m) && count < 64; count++)
        {
            if (!ours(m) && game::isRam(reinterpret_cast<void*>(m->sectionInfoOffset)))
            {
                OSSectionInfo* secs = reinterpret_cast<OSSectionInfo*>(m->sectionInfoOffset);
                for (u32 i = 5; i <= 6 && i < m->numSections; i++)
                {
                    u32 off = secs[i].offset;
                    if ((off & OS_SECTIONINFO_EXEC) == 0 && secs[i].size != 0 &&
                        game::isRam(reinterpret_cast<void*>(off)))
                    {
                        if (w != NULL)
                        {
                            add(*w, off, secs[i].size);
                        }
                        if (layout != NULL)
                        {
                            *layout = mix(mix(mix(*layout, m->id), off), secs[i].size);
                        }
                    }
                }
            }
            m = m->link.next;
        }
    }

    // Where the set's heaps and modules are, and the resource heaps' files.
    static bool fingerprint(u32& layout, u32& archives)
    {
        layout = 2166136261u;
        archives = 2166136261u;
        for (u32 r = 0; r < kRuleCount; r++)
        {
            u32 id = kRules[r].id;
            u32 pool = poolOf(id);
            if (pool != 0 && !chunkHeap(id))
            {
                layout = mix(mix(mix(layout, id), pool), field(pool, kPoolEnd));
            }
            if (!walkHeap(NULL, id, kRules[r].cls, kRules[r].cls == kClassResource ? &archives : NULL))
            {
                return false;
            }
        }
        walkModules(NULL, &layout);
        // Our own buffers inside heaps of the set (e.g. recorded inputs) can
        // be allocated after match start: a state saved before that must not
        // be loaded once an exclusion changes.
        for (u32 i = 0; i < kMaxOwnExclusions; i++)
        {
            layout = mix(mix(layout, s_own[i].addr), s_own[i].len);
        }
        return true;
    }

    static bool walkAll(Writer& w, u32* layout, u32* archives)
    {
        if (layout != NULL)
        {
            *layout = 2166136261u;
        }
        if (archives != NULL)
        {
            *archives = 2166136261u;
        }
        for (u32 r = 0; r < kRuleCount; r++)
        {
            u32 id = kRules[r].id;
            u32 cls = kRules[r].cls;
            if (layout != NULL)
            {
                u32 pool = poolOf(id);
                if (pool != 0 && !chunkHeap(id))
                {
                    *layout = mix(mix(mix(*layout, id), pool), field(pool, kPoolEnd));
                }
            }
            if (!walkHeap(&w, id, cls, cls == kClassResource ? archives : NULL))
            {
                return false;
            }
        }
        walkModules(&w, layout);
        if (layout != NULL)
        {
            for (u32 i = 0; i < kMaxOwnExclusions; i++)
            {
                *layout = mix(mix(*layout, s_own[i].addr), s_own[i].len);
            }
        }
        // Finish Tier 1 bulk heaps/modules, then enter the short atomic kFixed tail.
        flush(w);
        if (!w.measureOnly)
        {
            if (w.yieldEnabled && w.sinceYield > 0)
            {
                yieldIrq(w.irqLevel, w.irqStart, w.maxIrqOffTicks);
                w.sinceYield = 0;
            }
            w.yieldEnabled = false;
            if (w.slot != NULL)
            {
                w.slot->tailChunk = w.chunk;
                w.slot->tailPos = w.pos;
            }
        }
        for (u32 i = 0; i < kFixedCount; i++)
        {
            add(w, kFixedAddr[i], kFixedLen[i]);
        }
        flush(w);
        return true;
    }

    // ---- match identity ------------------------------------------------------

    // Stage kind and each player's character, state (human, CPU, none) and
    // costume, as the menus handed them to the match.
    static bool readSetup(u8* out)
    {
        GameGlobal* global = g_GameGlobal;
        if (!game::isRam(global) || !game::isRam(global->m_modeMelee))
        {
            return false;
        }
        u32 mode = reinterpret_cast<u32>(global->m_modeMelee);
        out[0] = *reinterpret_cast<const u8*>(mode + kModeStage);
        out[1] = *reinterpret_cast<const u8*>(mode + kModeStage + 1);
        for (u32 i = 0; i < kPlayers; i++)
        {
            const u8* p = reinterpret_cast<const u8*>(mode + kModePlayers + kModePlayerSize * i);
            out[2 + 3 * i] = p[0];
            out[3 + 3 * i] = p[1];
            out[4 + 3 * i] = p[5];
        }
        return true;
    }

    static bool sameSetup(const u8* a, const u8* b)
    {
        for (u32 i = 0; i < sizeof(s_slot[0].setup); i++)
        {
            if (a[i] != b[i])
            {
                return false;
            }
        }
        return true;
    }

    // ---- the copy ------------------------------------------------------------

    // Waits for the GPU to finish what it may still read; sleeps on the
    // draw-done interrupt. The game's frame-buffer copy (fn_80023AE4) also
    // passes 0x80023D90 to GXSetDrawDoneCallback (0x80023BA8) when its
    // object's +0x74 flag is set; the callback variable (r13 -0x3B5C) read
    // 0 (measured; point in the frame not recorded).
    static void waitGpuIdle()
    {
        GXDrawDone();
    }

    // After a stall of ours, count one pad sample so the loop runs no
    // catch-up steps (measured, setup not recorded: K traced frames for K
    // frames, every cycle). The main loop clamps +0x30 to 1-3 and passes it
    // to consumeFrameCounter (0x800172AC-0x800172D8).
    void forgetStall()
    {
        gfPadSystem* system = g_gfPadSystem;
        if (game::isRam(system))
        {
            BOOL level = OSDisableInterrupts();
            *reinterpret_cast<u32*>(reinterpret_cast<u32>(system) + kPadPendingSamples) = 1;
            OSRestoreInterrupts(level);
        }
    }

    // False if a record runs past its slot's end (a length overwritten):
    // nothing past it is read.
    static bool checkSlot(const Slot& slot, u32& sum)
    {
        u32 h = 0;
        for (u32 c = 0; c < s_chunkCount; c++)
        {
            u32 pos = slot.lo[c];
            while (pos + kRecordHeader <= slot.hi[c])
            {
                const u32* head = reinterpret_cast<const u32*>(s_chunks[c].addr + pos);
                u32 n = head[1];
                if (n > slot.hi[c] - pos - kRecordHeader)
                {
                    return false;
                }
                h = hashRecord(h, head[0], n, head + 2);
                pos += kRecordHeader + ((n + 7u) & ~7u);
            }
        }
        sum = h;
        return true;
    }

#ifdef TM_PERF
    // The whole-slot pass the check replaced, kept for the calibration.
    static u32 checksumSlot(const Slot& slot)
    {
        u32 h = 0;
        for (u32 c = 0; c < s_chunkCount; c++)
        {
            const u32* p = reinterpret_cast<const u32*>(s_chunks[c].addr + slot.lo[c]);
            const u32* e = reinterpret_cast<const u32*>(s_chunks[c].addr + slot.hi[c]);
            for (; p < e; p++)
            {
                h = ((h << 1) | (h >> 31)) + *p;
            }
        }
        return h;
    }
#endif

    static bool takeBuffers()
    {
        if (s_chunkCount != 0)
        {
            return true;
        }
        for (u32 i = 0; i < kMaxChunks; i++)
        {
            game::HeapUsage usage;
            if (!game::heapUsage(kSlotHeaps[i], usage) || usage.allocated != 0 || usage.largest < 0x100000)
            {
                return false;
            }
        }
        for (u32 i = 0; i < kMaxChunks; i++)
        {
            game::HeapUsage usage;
            game::heapUsage(kSlotHeaps[i], usage);
            u32 size = (usage.largest - 0x40) & ~0x1Fu;
            void* p = gfHeapManager::alloc(static_cast<Heaps::HeapType>(kSlotHeaps[i]), size);
            if (p == NULL)
            {
                release();
                return false;
            }
            s_chunks[i].addr = reinterpret_cast<u32>(p);
            s_chunks[i].len = size;
            s_chunkCount = i + 1;
        }
        for (u32 k = 0; k < kSlotCount; k++)
        {
            s_info[k].capacity = s_chunks[0].len + s_chunks[1].len;
        }
        TM_LOG("savestate: slot buffers %08x+%x, %08x+%x\n", s_chunks[0].addr, s_chunks[0].len, s_chunks[1].addr,
               s_chunks[1].len);
        return true;
    }

    // Where a slot begins and ends, in chunk order (chunk, offset).
    static bool firstOf(const Slot& slot, u32& chunk, u32& pos)
    {
        for (u32 c = 0; c < s_chunkCount; c++)
        {
            if (slot.hi[c] > slot.lo[c])
            {
                chunk = c;
                pos = slot.lo[c];
                return true;
            }
        }
        return false;
    }

    static bool lastOf(const Slot& slot, u32& chunk, u32& pos)
    {
        for (u32 c = s_chunkCount; c-- > 0;)
        {
            if (slot.hi[c] > slot.lo[c])
            {
                chunk = c;
                pos = slot.hi[c];
                return true;
            }
        }
        return false;
    }

    static u32 lin(u32 chunk, u32 pos)
    {
        return (chunk << 28) + pos;
    }

    // Writes the region set into `slot` from (chunk, pos), never reaching
    // (limitChunk, limitPos), while computing layout, archives, and checksum.
    static bool write(Slot& slot, u32 chunk, u32 pos, u32 limitChunk, u32 limitPos, Writer& w, bool& walked,
                      u32* layout, u32* archives)
    {
        for (u32 c = 0; c < kMaxChunks; c++)
        {
            slot.lo[c] = 0;
            slot.hi[c] = 0;
        }
        slot.lo[chunk] = pos;
        slot.tailChunk = kMaxChunks;
        slot.tailPos = 0;
        Writer fresh = {&slot, limitChunk, limitPos, chunk, pos, 0, 0, false, false, false, 0, 0, 0, 0, 0,
                        !TM_PERF_LEGACY(), 0, 0, 0};
        w = fresh;
        kOSDisableScheduler();
        w.irqLevel = OSDisableInterrupts();
#ifdef TM_PERF
        w.irqStart = OSGetTick();
#endif
        walked = walkAll(w, layout, archives);
        if (w.chunk < kMaxChunks)
        {
            slot.hi[w.chunk] = w.pos;
        }
#ifdef TM_PERF
        u32 tailDelta = OSGetTick() - w.irqStart;
        if (tailDelta > w.maxIrqOffTicks)
        {
            w.maxIrqOffTicks = tailDelta;
        }
#endif
        OSRestoreInterrupts(w.irqLevel);
        kOSEnableScheduler();
        return walked && !w.overflow;
    }

    void invalidate(int which)
    {
        if (which >= 0 && which < kSlotCount)
        {
            s_slot[which].valid = false;
            s_info[which].valid = false;
        }
    }

    bool droppedOther()
    {
        return s_dropped;
    }

    void setOwnExclusionAt(u32 index, u32 addr, u32 len)
    {
        if (index < kMaxOwnExclusions)
        {
            s_own[index].addr = addr;
            s_own[index].len = len;
        }
    }

    void setOwnExclusion(u32 addr, u32 len)
    {
        setOwnExclusionAt(0, addr, len);
    }

    // At its slot's home, else the start, else after the other slot's
    // state, where it does not overlap it; if none fits, the recording's
    // state takes the buffers whole (the quick state is dropped) and a quick
    // state is refused.
    Result capture(int which, u32 loopPass)
    {
        TM_PERF_SCOPE(kSaveCapture);
        if (!takeBuffers())
        {
            return kErrNoRoom;
        }
        Slot& slot = s_slot[which];
        SlotInfo& info = s_info[which];
        Slot& other = s_slot[1 - which];
        s_dropped = false;
        slot.valid = false;
        info.valid = false;
        u8 setup[sizeof(slot.setup)];
        u32 layout = 0, archives = 0;
        if (!readSetup(setup))
        {
            return kErrWalk;
        }
        bool legacy = TM_PERF_LEGACY();
        if (legacy)
        {
            bool fpOk;
            {
                TM_PERF_SCOPE(kSaveFingerprint);
                fpOk = fingerprint(layout, archives);
            }
            if (!fpOk)
            {
                return kErrWalk;
            }
        }
        {
            TM_PERF_SCOPE(kSaveWaitGpu);
            waitGpuIdle();
        }
        u32 endChunk = s_chunkCount - 1;
        u32 endPos = s_chunks[endChunk].len;
        u32 oc0 = 0, op0 = 0, oc1 = 0, op1 = 0;
        bool otherThere = other.valid && firstOf(other, oc0, op0) && lastOf(other, oc1, op1);
        Writer w;
        bool walked = true;
        bool ok = false;
        u32 t0 = OSGetTick();
        {
            TM_PERF_SCOPE(kSaveWrite);
            // Each slot has a home chunk (the recording's the first, the quick
            // state's the last), so two sets that each fit a chunk never move;
            // a larger set goes wherever it fits next to the other.
            u32 home = which == kSlotRecording ? 0 : endChunk;
            u32 starts[3][2] = {{home, 0}, {0, 0}, {oc1, op1}};
            u32 tries = otherThere ? 3 : 2;
            for (u32 k = 0; k < tries && !ok && walked; k++)
            {
                u32 sc = starts[k][0];
                u32 sp = starts[k][1];
                u32 lc = endChunk;
                u32 lp = endPos;
                if (otherThere)
                {
                    if (lin(sc, sp) >= lin(oc0, op0) && lin(sc, sp) < lin(oc1, op1))
                    {
                        continue; // inside the other state
                    }
                    if (lin(oc0, op0) >= lin(sc, sp))
                    {
                        lc = oc0;
                        lp = op0;
                    }
                }
                ok = write(slot, sc, sp, lc, lp, w, walked, legacy ? NULL : &layout, legacy ? NULL : &archives);
            }
            if (!ok && walked && otherThere && which == kSlotRecording)
            {
                invalidate(1 - which);
                s_dropped = true;
                ok = write(slot, 0, 0, endChunk, endPos, w, walked, legacy ? NULL : &layout, legacy ? NULL : &archives);
            }
        }
        u32 t1 = OSGetTick();
        if (!walked)
        {
            return kErrWalk;
        }
        if (!ok)
        {
            return kErrTooBig;
        }
        if (legacy)
        {
            TM_PERF_SCOPE(kSaveCheck);
#ifdef TM_PERF
            slot.checksum = checksumSlot(slot);
#else
            checkSlot(slot, slot.checksum);
#endif
        }
        else
        {
            slot.checksum = w.checksum;
        }
        slot.layout = layout;
        slot.archives = archives;
        for (u32 i = 0; i < sizeof(setup); i++)
        {
            slot.setup[i] = setup[i];
        }
        slot.valid = true;
        u32 setBytesV3 = w.bytes - w.fighterFreeBytes;
        if (legacy || w.needV3Walk)
        {
            // Fallback or legacy measurement walk for setBytesV3.
            Writer m = {NULL, 0, 0, 0, 0, 0, 0, false, true, false, 0, 0, 0, 0, 0, false, 0, 0, 0};
            {
                TM_PERF_SCOPE(kSaveMeasureV3);
                for (u32 r = 0; r < kRuleCount; r++)
                {
                    walkHeap(&m, kRules[r].id,
                             kRules[r].cls == kClassFighterInstance ? kClassGameplay : kRules[r].cls, NULL);
                }
                walkModules(&m, NULL);
                for (u32 i = 0; i < kFixedCount; i++)
                {
                    add(m, kFixedAddr[i], kFixedLen[i]);
                }
                flush(m);
            }
            setBytesV3 = m.bytes;
        }
        info.valid = true;
        info.frame = *kGameFrame;
        info.loopPass = loopPass;
        info.bytes = w.bytes;
        info.segments = w.segments;
        info.setBytesV3 = setBytesV3;
        info.saveTicks = t1 - t0;
#ifdef TM_PERF
        info.saveMaxIrqOffTicks = w.maxIrqOffTicks;
        perf::add(perf::kSaveMaxIrqOff, w.maxIrqOffTicks);
#endif
        return kOk;
    }

    Result restore(int which, void (*beforeCopy)())
    {
        TM_PERF_SCOPE(kLoadRestore);
        Slot& slot = s_slot[which];
        SlotInfo& info = s_info[which];
        if (!slot.valid)
        {
            return kErrEmpty;
        }
        u8 setup[sizeof(slot.setup)];
        if (!readSetup(setup) || !sameSetup(setup, slot.setup))
        {
            return kErrMatch;
        }
        u32 layout, archives;
        bool fpOk;
        {
            TM_PERF_SCOPE(kLoadFingerprint);
            fpOk = fingerprint(layout, archives);
        }
        if (!fpOk)
        {
            return kErrWalk;
        }
        if (layout != slot.layout)
        {
            return kErrLayout;
        }
        if (archives != slot.archives)
        {
            return kErrResources;
        }
        u32 c0 = OSGetTick();
        u32 sum = 0;
        bool whole;
        {
            TM_PERF_SCOPE(kLoadCheck);
            whole = checkSlot(slot, sum);
#ifdef TM_PERF
            if (TM_PERF_LEGACY())
            {
                sum = checksumSlot(slot);
                whole = true;
            }
#endif
        }
        info.checkTicks = OSGetTick() - c0;
        if (!whole || sum != slot.checksum)
        {
            invalidate(which);
            return kErrChecksum;
        }
        {
            TM_PERF_SCOPE(kLoadWaitGpu);
            waitGpuIdle();
        }
        if (beforeCopy != NULL)
        {
            beforeCopy();
        }
        kOSDisableScheduler();
        BOOL level = OSDisableInterrupts();
        bool legacyFlush = TM_PERF_LEGACY();
        u32 irqStart = 0;
        u32 maxIrqOff = 0;
        u32 sinceYield = 0;
        bool inAtomicTail = legacyFlush;
#ifdef TM_PERF
        u32 copyTicks = 0;
        u32 flushTicks = 0;
        irqStart = OSGetTick();
#endif
        u32 t0 = OSGetTick();
        for (u32 c = 0; c < s_chunkCount; c++)
        {
            u32 pos = slot.lo[c];
            while (pos + kRecordHeader <= slot.hi[c])
            {
                if (!inAtomicTail && (c > slot.tailChunk || (c == slot.tailChunk && pos >= slot.tailPos)))
                {
                    if (sinceYield > 0)
                    {
                        yieldIrq(level, irqStart, maxIrqOff);
                        sinceYield = 0;
                    }
                    inAtomicTail = true;
                }
                const u32* head = reinterpret_cast<const u32*>(s_chunks[c].addr + pos);
                u32 addr = head[0];
                u32 n = head[1];
                const u8* src = reinterpret_cast<const u8*>(head + 2);
                if (legacyFlush)
                {
#ifdef TM_PERF
                    u32 st0 = OSGetTick();
#endif
                    copy(reinterpret_cast<void*>(addr), src, n);
#ifdef TM_PERF
                    u32 st1 = OSGetTick();
#endif
                    DCFlushRange(reinterpret_cast<void*>(addr), n);
#ifdef TM_PERF
                    u32 st2 = OSGetTick();
                    copyTicks += st1 - st0;
                    flushTicks += st2 - st1;
#endif
                }
                else
                {
                    // Tile bulk Tier 1 segments into 16 KiB L1-hot copy+dcbst
                    // chunks and yield IRQs every 64 KiB; hold interrupts off
                    // uninterrupted only across the short kFixed atomic tail.
                    u32 done = 0;
                    while (done < n)
                    {
                        u32 tile = n - done;
                        if (!inAtomicTail && tile > kRestoreTileBytes)
                        {
                            tile = kRestoreTileBytes;
                        }
                        if (done + tile < n && (tile & 7u) != 0u)
                        {
                            tile &= ~7u;
                            if (tile == 0)
                            {
                                tile = n - done;
                            }
                        }
                        u32 tileAddr = addr + done;
#ifdef TM_PERF
                        u32 st0 = OSGetTick();
#endif
                        copy(reinterpret_cast<void*>(tileAddr), src + done, tile);
#ifdef TM_PERF
                        u32 st1 = OSGetTick();
#endif
                        storeCacheRangeNoSync(tileAddr, tile);
#ifdef TM_PERF
                        u32 st2 = OSGetTick();
                        copyTicks += st1 - st0;
                        flushTicks += st2 - st1;
#endif
                        done += tile;
                        if (!inAtomicTail)
                        {
                            sinceYield += tile;
                            if (sinceYield >= kIrqYieldBytes)
                            {
                                yieldIrq(level, irqStart, maxIrqOff);
                                sinceYield = 0;
                            }
                        }
                    }
                }
                pos += kRecordHeader + ((n + 7u) & ~7u);
            }
        }
        if (!legacyFlush)
        {
#ifdef TM_PERF
            u32 st0 = OSGetTick();
#endif
            // One Revolution OS cache sync barrier (dcbf + sc) after all dcbst ranges.
            DCFlushRange(reinterpret_cast<void*>(kCodeWord & ~0x1Fu), 0x20);
#ifdef TM_PERF
            flushTicks += OSGetTick() - st0;
#endif
        }
        ICInvalidateRange(reinterpret_cast<void*>(kCodeWord & ~0x1Fu), 0x20);
        u32 t1 = OSGetTick();
#ifdef TM_PERF
        u32 tailDelta = t1 - irqStart;
        if (tailDelta > maxIrqOff)
        {
            maxIrqOff = tailDelta;
        }
#endif
        OSRestoreInterrupts(level);
        kOSEnableScheduler();
#ifdef TM_PERF
        info.loadMaxIrqOffTicks = maxIrqOff;
        perf::add(perf::kLoadCopy, copyTicks);
        perf::add(perf::kLoadFlush, flushTicks);
        perf::add(perf::kLoadMaxIrqOff, maxIrqOff);
#endif
        // Vertex and texture caches may hold what the GPU read before.
        GXInvalidateVtxCache();
        GXInvalidateTexAll();
        info.loadTicks = t1 - t0;
        return kOk;
    }

    void release()
    {
        for (u32 i = 0; i < s_chunkCount; i++)
        {
            gfHeapManager::free(reinterpret_cast<void*>(s_chunks[i].addr));
            s_chunks[i].addr = 0;
            s_chunks[i].len = 0;
        }
        s_chunkCount = 0;
        for (u32 k = 0; k < kSlotCount; k++)
        {
            invalidate(k);
            s_info[k].capacity = 0;
            for (u32 c = 0; c < kMaxChunks; c++)
            {
                s_slot[k].lo[c] = 0;
                s_slot[k].hi[c] = 0;
            }
        }
    }

    bool holdingBuffers()
    {
        return s_chunkCount != 0;
    }

    const SlotInfo& slotInfo(int which)
    {
        return s_info[which];
    }

    const char* resultText(Result r)
    {
        static const char kText[] =
            "ok\0no free heap for the state\0state too large\0heap walk failed\0no state saved\0"
            "saved in another match\0memory layout changed\0files loaded since the save\0"
            "state memory overwritten\0";
        if (r >= kResultCount)
        {
            return "?";
        }
        const char* p = kText;
        for (int i = 0; i < r; i++)
        {
            while (*p++ != '\0') {}
        }
        return p;
    }

    u32 usageHash()
    {
        u32 h = 2166136261u;
        for (u32 r = 0; r < kRuleCount; r++)
        {
            game::HeapUsage usage;
            if (!chunkHeap(kRules[r].id) && game::heapUsage(kRules[r].id, usage))
            {
                h = mix(mix(h, usage.allocated), usage.used);
            }
        }
        return h;
    }

    u32 fighterHeapHash()
    {
        u32 h = 2166136261u;
        for (u32 r = 0; r < kRuleCount; r++)
        {
            game::HeapUsage usage;
            if (kRules[r].cls == kClassFighterInstance && game::heapUsage(kRules[r].id, usage) && usage.allocated != 0)
            {
                const u32* p = reinterpret_cast<const u32*>(usage.start);
                const u32* e = reinterpret_cast<const u32*>(usage.end);
                for (; p < e; p++)
                {
                    h = mix(h, *p);
                }
            }
        }
        return h;
    }

#ifdef TM_PERF
    // A counted loop of one instruction (bdnz) per pass.
    static asm void spin(register u32 n)
    {
        nofralloc
        mtctr r3
    loop:
        bdnz loop
        blr
    }

    void perfDamageSlot(u32 how)
    {
        const Slot& slot = s_slot[kSlotQuick];
        u32 c = 0;
        u32 pos = 0;
        if (!slot.valid || !firstOf(slot, c, pos))
        {
            TM_LOG("perf: damage: no quick state\n");
            return;
        }
        u32* head = reinterpret_cast<u32*>(s_chunks[c].addr + pos);
        if (how == 1)
        {
            head[1] = 0x7FFFFFF0;
        }
        else
        {
            head[2] ^= 1;
        }
        TM_LOG("perf: damage: quick state's slot, kind %u, at %08x\n", how, head);
    }

    void perfCalibrate()
    {
        if (s_chunkCount == 0 || s_chunks[0].len < 0x400000)
        {
            TM_LOG("perf: calibrate: no slot buffers (save a state first)\n");
            return;
        }
        u8* p = reinterpret_cast<u8*>(s_chunks[0].addr);
        u32 t[10];
        u32 t0 = OSGetTick();
        spin(1000000);
        t[0] = OSGetTick() - t0;
        static const u32 kSizes[3] = {0x4000, 0x100000, 0x400000};
        for (u32 i = 0; i < 3; i++)
        {
            t0 = OSGetTick();
            copy(p, p, kSizes[i]);
            t[1 + i] = OSGetTick() - t0;
        }
        t0 = OSGetTick();
        memcpy(p, p, 0x100000);
        t[4] = OSGetTick() - t0;
        Slot probe;
        probe.lo[0] = 0;
        probe.hi[0] = 0x100000;
        for (u32 c = 1; c < kMaxChunks; c++)
        {
            probe.lo[c] = 0;
            probe.hi[c] = 0;
        }
        t0 = OSGetTick();
        u32 sum = checksumSlot(probe);
        t[5] = OSGetTick() - t0;
        t0 = OSGetTick();
        if (TM_PERF_LEGACY())
        {
            DCFlushRange(p, 0x100000);
        }
        else
        {
            storeCacheRangeNoSync(reinterpret_cast<u32>(p), 0x100000);
            DCFlushRange(p, 0x20);
        }
        t[6] = OSGetTick() - t0;
        t0 = OSGetTick();
        TM_LOG("perf: calibrate: a log line of %u characters\n", 40);
        t[7] = OSGetTick() - t0;
        t0 = OSGetTick();
        TM_LOG("perf: calibrate: a formatted line %d %u %08x %.2f\n", -5, 123456u, sum, 1.5f);
        t[8] = OSGetTick() - t0;
        TM_LOG("perf: calibrate ticks: spin1M=%u copy16K=%u copy1M=%u copy4M=%u memcpy1M=%u checksum1M=%u "
               "flush1M=%u log=%u logfmt=%u bus=%u\n",
               t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], OS_BUS_CLOCK_SPEED);
    }
#endif

    bool layoutKnown()
    {
        const char* name = kRuleNames;
        for (u32 r = 0; r < kRuleCount; r++)
        {
            u32 id = kRules[r].id;
            if (poolOf(id) != 0 && (g_HeapInfos[id].m_name == NULL ||
                                    !game::sameString(g_HeapInfos[id].m_name, name)))
            {
                TM_LOG("savestate: heap %u is not %s; savestates off\n", id, name);
                return false;
            }
            while (*name++ != '\0') {}
        }
        return true;
    }
}
}
