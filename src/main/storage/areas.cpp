#include "main/storage/areas.h"

#include <memory.h>
#include <stddef.h>

#include "main/settings.h"
#include "main/storage/format.h"

namespace tm {
namespace storage {
    using main::Settings;

    struct AreaDef {
        u8 id;
        u8 generation;
        u16 offset; // in main::Settings
        u8 used;
        u8 size; // full area struct size in main::Settings
    };

    // `used` is the area's size without its reserved bytes, so a field
    // added from them extends what is stored.
#define TM_AREA(id, field, type) {id, 1, offsetof(Settings, field), offsetof(main::type, reserved), sizeof(main::type)}
    static const AreaDef kAreas[] = {
        TM_AREA(kAreaGeneral, general, GeneralSettings),
        TM_AREA(kAreaControls, controls, ControlSettings),
        TM_AREA(kAreaDisplay, display, DisplaySettings),
        TM_AREA(kAreaInfoHmn, info[0], InfoSettings),
        TM_AREA(kAreaInfoCpu, info[1], InfoSettings),
        TM_AREA(kAreaOsd, osd, OsdSettings),
        TM_AREA(kAreaCpu, cpu, CpuSettings),
        TM_AREA(kAreaRecording, recording, RecordingSettings),
        TM_AREA(kAreaOverlayHmn, overlay[0], OverlaySettings),
        TM_AREA(kAreaOverlayCpu, overlay[1], OverlaySettings),
        TM_AREA(kAreaTechInvis, techInvis, TechInvisSettings),
        {kAreaCpuTables, 1, offsetof(Settings, cpuTables), offsetof(cpu::CpuTables, reserved), sizeof(cpu::CpuTables)},
        {kAreaCharacterRng, 1, offsetof(Settings, characterRng), sizeof(main::CharacterRngSettings),
         sizeof(main::CharacterRngSettings)},
        TM_AREA(kAreaStageOptions, stageOptions, StageOptionsSettings),
        TM_AREA(kAreaComboTrainer, comboTrainer, ComboTrainerSettings),
        TM_AREA(kAreaFighterMechanics, fighterMechanics, FighterMechanicsSettings),
    };
#undef TM_AREA
    enum { kAreaCount = sizeof(kAreas) / sizeof(kAreas[0]) };
    TM_STATIC_ASSERT(kAreaCount <= kMaxAreas, tm_areas_fit);
    TM_STATIC_ASSERT(sizeof(main::GeneralSettings) <= 255 && sizeof(main::ControlSettings) <= 255
                         && sizeof(main::DisplaySettings) <= 255 && sizeof(main::InfoSettings) <= 255
                         && sizeof(main::OsdSettings) <= 255 && sizeof(main::CpuSettings) <= 255
                         && sizeof(main::RecordingSettings) <= 255 && sizeof(main::OverlaySettings) <= 255
                         && sizeof(main::TechInvisSettings) <= 255 && sizeof(cpu::CpuTables) <= 255
                         && sizeof(main::CharacterRngSettings) <= 255 && sizeof(main::StageOptionsSettings) <= 255
                         && sizeof(main::ComboTrainerSettings) <= 255 && sizeof(main::FighterMechanicsSettings) <= 255,
                     tm_area_sizes_fit_u8);

    // The largest settings file writeAreas() can make: the two headers, the
    // table, and every area with all of its reserved bytes in use (its whole
    // struct, rounded up to 4 as writeAreas rounds). One term per row of
    // kAreas, in its order; the count check fails the build when a row is
    // added or removed until the sum is brought in line. What it does not
    // catch: a row whose type is changed without changing its term here.
#define TM_ROOM(type) ((sizeof(type) + 3u) & ~3u)
    enum {
        kAreasWorstBytes = TM_ROOM(main::GeneralSettings) + TM_ROOM(main::ControlSettings) +
                           TM_ROOM(main::DisplaySettings) + 2 * TM_ROOM(main::InfoSettings) +
                           TM_ROOM(main::OsdSettings) + TM_ROOM(main::CpuSettings) +
                           TM_ROOM(main::RecordingSettings) + 2 * TM_ROOM(main::OverlaySettings) +
                           TM_ROOM(main::TechInvisSettings) + TM_ROOM(cpu::CpuTables) +
                           TM_ROOM(main::CharacterRngSettings) + TM_ROOM(main::StageOptionsSettings) +
                           TM_ROOM(main::ComboTrainerSettings) + TM_ROOM(main::FighterMechanicsSettings),
        kSettingsFileWorst = sizeof(FileHeader) + sizeof(SettingsHead) + kAreaCount * sizeof(AreaEntry) +
                             kAreasWorstBytes
    };
#undef TM_ROOM
    TM_STATIC_ASSERT(kAreaCount == 16, tm_areas_worst_case_counts_16_rows);
    TM_STATIC_ASSERT(kSettingsFileWorst <= kSettingsFileCap, tm_settings_file_fits_its_cap);

    static const AreaDef* findArea(int id)
    {
        for (int i = 0; i < kAreaCount; i++)
        {
            if (kAreas[i].id == id)
            {
                return &kAreas[i];
            }
        }
        return NULL;
    }

    bool areaBounds(int id, u16& offset, u16& size)
    {
        for (int i = 0; i < kAreaCount; i++)
        {
            if (kAreas[i].id == id)
            {
                offset = kAreas[i].offset;
                size = kAreas[i].size;
                return true;
            }
        }
        return false;
    }

    u32 areasRoom(u32 mask)
    {
        u32 room = 0;
        for (int i = 0; i < kAreaCount; i++)
        {
            if (mask & areaBit(kAreas[i].id))
            {
                room += sizeof(AreaEntry) + ((kAreas[i].used + 3u) & ~3u);
            }
        }
        return room;
    }

    u32 writeAreas(u8* out, u32 cap, u32 fileOffset, const u8* settings, u32 mask, u16& count)
    {
        count = 0;
        for (int i = 0; i < kAreaCount; i++)
        {
            count += (mask & areaBit(kAreas[i].id)) != 0 ? 1 : 0;
        }
        u32 at = count * sizeof(AreaEntry);
        if (at > cap)
        {
            return 0;
        }
        AreaEntry* table = reinterpret_cast<AreaEntry*>(out);
        int n = 0;
        for (int i = 0; i < kAreaCount; i++)
        {
            const AreaDef& a = kAreas[i];
            if ((mask & areaBit(a.id)) == 0)
            {
                continue;
            }
            u32 room = (a.used + 3u) & ~3u;
            if (at + room > cap)
            {
                return 0;
            }
            table[n].id = a.id;
            table[n].generation = a.generation;
            table[n].used = a.used;
            table[n].offset = fileOffset + at;
            memcpy(out + at, settings + a.offset, a.used);
            memset(out + at + a.used, 0, room - a.used);
            at += room;
            n++;
        }
        return at;
    }

    bool validAreas(const u8* file, u32 size, u32 table, u16 count)
    {
        if (table > size || count > kMaxAreas || table + count * sizeof(AreaEntry) > size)
        {
            return false;
        }
        const AreaEntry* entries = reinterpret_cast<const AreaEntry*>(file + table);
        for (u16 i = 0; i < count; i++)
        {
            if (entries[i].offset > size || entries[i].used > size - entries[i].offset)
            {
                return false;
            }
        }
        return true;
    }

    bool readAreas(const u8* file, u32 size, u32 table, u16 count, u8* settings, u32 mask, AreaStats& stats)
    {
        memset(&stats, 0, sizeof(stats));
        if (!validAreas(file, size, table, count))
        {
            return false;
        }
        const AreaEntry* entries = reinterpret_cast<const AreaEntry*>(file + table);
        for (int k = 0; k < kAreaCount; k++)
        {
            const AreaDef& a = kAreas[k];
            if ((mask & areaBit(a.id)) == 0)
            {
                continue;
            }
            const AreaEntry* e = NULL;
            for (u16 i = 0; i < count && e == NULL; i++)
            {
                e = entries[i].id == a.id ? &entries[i] : NULL;
            }
            if (e == NULL || e->generation != a.generation)
            {
                stats.reset++;
                continue;
            }
            u32 n = e->used < a.used ? e->used : a.used;
            memcpy(settings + a.offset, file + e->offset, n);
            stats.copied++;
            stats.grown += e->used < a.used ? 1 : 0;
        }
        for (u16 i = 0; i < count; i++)
        {
            stats.skipped += findArea(entries[i].id) == NULL ? 1 : 0;
        }
        return true;
    }
}
}
