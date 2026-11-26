#pragma once

#include <gf/gf_file_io_handle.h>
#include <types.h>

#include "shared/version.h"

// Files in one folder of ours on the SD card: <MOD_PATCH_DIR>tm/ (on
// Project+ "/Project+/tm/"), the folder the build loads its own files from,
// as the stub finds its plugin file.
//
// Reads, writes, deletes and the folder's creation are the game's own
// asynchronous requests (gfFileIOHandle, served by its file thread), one at
// a time per Job, polled once a frame; nothing waits for them except at a
// module's detach. Listing is the FA library's synchronous
// FAFsfirst/FAFsnext, as Syriinge lists plugins. No rename was found:
// gfFileIO has none (gf_file_io.h), nor do the FA library's named functions
// in the RSBE01 rev 2 symbol map, though many are unnamed (docs/files.md
// says what is done instead).
namespace tm {
namespace storage {
    enum Result {
        kOk = 0,
        kBusy,        // another request of this job is in flight
        kNoCard,
        kLocked,      // the card's write-protect switch
        kFull,        // a short write
        kWriteFailed, // the file could not be created
        kMissing,     // no such file (or unreadable)
        kNewer,       // made by a newer version
        kOlder,       // made by an older version that cannot be read
        kCorrupt,     // wrong size or checksum
        kNoMemory,
        kTooBig,
        kResultCount
    };

    // Short text for messages ("the SD card is locked").
    const char* resultText(int result);

    enum {
        kNameMax = 40, // file names, NUL included
        kPathMax = 96
    };

    // "sd:<MOD_PATCH_DIR>tm/<name>" (withDevice) or "<MOD_PATCH_DIR>tm/<name>"
    // (the FA library's form). False if it does not fit or the root is unreadable.
    bool makePath(char* out, const char* name, bool withDevice);

    // The card as the game's FA library sees it (gfFileIO::checkSDInserted
    // and checkSDWriteProtected, checked against their code first).
    bool cardInserted();
    bool cardLocked();
    // kOk, kNoCard or kLocked.
    int writable();

    // FNV-1a (32-bit), the checksum of every file.
    u32 checksum(const void* data, u32 size, u32 seed = 0x811C9DC5u);

    // Bytes the game writes for `size` (its SD write, fn_8001D740, rounds the
    // length up to 32 at 0x8001D7FC before FAFwrite).
    inline u32 padded(u32 size)
    {
        return (size + 31) & ~31u;
    }

    // A 32-byte aligned buffer from a game heap (Network, else MenuInstance)
    // for Job::read(): Brawl's gfFileIO::readSDFile can free its destination
    // via gfHeapManager::free on short/failed reads (0x8001CD50-0x8001CDD8),
    // so every read destination must be a heap block and freeReadBuffer()
    // verifies pool liveness before freeing on any error result.
    u8* allocReadBuffer(u32 bytes);
    inline bool readFreedBuffer(int result, int status)
    {
        return result != kOk && status == 2;
    }
    void freeReadBuffer(u8*& buf, int result, int status);

    // A 32-byte aligned write buffer in Heaps::Network so async SDIO writes
    // never reference a module's static .bss across module unload. On kBusy,
    // freeWriteBuffer() drops the pointer without freeing so DMA stays safe.
    u8* allocWriteBuffer(u32 bytes);
    void freeWriteBuffer(u8*& buf, int result);

    // One asynchronous request at a time. Stores gfFileIOHandle in raw
    // pointer-aligned storage so Job is trivially destructible and module
    // unload (_epilog / __destroy_global_chain) never calls ~gfFileIOHandle()
    // on an in-flight SDIO request after a kBusy timeout.
    class Job {
    public:
        Job();
        bool busy() const
        {
            return m_kind != kIdle;
        }
        // Each returns false (and starts nothing) if a request is in flight.
        // A write sends padded(bytes) bytes from `data`; a read wants exactly
        // `bytes` bytes (the file may be longer; a shorter or missing one is
        // refused before anything is sent: kCorrupt, kMissing).
        bool makeFolder();
        bool write(const char* name, const void* data, u32 bytes);
        bool read(const char* name, void* dest, u32 bytes);
        bool remove(const char* name);
        // True once the request has finished; `result` is then a Result and
        // `status` the game's own return status.
        bool poll(int& result, int& status);
        // Waits for the request (at most `ms` milliseconds, sleeping 1 ms at
        // a time). Returns its Result; kBusy if it is still running.
        int wait(u32 ms, int& status);

    private:
        enum Kind { kIdle, kFolder, kWrite, kRead, kDelete };
        bool start(int kind, const char* name);
        gfFileIOHandle& handle()
        {
            return *reinterpret_cast<gfFileIOHandle*>(m_handle.raw);
        }
        const gfFileIOHandle& handle() const
        {
            return *reinterpret_cast<const gfFileIOHandle*>(m_handle.raw);
        }
        union {
            void* align;
            u8 raw[sizeof(gfFileIOHandle)];
        } m_handle;
        int m_kind;
        int m_preset; // a read refused before it was sent: its Result
    };
    TM_STATIC_ASSERT(sizeof(gfFileIOHandle) == sizeof(void*), tm_file_io_handle_size);

    // The folder's files matching "*<suffix>", by the FA library
    // (synchronous). Calls each(name, size, ctx) per file; returns the count,
    // -1 if the folder cannot be read.
    typedef void (*ListFn)(const char* name, u32 size, void* ctx);
    int listFiles(const char* suffix, ListFn each, void* ctx);
    // The size of `name` in the folder, -1 if it is not there (FAFstat,
    // synchronous); exists() is fileSize() >= 0.
    int fileSize(const char* name);
    bool exists(const char* name);
}
}
