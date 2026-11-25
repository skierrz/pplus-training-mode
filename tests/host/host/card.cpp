#include "host/card.h"

#include <OS/OSTime.h>
#include <gf/gf_file_io.h>
#include <gf/gf_file_io_handle.h>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <revolution/FA.h>
#include <sy_core.h>

#include "host/fakes.h"
#include "host/mem.h"
#include "host/test.h"

HostModPatchDir* host_mod_patch_dir_ptr = NULL;

namespace host {
namespace card {
    enum { kMaxFiles = 32, kMaxPath = 128, kMaxRequests = 8 };
    static const char kRoot[] = "/Project+/";
    static const char kFolder[] = "/Project+/tm";

    struct File {
        bool used;
        char path[kMaxPath]; // as the FAT library takes it: no device
        u8* bytes;
        u32 size;
    };

    enum Kind { kNone, kFolderRequest, kWrite, kRead, kDelete };

    struct Request {
        int kind;
        int polls;
        int status;
        char path[kMaxPath];
        void* address;
        u32 length;
        u32 offset;
    };

    static File s_files[kMaxFiles];
    static Request s_requests[kMaxRequests];
    static int s_delay = 0;
    static bool s_locked = false;
    static bool s_full = false;
    static bool s_folder = false;
    static int s_shortRead = 0;
    static int s_failRead = 0;
    static int s_failStatus = 0;
    static int s_writes = 0, s_reads = 0, s_deletes = 0;
    static OSCalendarTime s_clock;
    static int s_listNext = 0;
    static char s_listFolder[kMaxPath];
    static char s_listSuffix[kMaxPath];

    void reset()
    {
        // The files' bytes are C library memory of the test layer's own.
        for (int i = 0; i < kMaxFiles; i++)
        {
            if (s_files[i].used)
            {
                ::operator delete(s_files[i].bytes);
            }
        }
        memset(s_files, 0, sizeof(s_files));
        memset(s_requests, 0, sizeof(s_requests));
        s_delay = 0;
        s_locked = s_full = s_folder = false;
        s_shortRead = 0;
        s_failRead = 0;
        s_failStatus = 0;
        s_writes = s_reads = s_deletes = 0;
        s_listNext = kMaxFiles;
        setClock(2026, 1, 2, 3, 4, 5);
        host_mod_patch_dir_ptr = static_cast<HostModPatchDir*>(mem::alloc(sizeof(HostModPatchDir), 4));
        strcpy(*host_mod_patch_dir_ptr, kRoot);
    }

    void setDelay(int polls)
    {
        s_delay = polls;
    }

    void setLocked(bool locked)
    {
        s_locked = locked;
    }

    void setFull(bool full)
    {
        s_full = full;
    }

    void setShortRead(int onRead)
    {
        s_shortRead = onRead;
    }

    void setReadStatus(int onRead, int status)
    {
        s_failRead = onRead;
        s_failStatus = status;
    }

    void setClock(int year, int month, int day, int hour, int minute, int second)
    {
        memset(&s_clock, 0, sizeof(s_clock));
        s_clock.year = year;
        s_clock.month = month - 1;
        s_clock.mday = day;
        s_clock.hour = hour;
        s_clock.min = minute;
        s_clock.sec = second;
    }

    static File* find(const char* path)
    {
        for (int i = 0; i < kMaxFiles; i++)
        {
            if (s_files[i].used && strcmp(s_files[i].path, path) == 0)
            {
                return &s_files[i];
            }
        }
        return NULL;
    }

    static void store(const char* path, const void* bytes, u32 size)
    {
        File* f = find(path);
        for (int i = 0; f == NULL && i < kMaxFiles; i++)
        {
            f = s_files[i].used ? NULL : &s_files[i];
        }
        if (f == NULL || strlen(path) >= kMaxPath)
        {
            printf("host: the card holds at most %d files with paths under %d characters\n", static_cast<int>(kMaxFiles),
                   static_cast<int>(kMaxPath));
            exit(3);
        }
        if (f->used)
        {
            ::operator delete(f->bytes);
        }
        f->used = true;
        strcpy(f->path, path);
        f->bytes = static_cast<u8*>(::operator new(size > 0 ? size : 1));
        memcpy(f->bytes, bytes, size);
        f->size = size;
    }

    static void erase(const char* path)
    {
        File* f = find(path);
        if (f != NULL)
        {
            ::operator delete(f->bytes);
            memset(f, 0, sizeof(*f));
        }
    }

    static const char* inFolder(const char* name)
    {
        static char path[kMaxPath];
        snprintf(path, sizeof(path), "%s/%s", kFolder, name);
        return path;
    }

    bool has(const char* name)
    {
        return find(inFolder(name)) != NULL;
    }

    u32 size(const char* name)
    {
        File* f = find(inFolder(name));
        return f != NULL ? f->size : 0;
    }

    u8* data(const char* name)
    {
        File* f = find(inFolder(name));
        return f != NULL ? f->bytes : NULL;
    }

    void put(const char* name, const void* bytes, u32 size)
    {
        store(inFolder(name), bytes, size);
    }

    void remove(const char* name)
    {
        erase(inFolder(name));
    }

    void putRaw(const char* path, const void* bytes, u32 size)
    {
        store(path, bytes, size);
    }

    void removeRaw(const char* path)
    {
        erase(path);
    }

    int fileCount()
    {
        int n = 0;
        for (int i = 0; i < kMaxFiles; i++)
        {
            n += s_files[i].used ? 1 : 0;
        }
        return n;
    }

    int writes()
    {
        return s_writes;
    }
    int reads()
    {
        return s_reads;
    }
    int deletes()
    {
        return s_deletes;
    }
    bool folderMade()
    {
        return s_folder;
    }

    // The path of a request: "sd:" and then the FAT library's form.
    static const char* strip(const char* path)
    {
        if (strncmp(path, "sd:", 3) != 0)
        {
            test::fail(__FILE__, __LINE__, "a file request without the sd: device: %s", path);
            return path;
        }
        return path + 3;
    }

    static Request* begin(gfFileIOHandle* handle, int kind, const char* path)
    {
        Request* r = NULL;
        for (int i = 0; r == NULL && i < kMaxRequests; i++)
        {
            r = s_requests[i].kind == kNone ? &s_requests[i] : NULL;
        }
        if (r == NULL)
        {
            printf("host: more than %d file requests in flight\n", static_cast<int>(kMaxRequests));
            exit(3);
        }
        memset(r, 0, sizeof(*r));
        r->kind = kind;
        r->polls = s_delay;
        snprintf(r->path, sizeof(r->path), "%s", strip(path));
        handle->m_request = r;
        return r;
    }

    static void finish(Request& r)
    {
        switch (r.kind)
        {
        case kFolderRequest:
            r.status = s_locked ? 1 : 0;
            s_folder = s_folder || !s_locked;
            break;
        case kWrite:
        {
            u32 padded = (r.length + 31) & ~31u;
            if (s_locked)
            {
                r.status = 1;
            }
            else if (s_full)
            {
                store(r.path, r.address, padded / 2);
                r.status = 3;
            }
            else
            {
                store(r.path, r.address, padded);
                r.status = 0;
            }
            break;
        }
        case kRead:
        {
            File* f = find(r.path);
            if (f == NULL || r.offset > f->size || f->size - r.offset < r.length)
            {
                test::fail(__FILE__, __LINE__, "a read the game cannot serve: %lu bytes at %lu of %s, which %s",
                           r.length, r.offset, r.path, f == NULL ? "is not there" : "is shorter");
                r.status = 1;
            }
            else if (s_shortRead > 0 && --s_shortRead == 0)
            {
                // Brawl's gfFileIO::readSDFile frees the destination buffer on
                // a short FAFread and returns status 2 (0x8001CD50-0x8001CDD8).
                fakes::freeByGame(r.address);
                r.status = 2;
            }
            else if (s_failRead > 0 && --s_failRead == 0)
            {
                r.status = s_failStatus;
            }
            else
            {
                memcpy(r.address, f->bytes + r.offset, r.length);
                r.status = 0;
            }
            break;
        }
        case kDelete:
            r.status = s_locked || find(r.path) == NULL ? 1 : 0;
            if (!s_locked)
            {
                erase(r.path);
            }
            break;
        default:
            break;
        }
    }
}
}

using namespace host::card;

bool gfFileIOHandle::createDirRequest(const char* filepath)
{
    begin(this, kFolderRequest, filepath);
    return true;
}

bool gfFileIOHandle::writeRequest(const char* filepath, void* addr, int length, int)
{
    Request* r = begin(this, kWrite, filepath);
    r->address = addr;
    r->length = static_cast<u32>(length);
    s_writes++;
    return true;
}

bool gfFileIOHandle::read(const char* filepath, Heaps::HeapType heap, int)
{
    char full[kMaxPath];
    snprintf(full, sizeof(full), "%spf/%s", MOD_PATCH_DIR, filepath);
    File* f = find(full);
    if (f == NULL)
    {
        return false;
    }
    void* buf = gfMemoryPool::alloc(gfHeapManager::getHeap(heap), f->size, 32);
    if (buf == NULL)
    {
        return false;
    }
    memcpy(buf, f->bytes, f->size);
    char sdFull[kMaxPath + 4];
    snprintf(sdFull, sizeof(sdFull), "sd:%s", full);
    Request* r = begin(this, kRead, sdFull);
    r->address = buf;
    r->length = f->size;
    r->polls = -1;
    r->status = 0;
    s_reads++;
    return true;
}

void* gfFileIOHandle::getBuffer() const
{
    Request* r = static_cast<Request*>(m_request);
    return r != NULL ? r->address : NULL;
}

int gfFileIOHandle::getSize() const
{
    Request* r = static_cast<Request*>(m_request);
    return r != NULL ? static_cast<int>(r->length) : 0;
}

bool gfFileIOHandle::readRequestNoSync(const char* filepath, void* addr, int length, int offset)
{
    Request* r = begin(this, kRead, filepath);
    r->address = addr;
    r->length = static_cast<u32>(length);
    r->offset = static_cast<u32>(offset);
    s_reads++;
    return true;
}

bool gfFileIOHandle::del(const char* filepath)
{
    begin(this, kDelete, filepath);
    s_deletes++;
    return true;
}

bool gfFileIOHandle::isReady() const
{
    Request* r = static_cast<Request*>(m_request);
    // A request from before the card was reset is gone: nothing to wait for.
    if (r == NULL || r->kind == kNone)
    {
        return true;
    }
    if (r->polls > 0)
    {
        r->polls--;
        return false;
    }
    if (r->polls == 0)
    {
        finish(*r);
        r->polls = -1;
    }
    return true;
}

int gfFileIOHandle::getReturnStatus() const
{
    Request* r = static_cast<Request*>(m_request);
    return r != NULL ? r->status : 0;
}

void gfFileIOHandle::release()
{
    Request* r = static_cast<Request*>(m_request);
    if (r != NULL)
    {
        r->kind = kNone;
    }
    m_request = NULL;
}

bool gfFileIO::checkSDInserted()
{
    return true;
}

bool gfFileIO::checkSDWriteProtected()
{
    return false;
}

extern "C" int FAFstat(const char* filepath, FAStat* st)
{
    File* f = find(filepath);
    if (f == NULL)
    {
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->filesize = static_cast<int>(f->size);
    return 0;
}

static bool listed(const File& f)
{
    u32 folder = static_cast<u32>(strlen(s_listFolder));
    u32 suffix = static_cast<u32>(strlen(s_listSuffix));
    u32 n = static_cast<u32>(strlen(f.path));
    return f.used && n >= folder + suffix && strncmp(f.path, s_listFolder, folder) == 0
           && strchr(f.path + folder, '/') == NULL && strcmp(f.path + n - suffix, s_listSuffix) == 0;
}

extern "C" int FAFsnext(FAEntryInfo* ret)
{
    for (; s_listNext < kMaxFiles; s_listNext++)
    {
        const File& f = s_files[s_listNext];
        if (listed(f))
        {
            memset(ret, 0, sizeof(*ret));
            snprintf(ret->name, sizeof(ret->name), "%s", f.path + strlen(s_listFolder));
            ret->size = static_cast<int>(f.size);
            s_listNext++;
            return 0;
        }
    }
    return -1;
}

// `pattern` is <folder>/*<suffix>, the one form the mod asks for.
extern "C" int FAFsfirst(const char* pattern, int, FAEntryInfo* ret)
{
    const char* star = strchr(pattern, '*');
    if (star == NULL || static_cast<u32>(star - pattern) >= sizeof(s_listFolder) || strlen(star + 1) >= sizeof(s_listSuffix))
    {
        return -1;
    }
    memcpy(s_listFolder, pattern, star - pattern);
    s_listFolder[star - pattern] = '\0';
    strcpy(s_listSuffix, star + 1);
    s_listNext = 0;
    return FAFsnext(ret);
}

extern "C" s64 OSGetTime(void)
{
    return 0;
}

extern "C" u32 OSGetTick(void)
{
    return 0;
}

extern "C" void OSTicksToCalendarTime(s64, OSCalendarTime* cal)
{
    *cal = s_clock;
}

extern "C" void OSSleepTicks(s64)
{
}
