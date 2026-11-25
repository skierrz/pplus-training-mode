// The module loader: the plugin carries larger modules appended to its own
// file, reads that file again and links the modules each scene's policy
// names (load_policy.cpp) into their heaps, unloading them at scene exit.
// They are linked as the game links its own modules (gfModule::create).
// The method is that of Brawl Online's PPOnline loader
// (game-code/PPOnlineLoader in its repository); the file format, the checks
// and the per-scene loading are the mod's own.
#include "stub/loader.h"

#include <OS/OSLink.h>
#include <OS/OSTime.h>
#include <revolution/FA.h>
#include <gf/gf_file_io_handle.h>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <gf/gf_scene.h>
#include <memory.h>
#include <string.h>
#include <sy_core.h>

#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/game/scene.h"
#include "shared/interface.h"
#include "shared/log.h"
#include "stub/dispatch.h"
#include "stub/load_policy.h"
#include "stub/packed_file.h"
#include "stub/ppc.h"

// OSSleepTicks: RSBE01 rev 2 symbol 0x801E1A80 (link/EXTRA.lst), declared
// as the storage code does.
extern "C" void OSSleepTicks(s64 ticks);

namespace tm {
namespace loader {
    // Temporary buffer heap: at boot for the whole plugin file (MenuInstance
    // has about 5 MB then, 0x4EB900), and at scene start for one module's REL
    // image at a time while linking (3.58 MB free on character select, 0x79A60
    // = 498 KB free at match start, empty in both; freed before the module
    // runs). The game checks MenuInstance for leftover blocks at scene end.
    static const Heaps::HeapType kTempHeap = Heaps::MenuInstance;

    // The same relative form Syriinge reads plugins with ("plugins/<file>"),
    // which P+'s file patch resolves under MOD_PATCH_DIR "pf/". Syriinge
    // does not tell a plugin its file name, so the name is fixed.
    static const char kPath[] = "plugins/" TM_PLUGIN_NAME ".rel";

    // Block and .bss both start 32-byte aligned (the header check limits a
    // module's own alignment to 32).
    static u32 align32(u32 n)
    {
        return (n + 31) & ~31u;
    }

    // Modules are linked as gfModule::create (0x800264D8) links the game's
    // own: the module file is read into a temporary buffer in kTempHeap, its
    // first fixSize bytes are copied into the permanent block, OSLinkFixed
    // applies every relocation against the tables still in the temporary
    // buffer, drops the import entries for the game and the module itself and
    // zeroes .bss at align32(fixSize) (Brawl decompilation, OSLink.c:176-261),
    // the kept import table is repointed into the permanent block and the
    // temporary buffer is freed. The permanent block holds only fixSize + .bss.
    static u32 bssOffset(const packed::Image& image)
    {
        return align32(image.fixSize);
    }

    static u32 blockSize(const packed::Image& image)
    {
        return bssOffset(image) + align32(image.bssSize);
    }

    typedef const iface::ModuleInfo* (*ModuleProlog)();
    typedef void (*ModuleEpilog)();

    struct Loaded {
        const packed::Image* image;
        Heaps::HeapType heap;
        u8* block;
        const OSModuleHeader* header;
        const iface::ModuleInfo* info;
        const gfScene* scene;
        game::HeapUsage before;
    };

    static packed::Image s_images[packed::kMaxModules];
    static u32 s_imageCount = 0;
    // The modules loaded for the current scene, in load order. A later one
    // may be linked against an earlier one, so they are unloaded last first.
    static Loaded s_mods[policy::kMaxPlacements];
    static u32 s_loadedCount = 0;
    static bool s_otherAllocationLogged = false;

    // Module kinds are four-letter tags ("MTCH"); logs print them as text.
#define TM_KIND "%.4s"
#define TM_KIND_ARG(kind) reinterpret_cast<const char*>(&(kind))

    void init()
    {
        game::HeapUsage before;
        bool measured = game::heapUsage(kTempHeap, before);
        gfFileIOHandle handle;
        bool read = handle.read(kPath, kTempHeap, 0);
        u8* buffer = static_cast<u8*>(handle.getBuffer());
        u32 size = static_cast<u32>(handle.getSize());
        const char* why = "could not read the file";
        if (read && game::isRam(buffer))
        {
            why = packed::inspect(buffer, size, s_images, s_imageCount);
        }
        // As Syriinge does: the buffer is ours to free, then the request.
        if (game::isRam(buffer))
        {
            free(buffer);
        }
        handle.release();

        game::HeapUsage after;
        if (!measured || !game::heapUsage(kTempHeap, after))
        {
            after.freeBytes = before.freeBytes = 0;
        }
        const char* root = game::isRam(MOD_PATCH_DIR) ? MOD_PATCH_DIR : "?";
        TM_LOG("modules: %spf/%s: %s; %u-byte file, %u module(s); MenuInstance free %x -> %x\n", root,
               kPath, why != NULL ? why : "ok", size, s_imageCount, before.freeBytes, after.freeBytes);
        for (u32 i = 0; i < s_imageCount; i++)
        {
            const packed::Image& m = s_images[i];
            TM_LOG("module " TM_KIND ": id %u, image at %x size %x, bss %x, checksum %08x: %s\n", TM_KIND_ARG(m.kind),
                   m.id, m.offset, m.size, m.bssSize, m.checksum, m.rejected != NULL ? m.rejected : "ok");
        }
    }

    // "sd:" + MOD_PATCH_DIR ("/Project+/") + "pf/" + kPath: the file P+'s
    // file patch serves for kPath, named on the SD card itself. False if
    // MOD_PATCH_DIR is not a terminated string.
    static const u32 kSdPathMax = 3 + sizeof(MOD_PATCH_DIR) + 3 + sizeof(kPath);
    static bool sdPath(char* out)
    {
        u32 at = 0;
        memcpy(out, "sd:", 3);
        at += 3;
        for (u32 i = 0; i < sizeof(MOD_PATCH_DIR) && MOD_PATCH_DIR[i] != '\0'; i++)
        {
            out[at++] = MOD_PATCH_DIR[i];
        }
        if (at == 3 || at == 3 + sizeof(MOD_PATCH_DIR))
        {
            return false;
        }
        memcpy(out + at, "pf/", 3);
        memcpy(out + at + 3, kPath, sizeof(kPath));
        return true;
    }

    // The plugin file's size on the SD card right now, as FAFstat gives it
    // (the FA library takes the path without "sd:"); 0 if it is not there.
    static u32 fileSizeNow(const char* path)
    {
        // Spare room past the declared record, as the storage code leaves.
        struct {
            FAStat st;
            u8 spare[32];
        } s;
        if (FAFstat(path + 3, &s.st) != 0 || s.st.filesize <= 0)
        {
            return 0;
        }
        return static_cast<u32>(s.st.filesize);
    }

    // Reads `size` bytes of the plugin file at `offset` into `dest`; the
    // game's file status (0 = read), -1 if the request could not be made.
    // Read in code (RSBE01 rev 2, P+ 3.2.1): readRequestNoSync sets address,
    // length and offset (setReadParam 0x8002239C: +0xC, +0x8, +0x4);
    // readSDFile (0x8001CBF4) seeks (FAFseek 0x8001CCDC) and reads `length`
    // bytes (P+'s "ReadSD Length Fix" 0x8001CCB8) into `address` ("ReadSD
    // Allocation Fix" 0x8001CD0C). The path must name "sd:": without a
    // device, P+'s file patch (0x8001BF38) has checkFileSD write the file's
    // size over the length (0x8001F654), so the whole file is read from the
    // offset (measured: a short read, status 1). read() always asks for the
    // whole file; the wait is read()'s.
    static int readRange(const char* path, u32 offset, u32 size, void* dest)
    {
        gfFileIOHandle handle;
        if (!handle.readRequestNoSync(path, dest, static_cast<int>(size), static_cast<int>(offset)))
        {
            handle.release();
            return -1;
        }
        while (!handle.isReady())
        {
            OSSleepTicks(OS_MSEC_TO_TICKS(1));
        }
        int status = handle.getReturnStatus();
        handle.release();
        return status;
    }

    // Undoes a link: runs the module's destructors, takes it off the OS
    // module list and frees its memory.
    static void unlinkAndFree(const OSModuleHeader* header, u8* block)
    {
        reinterpret_cast<ModuleEpilog>(header->epilog)();
        OSUnlink(const_cast<OSModuleInfo*>(&header->info));
        free(block);
    }

    static const char* checkInfo(const iface::ModuleInfo* info, u32 kind)
    {
        if (!game::isRam(info))
        {
            return "no module info";
        }
        if (info->magic != iface::kModuleMagic)
        {
            return "bad magic";
        }
        if (info->version != iface::kVersion)
        {
            return "interface version mismatch";
        }
        if (info->kind != kind)
        {
            return "wrong kind";
        }
        if (!game::isRam(reinterpret_cast<const void*>(info->attach))
            || !game::isRam(reinterpret_cast<const void*>(info->detach)))
        {
            return "no attach/detach";
        }
        return NULL;
    }

    static bool notLoaded(u32 kind, const char* why)
    {
        TM_LOG("module " TM_KIND ": not loaded: %s\n", TM_KIND_ARG(kind), why);
        return false;
    }

    static const packed::Image* findImage(u32 kind)
    {
        for (u32 i = 0; i < s_imageCount; i++)
        {
            if (s_images[i].kind == kind)
            {
                return &s_images[i];
            }
        }
        return NULL;
    }

    // Links a verified REL image in `temp` into permanent `block` (size
    // blockSize(image)), matching gfModule::create (0x800264D8): copies the
    // first fixSize bytes, shifts the header's and temp import table's
    // offsets by (temp - block) so OSLinkFixed reads relocations from `temp`,
    // then frees `temp`, repoints the kept import table into `block` (so
    // later links and OSUnlink never touch `temp`) and flushes code caches.
    // Always frees `temp` before returning.
    static bool linkFromTemp(u8* block, u8* temp, const packed::Image& image)
    {
        OSModuleHeader* src = reinterpret_cast<OSModuleHeader*>(temp);
        u32 impOffset = src->impOffset;
        u32 impSize = src->impSize;
        memcpy(block, temp, image.fixSize);

        OSModuleHeader* header = reinterpret_cast<OSModuleHeader*>(block);
        u32 delta = reinterpret_cast<u32>(temp) - reinterpret_cast<u32>(block);
        header->relOffset += delta;
        header->impOffset += delta;
        OSImportInfo* tempImp = reinterpret_cast<OSImportInfo*>(temp + impOffset);
        u32 impCount = impSize / sizeof(OSImportInfo);
        for (u32 i = 0; i < impCount; i++)
        {
            tempImp[i].offset += delta;
        }

        // fixSize from the boot check: linking turns the header's into an address.
        bool linked = OSLinkFixed(&header->info, block + bssOffset(image)) != 0;
        free(temp);
        if (!linked)
        {
            return false;
        }

        OSImportInfo* permImp = reinterpret_cast<OSImportInfo*>(block + impOffset);
        header->impOffset = reinterpret_cast<u32>(permImp);
        u32 keptCount = header->impSize / sizeof(OSImportInfo);
        for (u32 i = 0; i < keptCount; i++)
        {
            permImp[i].offset += reinterpret_cast<u32>(block);
        }
        // OSLinkFixed only flushes exec sections touched by R_DOLPHIN_SECTION;
        // flush the whole copied prefix so memcpy'd code is visible on hardware.
        ppc::flushCode(block, image.fixSize);
        return true;
    }

    // Reads one module into kTempHeap, checks it, links it into its permanent
    // block in `place.heap`, frees the temporary buffer and attaches it.
    static bool loadOne(const policy::Placement& place, const char* path, u32 fileSize, const gfScene* scene,
                        u32 startTick)
    {
        u32 kind = place.kind;
        Heaps::HeapType heap = place.heap;
        const packed::Image* image = findImage(kind);
        if (image == NULL || image->rejected != NULL)
        {
            return notLoaded(kind, image == NULL ? "none in file" : "rejected at boot");
        }
        game::HeapUsage before;
        if (!game::isRam(gfHeapManager::getHeap(heap)) || !game::heapUsage(heap, before))
        {
            return notLoaded(kind, "no such heap now");
        }
        if (place.onlyIfEmpty && before.allocated != 0)
        {
            TM_LOG("module " TM_KIND ": not loaded: %s holds %u blocks\n", TM_KIND_ARG(kind), game::heapName(heap),
                   before.allocated);
            return false;
        }
        u32 bytes = blockSize(*image);
        if (before.largest < bytes)
        {
            TM_LOG("module " TM_KIND ": not loaded: needs %x, largest free %x\n", TM_KIND_ARG(kind), bytes,
                   before.largest);
            return false;
        }
        if (path == NULL)
        {
            TM_LOG("modules: read failed: no SD path\n");
            return notLoaded(kind, "read failed");
        }
        if (fileSize < image->offset || fileSize - image->offset < image->size)
        {
            TM_LOG("module " TM_KIND ": read failed: %x bytes at %x, file now %x bytes\n", TM_KIND_ARG(kind),
                   image->size, image->offset, fileSize);
            return notLoaded(kind, "read failed");
        }
        game::HeapUsage tempBefore;
        if (!game::isRam(gfHeapManager::getHeap(kTempHeap)) || !game::heapUsage(kTempHeap, tempBefore))
        {
            return notLoaded(kind, "no temp heap");
        }
        if (tempBefore.largest < image->size)
        {
            TM_LOG("module " TM_KIND ": not loaded: temp needs %x, largest free %x\n", TM_KIND_ARG(kind),
                   image->size, tempBefore.largest);
            return false;
        }
        u8* temp = static_cast<u8*>(gfMemoryPool::alloc(gfHeapManager::getHeap(kTempHeap), image->size, 32));
        if (!game::isRam(temp))
        {
            return notLoaded(kind, "temp allocation failed");
        }
        int status = readRange(path, image->offset, image->size, temp);
        if (status != 0)
        {
            // A short read makes readSDFile free the destination (0x8001CDD4);
            // if kTempHeap's allocated count dropped back to tempBefore, do
            // not free `temp` again.
            game::HeapUsage tempAfter;
            bool freedByGame = game::heapUsage(kTempHeap, tempAfter) && tempAfter.allocated == tempBefore.allocated;
            if (!freedByGame)
            {
                free(temp);
            }
            TM_LOG("module " TM_KIND ": read failed: %x bytes at %x, status %d, block %s\n", TM_KIND_ARG(kind),
                   image->size, image->offset, status, freedByGame ? "freed by the game" : "kept");
            return notLoaded(kind, "read failed");
        }

        // The file may have changed since boot; never link what was not checked.
        const OSModuleHeader* src = reinterpret_cast<const OSModuleHeader*>(temp);
        const char* why = packed::checkHeader(src, image->size, image->id);
        if (why == NULL && packed::checksum(temp, image->size) != image->checksum)
        {
            why = "checksum mismatch";
        }
        if (why != NULL)
        {
            free(temp);
            return notLoaded(kind, why);
        }
        u8* block = static_cast<u8*>(gfMemoryPool::alloc(gfHeapManager::getHeap(heap), bytes, 32));
        if (!game::isRam(block))
        {
            free(temp);
            return notLoaded(kind, "allocation failed");
        }
        if (!linkFromTemp(block, temp, *image))
        {
            free(block);
            return notLoaded(kind, "OSLinkFixed failed");
        }

        const OSModuleHeader* header = reinterpret_cast<const OSModuleHeader*>(block);
        // A later module only registers with the ones before it; if it is
        // refused, the callbacks they filled are put back as they were.
        iface::Callbacks kept = dispatch::table().callbacks;
        const iface::ModuleInfo* info = reinterpret_cast<ModuleProlog>(header->prolog)();
        why = checkInfo(info, kind);
        if (why == NULL && !info->attach(&dispatch::table()))
        {
            why = "attach refused";
        }
        if (why != NULL)
        {
            dispatch::table().callbacks = kept;
            unlinkAndFree(header, block);
            TM_LOG("module " TM_KIND ": refused (%s; stub interface v%u), unloaded\n", TM_KIND_ARG(kind), why,
                   iface::kVersion);
            return false;
        }

        Loaded& mod = s_mods[s_loadedCount++];
        mod.image = image;
        mod.heap = heap;
        mod.block = block;
        mod.header = header;
        mod.info = info;
        mod.scene = scene;
        mod.before = before;
        s_otherAllocationLogged = false;
        u32 micros = OS_TICKS_TO_USEC(OSGetTick() - startTick);
        game::HeapUsage after;
        game::heapUsage(heap, after);
        TM_LOG("module " TM_KIND ": loaded v%s at %08x in %s; free %x largest %x blocks %u -> free %x largest %x"
               "; %u us\n",
               TM_KIND_ARG(kind), game::isRam(info->buildVersion) ? info->buildVersion : "?", block,
               game::heapName(heap), before.freeBytes, before.largest, before.allocated, after.freeBytes,
               after.largest, micros);
        return true;
    }

    void unload(const char* when)
    {
        if (s_loadedCount == 0)
        {
            return;
        }
        dispatch::clearCallbacks();
        while (s_loadedCount > 0)
        {
            const Loaded& mod = s_mods[--s_loadedCount];
            mod.info->detach();
            unlinkAndFree(mod.header, mod.block);

            game::HeapUsage after;
            bool exists = game::heapUsage(mod.heap, after);
            const game::HeapUsage& b = mod.before;
            bool baseline = exists && after.freeBytes == b.freeBytes && after.largest == b.largest
                            && after.freeBlocks == b.freeBlocks && after.allocated == b.allocated;
            TM_LOG("module " TM_KIND ": unloaded %s; free %x largest %x blocks %u, back to baseline: %s\n",
                   TM_KIND_ARG(mod.image->kind), when, after.freeBytes, after.largest, after.allocated,
                   baseline ? "yes" : "NO");
        }
    }

    void onSceneStart()
    {
        gfScene* scene = game::currentScene();
        policy::Placement placements[policy::kMaxPlacements];
        u32 count = policy::forSceneStart(scene, placements);
        if (count == 0 || s_loadedCount != 0)
        {
            return;
        }
        u32 startTick = OSGetTick();
        char path[kSdPathMax];
        const char* sd = s_imageCount != 0 && sdPath(path) ? path : NULL;
        u32 fileSize = sd != NULL ? fileSizeNow(sd) : 0;
        // Each module may depend on the ones before it, so everything stops
        // at the first one that cannot be loaded.
        for (u32 i = 0; i < count && loadOne(placements[i], sd, fileSize, scene, startTick); i++)
        {
        }
    }

    void onSceneExit()
    {
        if (s_loadedCount != 0 && game::currentScene() == s_mods[0].scene)
        {
            unload("at scene exit");
        }
    }

    s32 extraHeap()
    {
        for (u32 i = 0; i < s_loadedCount; i++)
        {
            if (s_mods[i].image->kind == iface::kKindExtra)
            {
                return static_cast<s32>(s_mods[i].heap);
            }
        }
        return -1;
    }

    void checkScene()
    {
        if (s_loadedCount == 0)
        {
            return;
        }
        if (game::currentScene() != s_mods[0].scene)
        {
            // The heaps may already be gone, so nothing in them is touched.
            dispatch::clearCallbacks();
            s_loadedCount = 0;
            TM_LOG("modules: still loaded in %s; callbacks off, memory abandoned\n", game::sceneName());
            return;
        }
        for (u32 i = 0; i < s_loadedCount && !s_otherAllocationLogged; i++)
        {
            const Loaded& mod = s_mods[i];
            game::HeapUsage now;
            if (game::heapUsage(mod.heap, now) && now.allocated != mod.before.allocated + 1)
            {
                s_otherAllocationLogged = true;
                TM_LOG("module " TM_KIND ": heap has %u blocks, expected %u\n", TM_KIND_ARG(mod.image->kind),
                       now.allocated, mod.before.allocated + 1);
            }
        }
    }
}
}
