#include "main/storage/file_io.h"

#include <gf/gf_file_io.h>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>
#include <revolution/FA.h>
#include <sy_core.h>

#include "main/storage/format.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"

// OSSleepTicks: RSBE01 rev 2 symbol 0x801E1A80 (link/EXTRA.lst); its
// declaration in OSThread.h takes the 64-bit tick count.
extern "C" void OSSleepTicks(s64 ticks);

namespace tm {
namespace storage {
    // Our folder under the build's own folder, and the device prefix the
    // game's file requests take for the SD card ("sd:/Project+/..."; the
    // FA library itself takes the path without it).
    static const char kFolder[] = "tm/";
    static const char kDevice[] = "sd:";

    // The bus clock in the OS globals (0x800000F8, OSHardware.h); the timer
    // runs at a quarter of it (OS_TIME_SPEED, OSTime.h).
    static const volatile u32* const kBusClock = reinterpret_cast<const volatile u32*>(0x800000F8);

    static s64 ticksPerMs()
    {
        u32 bus = *kBusClock;
        if (bus < 100000000u || bus > 500000000u)
        {
            bus = 243000000u;
        }
        return static_cast<s64>(bus / 4 / 1000);
    }

    const char* resultText(int result)
    {
        static const char* const kTexts[kResultCount] = {
            "done",
            "busy",
            "no SD card",
            "the SD card is locked",
            "the SD card is full",
            "the file could not be written",
            "file not found",
            "made by a newer version",
            "made by an older version",
            "the file is damaged",
            "not enough memory",
            "too big"
        };
        return result >= 0 && result < kResultCount ? kTexts[result] : "?";
    }

    static u32 length(const char* s)
    {
        u32 n = 0;
        while (s[n] != '\0')
        {
            n++;
        }
        return n;
    }

    static bool terminated(const char* s, u32 max)
    {
        for (u32 i = 0; i < max; i++)
        {
            if (s[i] == '\0')
            {
                return true;
            }
        }
        return false;
    }

    static bool append(char* out, u32& at, const char* s)
    {
        while (*s != '\0')
        {
            if (at + 1 >= kPathMax)
            {
                return false;
            }
            out[at++] = *s++;
        }
        out[at] = '\0';
        return true;
    }

    bool makePath(char* out, const char* name, bool withDevice)
    {
        u32 at = 0;
        out[0] = '\0';
        // MOD_PATCH_DIR is the build's folder with both slashes ("/Project+/").
        if (!game::isRam(MOD_PATCH_DIR) || MOD_PATCH_DIR[0] != '/' || !terminated(MOD_PATCH_DIR, 0x18))
        {
            return false;
        }
        return (!withDevice || append(out, at, kDevice)) && append(out, at, MOD_PATCH_DIR)
               && append(out, at, kFolder) && append(out, at, name);
    }

    // gfFileIO::checkSDInserted and ::checkSDWriteProtected are not named in
    // the decompilation. They were found from the SD insert and eject
    // callbacks that set and clear the byte the first returns (fn_8001BB1C,
    // fn_8001BB8C, registered as a pair at 0x8001BBE0; stores at 0x8001BB3C,
    // 0x8001BBA0); the second asks the FA library about drive 'A'
    // (fn_803EC094; that it answers the write-protect bit was inferred, not
    // traced). Called only while their code is as read on RSBE01 rev 2 with
    // P+ 3.2.1.
    static const u32 kInsertedCode[] = {0x886DBBBC, 0x4E800020};
    static const u32 kLockedCode[] = {0x9421FFF0, 0x7C0802A6, 0x38600041, 0x90010014, 0x93E1000C,
                                      0x3BE00000, 0x483D03C1, 0x2C030001};

    static bool codeIs(const void* fn, const u32* words, u32 count)
    {
        const u32* code = reinterpret_cast<const u32*>(fn);
        if (!game::isRam(code))
        {
            return false;
        }
        for (u32 i = 0; i < count; i++)
        {
            if (code[i] != words[i])
            {
                return false;
            }
        }
        return true;
    }

    bool cardInserted()
    {
        bool (*fn)() = &gfFileIO::checkSDInserted;
        if (!codeIs(reinterpret_cast<const void*>(fn), kInsertedCode, 2))
        {
            return true; // unknown code: let the request itself fail
        }
        return fn();
    }

    bool cardLocked()
    {
        bool (*fn)() = &gfFileIO::checkSDWriteProtected;
        if (!codeIs(reinterpret_cast<const void*>(fn), kLockedCode, sizeof(kLockedCode) / sizeof(kLockedCode[0])))
        {
            return false;
        }
        return fn();
    }

    int writable()
    {
        if (!cardInserted())
        {
            return kNoCard;
        }
        return cardLocked() ? kLocked : kOk;
    }

    u32 checksum(const void* data, u32 size, u32 seed)
    {
        const u8* p = static_cast<const u8*>(data);
        u32 h = seed;
        for (u32 i = 0; i < size; i++)
        {
            h = (h ^ p[i]) * 0x01000193u;
        }
        return h;
    }

    int checkHeader(const void* file, u32 bytes, u32 magic, u16 format, bool whole)
    {
        const FileHeader* h = static_cast<const FileHeader*>(file);
        if (bytes < sizeof(FileHeader) || h->magic != magic || h->headerSize < sizeof(FileHeader)
            || h->size < h->headerSize)
        {
            return kCorrupt;
        }
        if (whole && (bytes < h->size || checksum(static_cast<const u8*>(file) + kChecksumFrom,
                                                  h->size - kChecksumFrom) != h->checksum))
        {
            return kCorrupt;
        }
        if (h->format > format)
        {
            return kNewer;
        }
        if (h->format < format)
        {
            return kOlder;
        }
        return kOk;
    }

    int fileSize(const char* name)
    {
        char path[kPathMax];
        // Room past the declared record in case the library writes more
        // (not checked against FAFstat's code; Project+'s own call, the
        // getFileSize patch in FilePatchCode.asm, gives it only 0x10 bytes).
        struct {
            FAStat st;
            u8 spare[32];
        } s;
        if (!makePath(path, name, false) || FAFstat(path, &s.st) != 0)
        {
            return -1;
        }
        return s.st.filesize;
    }

    Job::Job() : m_kind(kIdle), m_preset(kOk)
    {
        memset(&m_handle, 0, sizeof(m_handle));
    }

    u8* allocReadBuffer(u32 bytes)
    {
        static const Heaps::HeapType kHeaps[2] = {Heaps::Network, Heaps::MenuInstance};
        u32 need = padded(bytes);
        if (need == 0)
        {
            return NULL;
        }
        for (int i = 0; i < 2; i++)
        {
            game::HeapUsage u;
            u.largest = 0;
            if (game::heapUsage(kHeaps[i], u) && u.largest >= need)
            {
                void* p = gfMemoryPool::alloc(gfHeapManager::getHeap(kHeaps[i]), need, 32);
                if (game::isRam(p))
                {
                    return static_cast<u8*>(p);
                }
            }
        }
        return NULL;
    }

    static bool poolBlockLive(const void* buf)
    {
#ifdef __MWERKS__
        // gfMemoryPool::alloc (0x80025D40) writes 0xCCCCCCCC at (buf - 4),
        // the block header at (buf - 12), and clears bit 0 of block+0x13;
        // gfMemoryPool::free (0x8002600C) writes 0xDDDDDDDD at (buf - 4) and
        // sets bit 0 of block+0x13. Checking these before freeing on any
        // non-kOk read error prevents double-free even if readSDFile frees
        // `buf` and returns status == 1.
        if (!game::isRam(buf))
        {
            return false;
        }
        u32 addr = reinterpret_cast<u32>(buf);
        if ((addr & 3u) != 0u || *reinterpret_cast<const u32*>(addr - 4) != 0xCCCCCCCCu)
        {
            return false;
        }
        u32 block = *reinterpret_cast<const u32*>(addr - 12);
        if (!game::isRam(reinterpret_cast<const void*>(block)))
        {
            return false;
        }
        if ((*reinterpret_cast<const u8*>(block + 19) & 1u) != 0u)
        {
            return false;
        }
        u32 pool = *reinterpret_cast<const u32*>(block);
        return pool == reinterpret_cast<u32>(gfHeapManager::getHeap(Heaps::Network))
               || pool == reinterpret_cast<u32>(gfHeapManager::getHeap(Heaps::MenuInstance));
#else
        return host_game_block_live(buf);
#endif
    }

    void freeReadBuffer(u8*& buf, int result, int status)
    {
        if (buf == NULL)
        {
            return;
        }
        if (result == kBusy || readFreedBuffer(result, status) || (result != kOk && !poolBlockLive(buf)))
        {
            buf = NULL;
            return;
        }
        free(buf);
        buf = NULL;
    }

    u8* allocWriteBuffer(u32 bytes)
    {
        u32 need = padded(bytes);
        game::HeapUsage u;
        u.largest = 0;
        if (need == 0 || !game::heapUsage(Heaps::Network, u) || u.largest < need)
        {
            return NULL;
        }
        void* p = gfMemoryPool::alloc(gfHeapManager::getHeap(Heaps::Network), need, 32);
        if (!game::isRam(p))
        {
            return NULL;
        }
        memset(p, 0, need);
        return static_cast<u8*>(p);
    }

    void freeWriteBuffer(u8*& buf, int result)
    {
        if (buf == NULL)
        {
            return;
        }
        if (result != kBusy)
        {
            free(buf);
        }
        buf = NULL;
    }

    bool Job::start(int kind, const char* name)
    {
        if (busy())
        {
            return false;
        }
        char path[kPathMax];
        if (!makePath(path, name, true))
        {
            return false;
        }
        if (kind == kFolder)
        {
            // gfFACreateDir creates every missing folder of the path (when
            // the whole path fails it creates each '/' prefix in turn,
            // 0x8001BE78-0x8001BEEC).
            u32 n = length(path);
            if (n > 0 && path[n - 1] == '/')
            {
                path[n - 1] = '\0';
            }
        }
        m_kind = kind;
        return true;
    }

    bool Job::makeFolder()
    {
        if (!start(kFolder, ""))
        {
            return false;
        }
        char path[kPathMax];
        makePath(path, "", true);
        path[length(path) - 1] = '\0';
        handle().createDirRequest(path);
        return true;
    }

    bool Job::write(const char* name, const void* data, u32 bytes)
    {
        char path[kPathMax];
        if (!start(kWrite, name) || !makePath(path, name, true))
        {
            return false;
        }
        handle().writeRequest(path, const_cast<void*>(data), static_cast<int>(bytes), 0);
        return true;
    }

    bool Job::read(const char* name, void* dest, u32 bytes)
    {
        char path[kPathMax];
        if (bytes == 0 || !start(kRead, name) || !makePath(path, name, true))
        {
            return false;
        }
        // Never more than the file holds: on a short FAFread the game's
        // readSDFile frees the buffer with gfHeapManager::free (0x8001CD5C,
        // 0x8001CDD4), and ours is from no heap (in Dolphin, reads and writes
        // at 0x3-0x24, measured, setup not recorded; on the console an
        // exception, not checked).
        int size = fileSize(name);
        if (size < 0 || static_cast<u32>(size) < bytes)
        {
            m_preset = size < 0 ? kMissing : kCorrupt;
            return true;
        }
        // Into `dest` (Project+'s SD read patches use the request's address
        // when no heap is given, and its length when it is not 0:
        // FilePatchCode.asm, hooks at 0x8001CD0C and 0x8001CCB8).
        handle().readRequestNoSync(path, dest, static_cast<int>(bytes), 0);
        return true;
    }

    bool Job::remove(const char* name)
    {
        char path[kPathMax];
        if (!start(kDelete, name) || !makePath(path, name, true))
        {
            return false;
        }
        handle().del(path);
        return true;
    }

    bool Job::poll(int& result, int& status)
    {
        if (!busy())
        {
            result = kOk;
            status = 0;
            return true;
        }
        if (m_preset != kOk)
        {
            result = m_preset;
            status = -1;
            m_preset = kOk;
            m_kind = kIdle;
            return true;
        }
        if (!handle().isReady())
        {
            return false;
        }
        status = handle().getReturnStatus();
        int kind = m_kind;
        handle().release();
        m_kind = kIdle;
        if (status == 0)
        {
            result = kOk;
        }
        else if (!cardInserted())
        {
            result = kNoCard;
        }
        else if (kind == kWrite)
        {
            // writeSDFile (fn_8001D740): 1 when FAFopen fails, 3 on a short
            // FAFwrite (0x8001D794, 0x8001D884; returned at 0x8001D8D0).
            result = cardLocked() ? kLocked : (status == 3 ? kFull : kWriteFailed);
        }
        else if (kind == kFolder)
        {
            result = cardLocked() ? kLocked : kWriteFailed;
        }
        else if (kind == kDelete && cardLocked())
        {
            result = kLocked;
        }
        else
        {
            result = kMissing;
        }
        return true;
    }

    int Job::wait(u32 ms, int& status)
    {
        int result = kOk;
        status = 0;
        s64 tick = ticksPerMs();
        for (u32 i = 0; !poll(result, status); i++)
        {
            if (i >= ms)
            {
                return kBusy;
            }
            OSSleepTicks(tick);
        }
        return result;
    }

    bool exists(const char* name)
    {
        return fileSize(name) >= 0;
    }

#if !defined(TM_MODULE_MAIN) || !defined(__MWERKS__)
    // FAEntryInfo is large; one is enough (listing is synchronous). Spare
    // room after it in case the library's record is longer than declared.
    // Only the menu module (and host tests) list files on SD.
    static struct {
        FAEntryInfo e;
        u8 spare[256];
    } s_list;

    int listFiles(const char* suffix, ListFn each, void* ctx)
    {
        char pattern[kPathMax];
        if (!makePath(pattern, "*", false))
        {
            return -1;
        }
        u32 at = length(pattern);
        if (!append(pattern, at, suffix))
        {
            return -1;
        }
        int count = 0;
        // 0x20: files (the archive attribute), as Syriinge lists plugins
        // (sy_core.cpp, FAFsfirst(tmp, 0x20, &info)).
        if (FAFsfirst(pattern, 0x20, &s_list.e) != 0)
        {
            return cardInserted() ? 0 : -1;
        }
        do
        {
            bool longName = s_list.e.name[0] != '\0';
            const char* name = longName ? s_list.e.name : s_list.e.shortname;
            u32 nameCap = longName ? sizeof(s_list.e.name) : sizeof(s_list.e.shortname);
            if (name[0] != '.' && terminated(name, nameCap))
            {
                each(name, static_cast<u32>(s_list.e.size), ctx);
                count++;
            }
        } while (FAFsnext(&s_list.e) == 0);
        return count;
    }
#endif
}
}
