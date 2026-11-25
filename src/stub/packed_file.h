#pragma once

#include <OS/OSLink.h>
#include <types.h>

// The plugin file is the stub REL followed by the modules the stub can load,
// a table describing them and a trailer (written by scripts/pack-rel.py;
// keep both in step):
//   [stub REL][zeros to 32][module REL, zero-padded to 32]...[table][trailer]
// All fields are big-endian u32.
namespace tm {
namespace packed {
    enum {
        kTrailerMagic = 0x544D504B, // "TMPK"
        kFormatVersion = 2,
        kTrailerSize = 0x20,
        kEntrySize = 0x20,
        kMaxModules = 4
    };

    struct Trailer {
        u32 magic;
        u32 format;
        u32 count;       // table entries
        u32 tableOffset; // from the start of the file; the table ends at the trailer
        u32 reserved[4];
    };

    struct Entry {
        u32 id;       // REL module id
        u32 kind;     // iface::kKind*
        u32 offset;   // of the REL from the start of the file; 32-aligned
        u32 size;     // of the REL including its padding; a multiple of 32
        u32 checksum; // FNV-1a (32-bit) of those `size` bytes
        u32 reserved[3];
    };

    // One table entry as checked at boot, plus what loading it needs.
    struct Image {
        u32 id;
        u32 kind;
        u32 offset;
        u32 size;
        u32 fixSize; // the header's: what stays needed once linked
        u32 bssSize;
        u32 checksum;
        const char* rejected; // NULL if usable, else why not
    };

    u32 checksum(const void* data, u32 size);

    // Checks the trailer and table at the end of `file` and every module they
    // describe. Returns NULL and fills images[0..count), each with its own
    // verdict, or the reason the whole table is unusable.
    const char* inspect(const u8* file, u32 fileSize, Image* images, u32& count);

    // Checks an unlinked REL header for what the loader relies on: its id,
    // format version 3, sections and tables inside `size`, fixSize inside
    // `size` with the section table, the import table and every section's
    // data before it (OSLinkFixed keeps only that part), alignment of at most
    // 32 and a prolog and epilog. Returns NULL or a short reason.
    const char* checkHeader(const OSModuleHeader* header, u32 size, u32 moduleId);
}
}
