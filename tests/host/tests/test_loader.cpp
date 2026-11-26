// Module loader temporary-buffer linking, cache maintenance, import table
// repointing and failure/cleanup paths (src/stub/loader.cpp).
#include <OS/OSCache.h>
#include <OS/OSLink.h>

#include "host/card.h"
#include "host/fakes.h"
#include "host/log.h"
#include "host/mem.h"
#include "host/test.h"
#include "stub/loader.cpp"

namespace {
    using namespace tm;

    static const char kPluginPath[] = "/Project+/pf/plugins/" TM_PLUGIN_NAME ".rel";
    static const u32 kModuleId = 21582;
    static const u32 kExtraId = 21584;
    static const u32 kImportedId = 27; // sora_melee (kept by OSLinkFixed)

    // Synthetic REL layout: file size (0x300) > align32(fixSize) + align32(bssSize)
    // (0x180 + 0x40 = 0x1C0), so a permanent block strictly smaller than the
    // file exercises the temporary-buffer path and guard-byte checks.
    enum {
        kSecOff = 0x50,        // 3 sections (24 bytes): [0x50, 0x68)
        kTextOff = 0x80,       // .text (0x40 bytes): [0x80, 0xC0)
        kTextSize = 0x40,
        kImpOff = 0xC0,        // 3 imports (24 bytes): [0xC0, 0xD8)
        kImpCount = 3,
        kImpSize = kImpCount * sizeof(OSImportInfo),
        kKeptRelOff = 0xE0,    // relocations for kImportedId (kept, <= fixSize)
        kFixSize = 0x180,
        kBssSize = 0x40,
        kGameRelOff = 0x180,   // relocations for module 0 (dropped, past fixSize)
        kSelfRelOff = 0x200,   // relocations for self (dropped, past fixSize)
        kRelSize = 0x300       // padded file size
    };

    struct LinkTrace {
        bool failLink;
        int linkCalls;
        int unlinkCalls;
        int prologCalls;
        int epilogCalls;
        bool sawTempImpsInTemp;
        bool sawTempEmptyInProlog;
        bool sawPermImpsInBlockAtProlog;
        bool sawPermImpsInBlockAtUnlink;
        u32 lastBlock;
        u32 lastBss;
        u32 lastTemp;
        u32 lastTempSize;
        u32 lastBlockSize;
        u32 lastFixSize;
        u32 lastOrigImpOff;
        u32 lastKeptRelOff;
        u32 lastImpsAfterLink;
        iface::ModuleInfo* prologReturn;
    };

    static LinkTrace s_trace;
    static iface::Interface s_iface;
    static policy::Placement s_placements[policy::kMaxPlacements];
    static u32 s_placementCount = 0;

    void fakeEpilog()
    {
        s_trace.epilogCalls++;
    }

    const iface::ModuleInfo* fakeProlog()
    {
        s_trace.prologCalls++;
        // Temporary buffer must already be freed before _prolog runs.
        s_trace.sawTempEmptyInProlog = (host::fakes::heapBlocks(Heaps::MenuInstance) == 0);
        // Permanent block's import table must already point inside `block`,
        // with kept entries relocated relative to `block`.
        const OSModuleHeader* h = reinterpret_cast<const OSModuleHeader*>(s_trace.lastBlock);
        u32 expectedImp = s_trace.lastBlock + s_trace.lastOrigImpOff;
        const OSImportInfo* permImp = reinterpret_cast<const OSImportInfo*>(h->impOffset);
        s_trace.sawPermImpsInBlockAtProlog =
            (h->impOffset == expectedImp) && (h->impSize == sizeof(OSImportInfo))
            && (permImp[0].id == kImportedId)
            && (permImp[0].offset == s_trace.lastBlock + s_trace.lastKeptRelOff);
        // Poison the freed temp memory: any later access into `temp` will read 0xCC.
        if (s_trace.lastTemp != 0 && s_trace.lastTempSize != 0)
        {
            memset(reinterpret_cast<void*>(s_trace.lastTemp), 0xCC, s_trace.lastTempSize);
        }
        return s_trace.prologReturn;
    }

    void buildRel(u8* out, u32 moduleId)
    {
        memset(out, 0, kRelSize);
        OSModuleHeader* h = reinterpret_cast<OSModuleHeader*>(out);
        h->info.id = moduleId;
        h->info.numSections = 3;
        h->info.sectionInfoOffset = kSecOff;
        h->info.version = OS_MODULE_VERSION;
        h->bssSize = kBssSize;
        h->relOffset = kKeptRelOff;
        h->impOffset = kImpOff;
        h->impSize = kImpSize;
        h->prologSection = 1;
        h->epilogSection = 1;
        h->prolog = 0;
        h->epilog = 4;
        h->align = 32;
        h->bssAlign = 32;
        h->fixSize = kFixSize;

        OSSectionInfo* secs = reinterpret_cast<OSSectionInfo*>(out + kSecOff);
        secs[1].offset = kTextOff | OS_SECTIONINFO_EXEC;
        secs[1].size = kTextSize;
        secs[2].offset = 0;
        secs[2].size = kBssSize;

        OSImportInfo* imps = reinterpret_cast<OSImportInfo*>(out + kImpOff);
        imps[0].id = kImportedId;
        imps[0].offset = kKeptRelOff;
        imps[1].id = 0;
        imps[1].offset = kGameRelOff;
        imps[2].id = moduleId;
        imps[2].offset = kSelfRelOff;

        // Place R_DOLPHIN_END markers at each relocation list (one before
        // fixSize, two past fixSize in the tail).
        reinterpret_cast<OSRel*>(out + kKeptRelOff)->type = R_DOLPHIN_END;
        reinterpret_cast<OSRel*>(out + kGameRelOff)->type = R_DOLPHIN_END;
        reinterpret_cast<OSRel*>(out + kSelfRelOff)->type = R_DOLPHIN_END;
    }

    struct PackedImage {
        u8 bytes[0x100 + 2 * kRelSize + 2 * packed::kEntrySize + packed::kTrailerSize];
        u32 size;
        u32 modOffset[2];
    };

    void packPlugin(PackedImage& img, u32 count, const u32* kinds, const u32* ids)
    {
        memset(&img, 0, sizeof(img));
        u32 pos = 0x40; // stub placeholder
        u32 modOff[2] = {0, 0};
        u32 modSum[2] = {0, 0};
        for (u32 i = 0; i < count; i++)
        {
            modOff[i] = pos;
            img.modOffset[i] = pos;
            buildRel(img.bytes + pos, ids[i]);
            modSum[i] = packed::checksum(img.bytes + pos, kRelSize);
            pos += kRelSize;
        }
        u32 tableOff = pos;
        packed::Entry* entries = reinterpret_cast<packed::Entry*>(img.bytes + tableOff);
        for (u32 i = 0; i < count; i++)
        {
            entries[i].id = ids[i];
            entries[i].kind = kinds[i];
            entries[i].offset = modOff[i];
            entries[i].size = kRelSize;
            entries[i].checksum = modSum[i];
        }
        pos += count * packed::kEntrySize;
        packed::Trailer* trailer = reinterpret_cast<packed::Trailer*>(img.bytes + pos);
        trailer->magic = packed::kTrailerMagic;
        trailer->format = packed::kFormatVersion;
        trailer->count = count;
        trailer->tableOffset = tableOff;
        pos += packed::kTrailerSize;
        img.size = pos;
    }

    void resetLoader(const PackedImage& img)
    {
        memset(&s_trace, 0, sizeof(s_trace));
        memset(&s_iface, 0, sizeof(s_iface));
        loader::s_imageCount = 0;
        loader::s_loadedCount = 0;
        loader::s_otherAllocationLogged = false;
        host::fakes::setHeapRoom(Heaps::MenuInstance, 0x00400000u);
        host::fakes::setHeapRoom(Heaps::OverlayMenu, 0x00400000u);
        host::fakes::setHeapRoom(Heaps::AssistFigureResource, 0x00400000u);
        host::fakes::setHeapRoom(Heaps::OverlayFighter4, 0x00400000u);
        host::fakes::clearCacheOps();
        host::fakes::setTrainingSelectRunning(true);
        host::card::putRaw(kPluginPath, img.bytes, img.size);
        loader::init();
    }

    bool flushedCode(u32 addr, u32 bytes)
    {
        const host::fakes::CacheOp* ops = host::fakes::cacheOps();
        int n = host::fakes::cacheOpCount();
        int dc = -1;
        for (int i = 0; i < n; i++)
        {
            if (!ops[i].icache && ops[i].addr <= addr && ops[i].addr + ops[i].bytes >= addr + bytes)
            {
                dc = i;
                break;
            }
        }
        if (dc < 0)
        {
            return false;
        }
        for (int i = dc + 1; i < n; i++)
        {
            if (ops[i].icache && ops[i].addr <= addr && ops[i].addr + ops[i].bytes >= addr + bytes)
            {
                return true;
            }
        }
        return false;
    }
}

namespace tm {
namespace dispatch {
    iface::Interface& table()
    {
        return s_iface;
    }
    void clearCallbacks()
    {
        memset(&s_iface.callbacks, 0, sizeof(s_iface.callbacks));
    }
}

namespace policy {
    u32 forSceneStart(const gfScene*, Placement out[kMaxPlacements])
    {
        for (u32 i = 0; i < s_placementCount; i++)
        {
            out[i] = s_placements[i];
        }
        return s_placementCount;
    }
}

namespace game {
    const char* sceneName()
    {
        return "scMelee";
    }
}
}

extern "C" int OSLinkFixed(OSModuleInfo* newModule, void* bss)
{
    s_trace.linkCalls++;
    if (s_trace.failLink)
    {
        return 0;
    }
    OSModuleHeader* h = reinterpret_cast<OSModuleHeader*>(newModule);
    u32 base = reinterpret_cast<u32>(h);
    u32 fix = h->fixSize;
    u32 bssAddr = reinterpret_cast<u32>(bss);
    if ((base & 31) != 0 || (bssAddr & 31) != 0 || h->info.version != OS_MODULE_VERSION)
    {
        return 0;
    }

    // Resolve header pointers as OSLink.c:195-216 does.
    h->info.sectionInfoOffset += base;
    h->relOffset += base;
    h->impOffset += base;
    h->fixSize += base;

    u32 tempBase = h->impOffset - kImpOff;
    s_trace.lastBlock = base;
    s_trace.lastBss = bssAddr;
    s_trace.lastTemp = tempBase;
    s_trace.lastTempSize = kRelSize;
    s_trace.lastFixSize = fix;
    s_trace.lastOrigImpOff = kImpOff;
    s_trace.lastKeptRelOff = kKeptRelOff;

    OSSectionInfo* secs = reinterpret_cast<OSSectionInfo*>(h->info.sectionInfoOffset);
    for (u32 i = 1; i < h->info.numSections; i++)
    {
        if (secs[i].offset != 0)
        {
            secs[i].offset += base;
        }
        else if (secs[i].size != 0)
        {
            h->bssSection = static_cast<u8>(i);
            secs[i].offset = bssAddr;
        }
    }

    // Verify all import entries resolve into `temp`, including the relocation
    // lists past fixSize, and check their R_DOLPHIN_END markers there.
    bool allInTemp = (tempBase != base);
    OSImportInfo* imps = reinterpret_cast<OSImportInfo*>(h->impOffset);
    u32 count = h->impSize / sizeof(OSImportInfo);
    for (u32 i = 0; i < count; i++)
    {
        imps[i].offset += base;
        u32 at = imps[i].offset;
        if (at < tempBase || at + sizeof(OSRel) > tempBase + kRelSize)
        {
            allInTemp = false;
        }
        else if (reinterpret_cast<const OSRel*>(at)->type != R_DOLPHIN_END)
        {
            allInTemp = false;
        }
    }
    s_trace.sawTempImpsInTemp = allInTemp;

    // Truncate impSize at the first import from module 0 or self (OSLink.c:246-255).
    for (u32 i = 0; i < count; i++)
    {
        if (imps[i].id == 0 || imps[i].id == h->info.id)
        {
            h->impSize = i * sizeof(OSImportInfo);
            break;
        }
    }
    s_trace.lastImpsAfterLink = h->impSize / sizeof(OSImportInfo);

    // Zero .bss at align32(fixSize) (OSLink.c:257).
    memset(bss, 0, h->bssSize);

    // Point prolog/epilog at our host test callbacks.
    h->prolog = reinterpret_cast<u32>(&fakeProlog);
    h->epilog = reinterpret_cast<u32>(&fakeEpilog);
    return 1;
}

extern "C" int OSUnlink(OSModuleInfo* oldModule)
{
    s_trace.unlinkCalls++;
    OSModuleHeader* h = reinterpret_cast<OSModuleHeader*>(oldModule);
    u32 base = reinterpret_cast<u32>(h);
    // Walk kept imports just as OSLink.c:406-410 does; must point inside
    // `block` (not freed & 0xCC-poisoned `temp`) with intact R_DOLPHIN_END.
    bool inBlock = (h->impOffset >= base && h->impOffset + h->impSize <= base + kFixSize);
    OSImportInfo* imps = reinterpret_cast<OSImportInfo*>(h->impOffset);
    u32 count = h->impSize / sizeof(OSImportInfo);
    for (u32 i = 0; i < count; i++)
    {
        u32 at = imps[i].offset;
        if (at < base || at + sizeof(OSRel) > base + kFixSize
            || reinterpret_cast<const OSRel*>(at)->type != R_DOLPHIN_END)
        {
            inBlock = false;
        }
        imps[i].offset -= base;
    }
    s_trace.sawPermImpsInBlockAtUnlink = inBlock;
    h->relOffset -= base;
    h->impOffset -= base;
    h->info.sectionInfoOffset -= base;
    return 1;
}

TEST(loader, links_through_temp_buffer_repoints_imports_flushes_and_unlinks)
{
    PackedImage img;
    const u32 kinds[1] = {iface::kKindMatch};
    const u32 ids[1] = {kModuleId};
    packPlugin(img, 1, kinds, ids);
    resetLoader(img);
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));

    // Give OverlayMenu room for fixSize + bssSize (0x1C0), strictly LESS than
    // the file size (0x300): proves the permanent block is only fixSize + bss.
    const u32 permBytes = ((kFixSize + 31) & ~31u) + ((kBssSize + 31) & ~31u);
    REQUIRE(permBytes < kRelSize);
    host::fakes::setHeapRoom(Heaps::OverlayMenu, permBytes);

    // Return a ModuleInfo with mismatched version so loader runs prolog,
    // restores callbacks, calls epilog + OSUnlink (with temp poisoned) and frees block.
    iface::ModuleInfo* bad = host::mem::make<iface::ModuleInfo>();
    bad->magic = iface::kModuleMagic;
    bad->version = iface::kVersion + 1;
    bad->kind = iface::kKindMatch;
    s_trace.prologReturn = bad;

    s_placements[0].kind = iface::kKindMatch;
    s_placements[0].heap = Heaps::OverlayMenu;
    s_placements[0].onlyIfEmpty = false;
    s_placementCount = 1;

    loader::onSceneStart();

    CHECK_EQ(1, s_trace.linkCalls);
    CHECK_EQ(1, s_trace.prologCalls);
    CHECK_EQ(1, s_trace.epilogCalls);
    CHECK_EQ(1, s_trace.unlinkCalls);
    CHECK(s_trace.sawTempImpsInTemp);
    CHECK(s_trace.sawTempEmptyInProlog);
    CHECK(s_trace.sawPermImpsInBlockAtProlog);
    CHECK(s_trace.sawPermImpsInBlockAtUnlink);
    CHECK_EQ(1u, s_trace.lastImpsAfterLink);
    CHECK(flushedCode(s_trace.lastBlock, kFixSize));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::OverlayMenu));
}

TEST(loader, short_read_does_not_double_free_temp_buffer)
{
    PackedImage img;
    const u32 kinds[1] = {iface::kKindMatch};
    const u32 ids[1] = {kModuleId};
    packPlugin(img, 1, kinds, ids);
    resetLoader(img);

    s_placements[0].kind = iface::kKindMatch;
    s_placements[0].heap = Heaps::OverlayMenu;
    s_placements[0].onlyIfEmpty = false;
    s_placementCount = 1;

    // Next read fails as a short read: readSDFile frees `temp` itself.
    host::card::setShortRead(1);
    loader::onSceneStart();

    CHECK_EQ(0, s_trace.linkCalls);
    CHECK_LOG("freed by the game");
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::OverlayMenu));
}

TEST(loader, non_short_read_failure_frees_temp_buffer_once)
{
    PackedImage img;
    const u32 kinds[1] = {iface::kKindMatch};
    const u32 ids[1] = {kModuleId};
    packPlugin(img, 1, kinds, ids);
    resetLoader(img);

    s_placements[0].kind = iface::kKindMatch;
    s_placements[0].heap = Heaps::OverlayMenu;
    s_placements[0].onlyIfEmpty = false;
    s_placementCount = 1;

    // Non-short read error: game keeps `temp`, loader must free it once.
    host::card::setReadStatus(1, 1);
    loader::onSceneStart();

    CHECK_EQ(0, s_trace.linkCalls);
    CHECK_LOG("block kept");
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::OverlayMenu));
}

TEST(loader, failure_paths_free_both_buffers_without_leaks)
{
    PackedImage img;
    const u32 kinds[1] = {iface::kKindMatch};
    const u32 ids[1] = {kModuleId};
    packPlugin(img, 1, kinds, ids);

    s_placements[0].kind = iface::kKindMatch;
    s_placements[0].heap = Heaps::OverlayMenu;
    s_placements[0].onlyIfEmpty = false;
    s_placementCount = 1;

    // 1. Temp heap too small for file size.
    resetLoader(img);
    host::fakes::setHeapRoom(Heaps::MenuInstance, kRelSize - 1);
    loader::onSceneStart();
    CHECK_EQ(0, s_trace.linkCalls);
    CHECK_LOG("temp needs");
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::OverlayMenu));

    // 2. Checksum mismatch on SD card after boot: temp freed, block never allocated.
    resetLoader(img);
    img.bytes[img.modOffset[0] + kTextOff] ^= 0xFF;
    host::card::putRaw(kPluginPath, img.bytes, img.size);
    loader::onSceneStart();
    CHECK_EQ(0, s_trace.linkCalls);
    CHECK_LOG("checksum mismatch");
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::OverlayMenu));
    img.bytes[img.modOffset[0] + kTextOff] ^= 0xFF;

    // 3. OSLinkFixed fails: both temp and block freed exactly once.
    resetLoader(img);
    s_trace.failLink = true;
    loader::onSceneStart();
    CHECK_EQ(1, s_trace.linkCalls);
    CHECK_EQ(0, s_trace.prologCalls);
    CHECK_LOG("OSLinkFixed failed");
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::OverlayMenu));

    // 4. File shrank on SD card since boot: fails before allocating either buffer.
    resetLoader(img);
    host::card::putRaw(kPluginPath, img.bytes, img.modOffset[0] + kRelSize - 0x20);
    loader::onSceneStart();
    CHECK_EQ(0, s_trace.linkCalls);
    CHECK_LOG("read failed");
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::OverlayMenu));

    // 5. Target heap too small for permanent block: fails before reading or allocating temp.
    resetLoader(img);
    const u32 permBytes = ((kFixSize + 31) & ~31u) + ((kBssSize + 31) & ~31u);
    host::fakes::setHeapRoom(Heaps::OverlayMenu, permBytes - 1);
    int readsBefore = host::card::reads();
    loader::onSceneStart();
    CHECK_EQ(readsBefore, host::card::reads());
    CHECK_EQ(0, s_trace.linkCalls);
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
    CHECK_EQ(0, host::fakes::heapBlocks(Heaps::OverlayMenu));

    // 6. onlyIfEmpty heap holds another block (both AssistFigureResource and OverlayFighter4):
    // refuses before reading or allocating temp.
    static const Heaps::HeapType kOnlyEmptyHeaps[2] = {Heaps::AssistFigureResource, Heaps::OverlayFighter4};
    for (u32 hIdx = 0; hIdx < 2; hIdx++)
    {
        Heaps::HeapType targetHeap = kOnlyEmptyHeaps[hIdx];
        resetLoader(img);
        void* other = gfMemoryPool::alloc(gfHeapManager::getHeap(targetHeap), 0x40, 32);
        REQUIRE(other != NULL);
        s_placements[0].heap = targetHeap;
        s_placements[0].onlyIfEmpty = true;
        readsBefore = host::card::reads();
        loader::onSceneStart();
        CHECK_EQ(readsBefore, host::card::reads());
        CHECK_EQ(0, s_trace.linkCalls);
        CHECK_EQ(0, host::fakes::heapBlocks(Heaps::MenuInstance));
        CHECK_EQ(1, host::fakes::heapBlocks(targetHeap));
        free(other);
        CHECK_EQ(0, host::fakes::heapBlocks(targetHeap));
    }
}

TEST(loader, packed_inspect_rejects_unaligned_size_and_table_offset)
{
    PackedImage img;
    const u32 kinds[1] = {iface::kKindMatch};
    const u32 ids[1] = {kModuleId};
    packPlugin(img, 1, kinds, ids);

    packed::Image out[packed::kMaxModules];
    u32 count = 0;
    REQUIRE(packed::inspect(img.bytes, img.size, out, count) == NULL);
    CHECK_EQ(1u, count);

    // 1. Non-32-byte-aligned fileSize rejected.
    u8 shifted[sizeof(img.bytes) + 32];
    memset(shifted, 0, sizeof(shifted));
    memcpy(shifted + 4, img.bytes, img.size);
    CHECK_STR("no trailer", packed::inspect(shifted, img.size + 4, out, count));
    CHECK_EQ(0u, count);

    // 2. Non-4-byte-aligned tableOffset rejected.
    PackedImage bad = img;
    packed::Trailer* tr = reinterpret_cast<packed::Trailer*>(bad.bytes + bad.size - packed::kTrailerSize);
    tr->tableOffset += 1u;
    CHECK_STR("bad module table", packed::inspect(bad.bytes, bad.size, out, count));
    CHECK_EQ(0u, count);

    // 3. Wraparound tableOffset > end rejected (tableOffset + count * 0x20 wraps to end).
    u8 wrapBuf[0x60];
    memset(wrapBuf, 0, sizeof(wrapBuf));
    tr = reinterpret_cast<packed::Trailer*>(wrapBuf + sizeof(wrapBuf) - packed::kTrailerSize);
    tr->magic = packed::kTrailerMagic;
    tr->format = packed::kFormatVersion;
    tr->count = 4; // 4 * 0x20 = 0x80 > end (0x40)
    tr->tableOffset = 0x40u - 0x80u; // 0xFFFFFFC0: wraps around to 0x40 == end
    CHECK_STR("bad module table", packed::inspect(wrapBuf, sizeof(wrapBuf), out, count));
    CHECK_EQ(0u, count);
}
