#include "main/record/slots.h"

#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <string.h>

#include "main/savestate/snapshot.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// The heap: FighterTechqniq (MEM2, 1127 KiB) exists from the first Training
// character select until Training is left, and held no block in any match
// measured (1v1 and four fighters). It is in the savestates' region set, so
// the block is excluded from it (savestate::setOwnExclusion);
// Fighter3/4Resoruce are left to the savestates. Allocation and free are the
// stub's module loader's calls (gfMemoryPool::alloc on
// gfHeapManager::getHeap, then free).
namespace tm {
namespace record {
    static const Heaps::HeapType kHeap = Heaps::FighterTechqniq;

    static Slot* s_slots = NULL;
    static game::HeapUsage s_before;

    enum {
        kUserSlots = kSides * kSlotCount,
        kTotalSlots = kSides * (kSlotCount + 1)
    };

    u32 storageBytes()
    {
        return static_cast<u32>(sizeof(Slot)) * kTotalSlots;
    }

    Slot* runLogSlot(int side)
    {
        if (s_slots == NULL || side < 0 || side >= kSides)
        {
            return NULL;
        }
        return &s_slots[kUserSlots + side];
    }

    void runLogClear(int side)
    {
        Slot* s = runLogSlot(side);
        if (s != NULL)
        {
            slotClear(*s);
        }
    }

    void runLogWrite(int side, int t, const Input& in)
    {
        Slot* s = runLogSlot(side);
        if (s == NULL)
        {
            return;
        }
        if (t == 0)
        {
            slotClear(*s);
        }
        slotWrite(*s, t, in);
    }

    bool runLogRead(int side, int t, Input& out)
    {
        Slot* s = runLogSlot(side);
        return s != NULL && slotRead(*s, t, out);
    }

    const void* storageAddress()
    {
        return s_slots;
    }

    bool storageReady()
    {
        return s_slots != NULL;
    }

    bool storageAllocate(bool quiet)
    {
        if (s_slots != NULL)
        {
            return true;
        }
        game::HeapUsage u;
        u32 bytes = storageBytes();
        if (!game::heapUsage(kHeap, u))
        {
            TM_LOG("record: no storage: heap %s not there\n", game::heapName(kHeap));
            return false;
        }
        if (u.allocated != 0 || u.largest < bytes)
        {
            if (quiet)
            {
                return false;
            }
            TM_LOG("record: no storage: heap %s holds %u blocks, largest free %x, need %x\n", game::heapName(kHeap),
                   u.allocated, u.largest, bytes);
            return false;
        }
        void* block = gfMemoryPool::alloc(gfHeapManager::getHeap(kHeap), bytes, 32);
        if (!game::isRam(block))
        {
            TM_LOG("record: no storage: allocation of %x in %s failed\n", bytes, game::heapName(kHeap));
            return false;
        }
        s_before = u;
        s_slots = static_cast<Slot*>(block);
        // The inputs must stay what they are when a state is loaded.
        savestate::setOwnExclusion(reinterpret_cast<u32>(block), bytes);
        for (int i = 0; i < kTotalSlots; i++)
        {
            slotClear(s_slots[i]);
        }
        TM_LOG("record: storage %08x, %u bytes (%u per slot, 8 per frame) in %s, free %x before\n", s_slots, bytes,
               sizeof(Slot), game::heapName(kHeap), u.freeBytes);
        return true;
    }

    void storageFree()
    {
        if (s_slots == NULL)
        {
            return;
        }
        savestate::setOwnExclusion(0, 0);
        free(s_slots);
        s_slots = NULL;
        game::HeapUsage u;
        bool back = game::heapUsage(kHeap, u) && u.freeBytes == s_before.freeBytes
                    && u.allocated == s_before.allocated && u.freeBlocks == s_before.freeBlocks;
        TM_LOG("record: storage freed; %s free %x, back to baseline: %s\n", game::heapName(kHeap), u.freeBytes,
               back ? "yes" : "no");
    }

    Slot* slotAt(int side, int index)
    {
        if (s_slots == NULL || side < 0 || side >= kSides || index < 0 || index >= kSlotCount)
        {
            return NULL;
        }
        return &s_slots[side * kSlotCount + index];
    }

    bool slotEmpty(const Slot& s)
    {
        return s.end == 0 || s.start >= s.end;
    }

    void slotClear(Slot& s)
    {
        s.start = kNoFrame;
        s.end = 0;
        for (int i = 0; i < kTables; i++)
        {
            s.count[i] = 0;
        }
        s.lrCount = 0;
        s.lossy = 0;
    }

    static u32 bitsOf(float f)
    {
        union {
            float f;
            u32 u;
        } v;
        v.f = f;
        return v.u;
    }

    static float floatOf(u32 u)
    {
        union {
            float f;
            u32 u;
        } v;
        v.u = u;
        return v.f;
    }

    // Index of `bits` in the table, added if new; the nearest entry (and
    // `lossy` set) when the table is full.
    static u8 intern(u32* table, u16& count, u32 bits, bool& lossy)
    {
        for (int i = 0; i < count; i++)
        {
            if (table[i] == bits)
            {
                return static_cast<u8>(i);
            }
        }
        if (count < kPaletteSize)
        {
            table[count] = bits;
            return static_cast<u8>(count++);
        }
        lossy = true;
        float want = floatOf(bits);
        int best = 0;
        float bestDiff = 1e30f;
        for (int i = 0; i < count; i++)
        {
            float d = floatOf(table[i]) - want;
            d = d < 0.0f ? -d : d;
            if (d < bestDiff)
            {
                bestDiff = d;
                best = i;
            }
        }
        return static_cast<u8>(best);
    }

    // Index of the (l, r) pair, as intern() does for one value.
    static u8 internPair(Slot& s, u32 l, u32 r, bool& lossy)
    {
        for (int i = 0; i < s.lrCount; i++)
        {
            if (s.lr[i][0] == l && s.lr[i][1] == r)
            {
                return static_cast<u8>(i);
            }
        }
        if (s.lrCount < kPaletteSize)
        {
            s.lr[s.lrCount][0] = l;
            s.lr[s.lrCount][1] = r;
            return static_cast<u8>(s.lrCount++);
        }
        lossy = true;
        int best = 0;
        float bestDiff = 1e30f;
        for (int i = 0; i < s.lrCount; i++)
        {
            float dl = floatOf(s.lr[i][0]) - floatOf(l);
            float dr = floatOf(s.lr[i][1]) - floatOf(r);
            float d = dl * dl + dr * dr;
            if (d < bestDiff)
            {
                bestDiff = d;
                best = i;
            }
        }
        return static_cast<u8>(best);
    }

    static void pack(Slot& s, int t, const Input& in)
    {
        bool lossy = (in.buttons & 0xFFFF0000) != 0;
        Packed& p = s.frames[t];
        p.buttons = static_cast<u16>(in.buttons);
        p.x = intern(s.table[kTableX], s.count[kTableX], bitsOf(in.x), lossy);
        p.y = intern(s.table[kTableY], s.count[kTableY], bitsOf(in.y), lossy);
        p.cx = intern(s.table[kTableCx], s.count[kTableCx], bitsOf(in.cx), lossy);
        p.cy = intern(s.table[kTableCy], s.count[kTableCy], bitsOf(in.cy), lossy);
        p.lr = internPair(s, bitsOf(in.l), bitsOf(in.r), lossy);
        p.press = in.press;
        if (lossy)
        {
            s.lossy++;
        }
    }

    void slotWrite(Slot& s, int t, const Input& in)
    {
        if (t < 0 || t >= kMaxFrames)
        {
            return;
        }
        if (slotEmpty(s))
        {
            s.start = static_cast<u16>(t);
        }
        else if (t < s.start)
        {
            s.start = static_cast<u16>(t);
        }
        else if (t > s.end)
        {
            // Frames skipped since the slot last ended hold nothing.
            Input none;
            memset(&none, 0, sizeof(none));
            for (int i = s.end; i < t; i++)
            {
                pack(s, i, none);
            }
        }
        pack(s, t, in);
        s.end = static_cast<u16>(t + 1);
    }

    bool slotRead(const Slot& s, int t, Input& out)
    {
        if (slotEmpty(s) || t < s.start || t >= s.end)
        {
            return false;
        }
        const Packed& p = s.frames[t];
        out.buttons = p.buttons;
        out.x = floatOf(s.table[kTableX][p.x]);
        out.y = floatOf(s.table[kTableY][p.y]);
        out.cx = floatOf(s.table[kTableCx][p.cx]);
        out.cy = floatOf(s.table[kTableCy][p.cy]);
        out.l = floatOf(s.lr[p.lr][0]);
        out.r = floatOf(s.lr[p.lr][1]);
        out.press = p.press;
        return true;
    }

    void slotCopy(Slot& to, const Slot& from)
    {
        if (&to != &from)
        {
            memcpy(&to, &from, sizeof(Slot));
        }
    }

    void slotDropFront(Slot& s, int frames)
    {
        if (frames <= 0 || slotEmpty(s))
        {
            return;
        }
        if (frames >= s.end)
        {
            slotClear(s);
            return;
        }
        int first = s.start > frames ? s.start : frames;
        // Towards lower addresses, so a forward copy is safe (the game has a
        // memmove, 0x803F602C in the decompilation's symbol map, but the
        // link lists do not name it).
        for (int i = first; i < s.end; i++)
        {
            s.frames[i - frames] = s.frames[i];
        }
        s.start = static_cast<u16>(first - frames);
        s.end = static_cast<u16>(s.end - frames);
    }

    // Only the extra module calls it: kept from the linker's stripping.
#pragma force_active on
    bool slotEdit(Slot& s, int t, const Input& in)
    {
        if (t < s.start || t >= s.end)
        {
            return false;
        }
        u32 lossy = s.lossy;
        pack(s, t, in);
        return s.lossy == lossy;
    }
#pragma force_active reset
}
}
