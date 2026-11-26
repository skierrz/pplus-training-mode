#include "stub/packed_file.h"

namespace tm {
namespace packed {
    enum {
        kFnvOffset = 0x811C9DC5,
        kFnvPrime = 0x01000193,
        kRelVersion = 3,
        kMaxSections = 64,
        kMaxAlign = 32,
        kMaxBss = 0x100000
    };

    u32 checksum(const void* data, u32 size)
    {
        const u8* p = static_cast<const u8*>(data);
        u32 hash = kFnvOffset;
        for (u32 i = 0; i < size; i++)
        {
            hash = (hash ^ p[i]) * kFnvPrime;
        }
        return hash;
    }

    static bool alignmentOk(u32 align)
    {
        return align <= kMaxAlign && (align & (align - 1)) == 0;
    }

    static bool inside(u32 offset, u32 bytes, u32 size)
    {
        return offset <= size && bytes <= size - offset;
    }

    const char* checkHeader(const OSModuleHeader* header, u32 size, u32 moduleId)
    {
        if (size < sizeof(OSModuleHeader))
        {
            return "too small";
        }
        const OSModuleInfo& info = header->info;
        if (info.id != moduleId)
        {
            return "wrong module id";
        }
        if (info.version != kRelVersion)
        {
            return "not a version 3 REL";
        }
        if (info.numSections == 0 || info.numSections > kMaxSections
            || !inside(info.sectionInfoOffset, info.numSections * sizeof(OSSectionInfo), size))
        {
            return "bad section table";
        }
        if (!inside(header->relOffset, 0, size) || !inside(header->impOffset, header->impSize, size))
        {
            return "bad tables";
        }
        // The loader puts .bss at fixSize, so nothing the module needs once
        // linked may lie past it. pack-rel.py also checks the import order
        // and the relocation lists that stay.
        u32 fix = header->fixSize;
        if (!inside(fix, 0, size) || !inside(info.sectionInfoOffset, info.numSections * sizeof(OSSectionInfo), fix)
            || !inside(header->impOffset, header->impSize, fix))
        {
            return "bad fixSize";
        }
        const OSSectionInfo* sections = reinterpret_cast<const OSSectionInfo*>(
            reinterpret_cast<const u8*>(header) + info.sectionInfoOffset);
        for (u32 i = 0; i < info.numSections; i++)
        {
            u32 at = sections[i].offset & ~OS_SECTIONINFO_EXEC;
            if (at != 0 && !inside(at, sections[i].size, fix))
            {
                return "bad fixSize";
            }
        }
        if (!alignmentOk(header->align) || !alignmentOk(header->bssAlign))
        {
            return "alignment above 32";
        }
        if (header->bssSize > kMaxBss)
        {
            return "bss too big";
        }
        if (header->prologSection == 0 || header->prologSection >= info.numSections
            || header->epilogSection == 0 || header->epilogSection >= info.numSections)
        {
            return "no prolog or epilog";
        }
        return NULL;
    }

    const char* inspect(const u8* file, u32 fileSize, Image* images, u32& count)
    {
        count = 0;
        if (fileSize < kTrailerSize || (fileSize & 31u) != 0u)
        {
            return "no trailer";
        }
        u32 end = fileSize - kTrailerSize;
        const Trailer* trailer = reinterpret_cast<const Trailer*>(file + end);
        if (trailer->magic != kTrailerMagic)
        {
            return "no trailer";
        }
        if (trailer->format != kFormatVersion)
        {
            return "unknown format";
        }
        if (trailer->count == 0 || trailer->count > kMaxModules || (trailer->tableOffset & 3u) != 0u
            || trailer->tableOffset > end || end - trailer->tableOffset != trailer->count * kEntrySize)
        {
            return "bad module table";
        }
        const Entry* entries = reinterpret_cast<const Entry*>(file + trailer->tableOffset);
        for (u32 i = 0; i < trailer->count; i++)
        {
            const Entry& e = entries[i];
            Image& out = images[i];
            out.id = e.id;
            out.kind = e.kind;
            out.offset = e.offset;
            out.size = e.size;
            out.checksum = e.checksum;
            out.bssSize = 0;
            out.rejected = NULL;
            if ((e.offset & 31) != 0 || (e.size & 31) != 0 || e.size == 0
                || !inside(e.offset, e.size, trailer->tableOffset))
            {
                out.rejected = "out of range";
                continue;
            }
            const OSModuleHeader* header = reinterpret_cast<const OSModuleHeader*>(file + e.offset);
            out.rejected = checkHeader(header, e.size, e.id);
            if (out.rejected == NULL && checksum(header, e.size) != e.checksum)
            {
                out.rejected = "checksum mismatch";
            }
            if (out.rejected == NULL)
            {
                out.fixSize = header->fixSize;
                out.bssSize = header->bssSize;
            }
        }
        count = trailer->count;
        return NULL;
    }
}
}
