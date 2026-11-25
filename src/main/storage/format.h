#pragma once

#include <types.h>

#include "shared/version.h"

// The files on the SD card (docs/files.md). Every number is big-endian, as
// the console stores it. Every file starts with a FileHeader; the checksum
// covers the bytes from `checksumFrom` to `size`, so the first 16 bytes are
// checked by value. The game pads every write to a multiple of 32 bytes.
namespace tm {
namespace storage {
    enum {
        kSettingsMagic = 0x544D5346,  // "TMSF"
        kSettingsFormat = 1,
        kRecordingMagic = 0x544D5246, // "TMRF"
        kRecordingFormat = 1,
        kChecksumFrom = 16,
        kMaxAreas = 16,
        // The largest settings file read or written, and the size of the
        // buffers it is built and read in (settings_file.cpp). areas.cpp
        // checks at build time that the largest file it can write fits.
        kSettingsFileCap = 768
    };

    struct FileHeader {
        u32 magic;
        u16 format;     // of this kind of file; a reader refuses a newer one
        u16 headerSize; // bytes before the kind's own data
        u32 size;       // the file's bytes before the padding
        u32 checksum;   // storage::checksum() of [kChecksumFrom, size)
        u32 sequence;   // settings: +1 per save; recordings: 0
        u8 reserved[12];
    };

    // A settings area as stored (settings and recordings): the bytes of one
    // feature area of main::Settings.
    struct AreaEntry {
        u8 id;         // AreaId
        u8 generation; // +1 when a field of the area changes meaning
        u16 used;      // bytes of the area's fields (its reserved bytes excluded)
        u32 offset;    // of the bytes from the start of the file
    };

    // The settings file, after the FileHeader: the table, then the areas.
    struct SettingsHead {
        u16 areaCount;
        u16 layout; // main::kSettingsVersion of the writer, for information
        u32 reserved;
    };

    // A recording file, after the FileHeader: what the browser lists.
    struct RecordingInfo {
        char name[32];  // the export's name: its file name without ".tmr"
        u8 hmnKind;     // gmCharacterKind
        u8 hmnColorFile; // the match's gmPlayerInitData +5, +6 (costume file, costume)
        u8 hmnColor;
        u8 hmnIcon;     // the character select's icon id
        u8 cpuKind;
        u8 cpuColorFile;
        u8 cpuColor;
        u8 cpuIcon;
        u16 stage;      // srStageKind
        u16 altStage;   // Project+'s alternate-stage button word at the start
        u16 frames;     // the longest slot, in game frames
        u8 hmnSlots;    // bit per slot holding inputs
        u8 cpuSlots;
        u16 year;
        u8 month, day, hour, minute, second;
        // The character select's own +5 and +6 for each player (what makes
        // the same costumes again), with hasSelect 1 when they were found.
        u8 hasSelect;
        u8 hmnSelect[2];
        u8 cpuSelect[2];
        u8 hazardOff;   // P+ m_isHazardOff (0 hazards ON, 1 hazards OFF)
        u8 reserved[3];
    };

    // Then: RecordingHead, its area table and areas, two Placements (HMN,
    // CPU), and `slotCount` slots (SlotHead, the tables, the frames).
    struct RecordingHead {
        u16 areaCount;
        u16 slotCount;
        u32 placements; // offset of the two Placements; the slots follow them
    };

    struct Placement {
        float x, y, z;
        float lr;      // facing: 1 or -1
        float damage;
        float shield;  // shield health
        u8 haveShield;
        u8 reserved[7];
    };

    struct SlotHead {
        u8 side;  // 0 HMN, 1 CPU
        u8 index; // 0-5
        u16 start; // record::Slot's start and end (frames since the start state)
        u16 end;
        u16 count[4]; // used entries of each float table (stick x, y, C-stick x, y)
        u16 lrCount;  // used (L, R) pairs
        u32 lossy;
        // Then count[k] float words per table, lrCount pairs of float words,
        // and (end - start) 8-byte frames.
    };

    TM_STATIC_ASSERT(sizeof(FileHeader) == 32, tm_file_header_size);
    TM_STATIC_ASSERT(sizeof(AreaEntry) == 8, tm_area_entry_size);
    TM_STATIC_ASSERT(sizeof(RecordingInfo) == 64, tm_recording_info_size);
    TM_STATIC_ASSERT(sizeof(RecordingHead) == 8, tm_recording_head_size);
    TM_STATIC_ASSERT(sizeof(Placement) == 32, tm_placement_size);
    TM_STATIC_ASSERT(sizeof(SlotHead) == 20, tm_slot_head_size);

    enum {
        kRecordingListBytes = sizeof(FileHeader) + sizeof(RecordingInfo), // what the browser reads
        kRecordingHeadBytes = kRecordingListBytes + sizeof(RecordingHead),
        kRecordingMinBytes = kRecordingHeadBytes + 2 * sizeof(Placement),
        kRecordingSuffixLen = 4
    };
    static const char kRecordingSuffix[] = ".tmr";

    // Checks the header of a file of `magic` read whole (`bytes` read):
    // kOk, kNewer, kOlder, kCorrupt.
    int checkHeader(const void* file, u32 bytes, u32 magic, u16 format, bool whole);

    // Next save sequence number: 1 after 0, wrapping past 0xFFFFFFFF back to 1
    // so 0 stays reserved for "no valid file".
    inline u32 nextSequence(u32 sequence)
    {
        return sequence == 0xFFFFFFFFu ? 1u : sequence + 1u;
    }

    // Serial-number comparison between two valid save sequences (handles wrap
    // across 0xFFFFFFFF -> 1 cleanly).
    inline bool sequenceNewer(u32 a, u32 b)
    {
        return static_cast<s32>(a - b) > 0;
    }
}
}
