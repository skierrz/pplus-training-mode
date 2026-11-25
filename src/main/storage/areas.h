#pragma once

#include <types.h>

// main::Settings' feature areas as files store them, and the one migration
// rule: a field is only ever added at the end of its area (from the front of
// the area's reserved bytes), so an area's first `used` bytes keep their
// meaning across versions. Reading an area copies the bytes both versions
// know; the rest keep the reader's defaults. An area whose generation
// differs (a field changed meaning) or that the file lacks keeps its
// defaults; an area the reader does not know is skipped.
namespace tm {
namespace storage {
    enum AreaId {
        kAreaGeneral = 1,
        kAreaControls,
        kAreaDisplay,
        kAreaInfoHmn,
        kAreaInfoCpu,
        kAreaOsd,
        kAreaCpu,
        kAreaRecording,
        kAreaOverlayHmn,
        kAreaOverlayCpu,
        kAreaTechInvis,
        kAreaCpuTables, // 12: Advanced Counter pages
        kAreaCharacterRng = 13,
        kAreaStageOptions = 14,
        kAreaComboTrainer = 15,
        kAreaFighterMechanics = 16,
        kAreaLast = kAreaFighterMechanics
    };

    inline u32 areaBit(int id)
    {
        return 1u << id;
    }
    static const u32 kAllAreas = 0x1FFFEu; // ids 1-16

    // Byte range of area `id` in main::Settings (including its reserved tail).
    // Returns false for invalid or unknown ids.
    bool areaBounds(int id, u16& offset, u16& size);

    // Writes the area table and the areas in `mask` taken from `settings`
    // (a main::Settings) at `out`; `fileOffset` is where `out` is in the
    // file. Returns the bytes written, 0 if `cap` is too small.
    u32 writeAreas(u8* out, u32 cap, u32 fileOffset, const u8* settings, u32 mask, u16& count);
    // The bytes writeAreas() writes for `mask`.
    u32 areasRoom(u32 mask);

    struct AreaStats {
        u8 copied;  // areas taken from the file (wholly or the fields both know)
        u8 grown;   // of those, areas the file had fewer fields of
        u8 reset;   // areas left at their defaults (missing, other generation)
        u8 skipped; // areas of the file this version does not know
    };

    // True if the area table and every entry lie within `size` bytes.
    bool validAreas(const u8* file, u32 size, u32 table, u16 count);

    // Applies the areas in `mask` from a file (`file`, `size` bytes, its
    // table of `count` entries at `table`) to `settings`. False if the table
    // or an area lies outside the file.
    bool readAreas(const u8* file, u32 size, u32 table, u16 count, u8* settings, u32 mask, AreaStats& stats);
}
}
