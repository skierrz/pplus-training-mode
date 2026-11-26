#include "main/storage/recording_file.h"

#include <OS/OSTime.h>
#include <cstdio>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <gm/gm_global.h>
#include <memory.h>
#include <stddef.h>

#include "main/cpu/lab_data.h"
#include "main/game/match.h"
#include "main/lab.h"
#include "main/record/record.h"
#include "main/record/slots.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/block.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/notice.h"
#include "main/ui/lab_menu.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// OSTicksToCalendarTime: RSBE01 rev 2 symbol 0x801E1D80 (link/EXTRA.lst).
namespace tm {
namespace storage {
    using main::Settings;

    enum {
        // Built and read in one block of the Network heap, which a Training
        // match leaves free except for the costume decompression buffer (up to
        // 0x10BA20 bytes, leaving ~0x37D40) while a fighter's files load, and
        // the extra module's resident working buffers (~30 KiB). Requiring
        // kTempReserve (512 KiB) past `bytes` waits out costume decompression
        // while coexisting cleanly with extra's Network buffers.
        kTempHeap = Heaps::Network,
        kTempMax = 0x30000,
        kTempReserve = 0x80000,
        kWaitMs = 5000,
        // gmGlobalModeMelee (gm_global_mode_melee.h: the stage kind at
        // m_meleeInitData +0x12, P+ m_isHazardOff at m_meleeInitData +0x21
        // bit 0x20 per P+ Random.asm:1024-1031, the gmPlayerInitData array
        // at +0x98) and gmSelCharData (gm_sel_char_data.h; its icon words at
        // +0x8 are described in menu/game/training.cpp).
        kModeStage = 0x1A,
        kModeHazardFlags = 0x29,
        kModeHazardOffBit = 0x20,
        kModePlayers = 0x98,
        kPlayerSize = 0x5C,
        kPlayerKind = 0x0,
        kPlayerState = 0x1,
        kPlayerColorFile = 0x5,
        kPlayerColor = 0x6,
        kSelIcons = 0x8,
        kSelPlayers = 0xB8,
        kPorts = 4
    };
    static const u32 kRecordingAreas = (1u << kAreaRecording) | (1u << kAreaCpu) | (1u << kAreaCpuTables);
    // Project+'s matched alternate-stage button word (StageFiles.asm:761-763
    // copies the matched ASL entry from 0x800B9EA2 into 0x8053EFBA at stage
    // load and zeroes 0x800B9EA2).
    static const volatile u16* const kAltStageButtons = reinterpret_cast<const volatile u16*>(0x8053EFBA);

    enum Phase { kIdle, kExportFolder, kExportWrite, kImportWait, kImportRead };
    // Rendered frames an import has waited for its two buffers (the game
    // can hold load-time data in both heaps as the match starts; not
    // checked in the game's code).
    enum { kImportWaitFrames = 900 };
    static int s_phase = kIdle;
    static Job s_job;
    static bool s_jobStale = false; // timed-out request still draining
    static u8* s_buffer = NULL;
    static u32 s_size = 0;
    static char s_name[kNameMax];
    static int s_waited = 0;

    // ---- the temporary buffer ----

    static u8* allocTemp(u32 bytes, bool quiet = false)
    {
        game::HeapUsage u;
        if (bytes > kTempMax || !game::heapUsage(kTempHeap, u) || u.largest < bytes + kTempReserve)
        {
            if (quiet)
            {
                return NULL;
            }
            TM_LOG("storage: no buffer of %x in %s (blocks %u, largest %x)\n", bytes, game::heapName(kTempHeap),
                   u.allocated, u.largest);
            return NULL;
        }
        void* p = gfMemoryPool::alloc(gfHeapManager::getHeap(static_cast<Heaps::HeapType>(kTempHeap)), bytes, 32);
        return game::isRam(p) ? static_cast<u8*>(p) : NULL;
    }

    static void freeTemp()
    {
        if (s_buffer != NULL)
        {
            free(s_buffer);
            s_buffer = NULL;
        }
    }

    // ---- the match ----

    static u8* modePlayer(int player)
    {
        GameGlobal* g = g_GameGlobal;
        if (player < 0 || player >= kPorts || !game::isRam(g) || !game::isRam(g->m_modeMelee))
        {
            return NULL;
        }
        return reinterpret_cast<u8*>(g->m_modeMelee) + kModePlayers + player * kPlayerSize;
    }

    // The character select's record of the player of that kind and state
    // (its icon puts the tokens back after the match; +5 and +6 make the
    // same costume again). False if it holds none.
    static bool selectOf(u8 kind, u8 state, u8& icon, u8* select)
    {
        GameGlobal* g = g_GameGlobal;
        icon = kind;
        if (game::isRam(g) && game::isRam(g->m_selCharData))
        {
            u8* sel = reinterpret_cast<u8*>(g->m_selCharData);
            for (int n = 0; n < kPorts; n++)
            {
                u8* p = sel + kSelPlayers + n * kPlayerSize;
                if (p[kPlayerKind] == kind && p[kPlayerState] == state)
                {
                    icon = static_cast<u8>(game::wordAt(sel, kSelIcons + 4 * n) >> 8);
                    select[0] = p[kPlayerColorFile];
                    select[1] = p[kPlayerColor];
                    return true;
                }
            }
        }
        return false;
    }

    // ---- export ----

    static u32 slotBytes(const record::Slot& s)
    {
        u32 n = sizeof(SlotHead) + 8u * s.lrCount + 8u * (s.end - s.start);
        for (int k = 0; k < record::kTables; k++)
        {
            n += 4u * s.count[k];
        }
        return n;
    }

    static void fileName(char* out, const OSCalendarTime& t)
    {
        char base[24];
        snprintf(base, sizeof(base), "%02d%02d%02d-%02d%02d%02d", t.year % 100, t.month + 1, t.mday, t.hour, t.min,
                 t.sec);
        snprintf(out, kNameMax, "%s%s", base, kRecordingSuffix);
        for (int i = 2; i < 100 && exists(out); i++)
        {
            snprintf(out, kNameMax, "%s-%d%s", base, i, kRecordingSuffix);
        }
    }

    static void failNotice(bool isExport, const char* msg)
    {
        notice(kNoticeError, isExport ? "Export failed: %s" : "Import failed: %s", msg);
    }

    bool exportAvailable()
    {
        return record::positionsSaved();
    }

    void exportRecording()
    {
        if (s_phase != kIdle || s_jobStale || s_job.busy())
        {
            notice(kNoticeWarn, "The SD card is busy: try again");
            return;
        }
        record::LitePlacement place[2];
        record::Players who;
        if (!record::liteGet(place, who))
        {
            notice(kNoticeError, "Export: save positions first");
            return;
        }
        int w = writable();
        if (w != kOk)
        {
            failNotice(true, resultText(w));
            return;
        }
        u8* hmn = modePlayer(who.hmn);
        u8* cpu = modePlayer(who.cpu);
        GameGlobal* g = g_GameGlobal;
        if (hmn == NULL || cpu == NULL)
        {
            failNotice(true, "no match setup");
            return;
        }

        u32 slots = 0;
        u32 areasSize = areasRoom(kRecordingAreas);
        u32 size = sizeof(FileHeader) + sizeof(RecordingInfo) + sizeof(RecordingHead) + areasSize
                   + 2 * sizeof(Placement);
        for (int side = 0; side < record::kSides; side++)
        {
            for (int i = 0; i < record::kSlotCount; i++)
            {
                const record::Slot* s = record::slotAt(side, i);
                if (s != NULL && !record::slotEmpty(*s))
                {
                    size += slotBytes(*s);
                    slots++;
                }
            }
        }
        if (size > kTempMax)
        {
            failNotice(true, "the recording is too long");
            return;
        }
        s_buffer = allocTemp(padded(size));
        if (s_buffer == NULL)
        {
            failNotice(true, resultText(kNoMemory));
            return;
        }
        memset(s_buffer, 0, padded(size));

        OSCalendarTime t;
        OSTicksToCalendarTime(OSGetTime(), &t);
        fileName(s_name, t);
        RecordingInfo* info = reinterpret_cast<RecordingInfo*>(s_buffer + sizeof(FileHeader));
        for (u32 i = 0; i + 1 < sizeof(info->name) && s_name[i] != '\0' && s_name[i] != '.'; i++)
        {
            info->name[i] = s_name[i];
        }
        const u8* mode = reinterpret_cast<const u8*>(g->m_modeMelee);
        u16 stage = *reinterpret_cast<const u16*>(mode + kModeStage);
        info->hmnKind = hmn[kPlayerKind];
        info->hmnColorFile = hmn[kPlayerColorFile];
        info->hmnColor = hmn[kPlayerColor];
        info->cpuKind = cpu[kPlayerKind];
        info->cpuColorFile = cpu[kPlayerColorFile];
        info->cpuColor = cpu[kPlayerColor];
        bool found = selectOf(hmn[kPlayerKind], hmn[kPlayerState], info->hmnIcon, info->hmnSelect);
        found = selectOf(cpu[kPlayerKind], cpu[kPlayerState], info->cpuIcon, info->cpuSelect) && found;
        info->hasSelect = found ? 1 : 0;
        info->stage = stage;
        info->altStage = *kAltStageButtons;
        info->hazardOff = (mode[kModeHazardFlags] & kModeHazardOffBit) != 0 ? 1 : 0;
        info->year = static_cast<u16>(t.year);
        info->month = static_cast<u8>(t.month + 1);
        info->day = static_cast<u8>(t.mday);
        info->hour = static_cast<u8>(t.hour);
        info->minute = static_cast<u8>(t.min);
        info->second = static_cast<u8>(t.sec);

        u32 at = sizeof(FileHeader) + sizeof(RecordingInfo);
        RecordingHead* head = reinterpret_cast<RecordingHead*>(s_buffer + at);
        at += sizeof(RecordingHead);
        u16 areas = 0;
        u32 n = writeAreas(s_buffer + at, areasSize, at, reinterpret_cast<const u8*>(&main::settings()),
                           kRecordingAreas, areas);
        if (n != areasSize)
        {
            freeTemp();
            failNotice(true, "the settings did not fit");
            return;
        }
        at += n;
        head->areaCount = areas;
        head->slotCount = static_cast<u16>(slots);
        head->placements = at;
        for (int i = 0; i < 2; i++)
        {
            Placement* p = reinterpret_cast<Placement*>(s_buffer + at);
            p->x = place[i].x;
            p->y = place[i].y;
            p->z = place[i].z;
            p->lr = place[i].lr;
            p->damage = place[i].damage;
            p->shield = place[i].haveShield ? place[i].shield : 0.0f;
            p->haveShield = place[i].haveShield ? 1 : 0;
            at += sizeof(Placement);
        }
        u32 longest = 0;
        for (int side = 0; side < record::kSides; side++)
        {
            for (int i = 0; i < record::kSlotCount; i++)
            {
                const record::Slot* s = record::slotAt(side, i);
                if (s == NULL || record::slotEmpty(*s))
                {
                    continue;
                }
                SlotHead* sh = reinterpret_cast<SlotHead*>(s_buffer + at);
                sh->side = static_cast<u8>(side);
                sh->index = static_cast<u8>(i);
                sh->start = s->start;
                sh->end = s->end;
                for (int k = 0; k < record::kTables; k++)
                {
                    sh->count[k] = s->count[k];
                }
                sh->lrCount = s->lrCount;
                sh->lossy = s->lossy;
                at += sizeof(SlotHead);
                for (int k = 0; k < record::kTables; k++)
                {
                    memcpy(s_buffer + at, s->table[k], 4u * s->count[k]);
                    at += 4u * s->count[k];
                }
                memcpy(s_buffer + at, s->lr, 8u * s->lrCount);
                at += 8u * s->lrCount;
                memcpy(s_buffer + at, &s->frames[s->start], 8u * (s->end - s->start));
                at += 8u * (s->end - s->start);
                longest = s->end > longest ? s->end : longest;
                if (side == 0)
                {
                    info->hmnSlots |= static_cast<u8>(1 << i);
                }
                else
                {
                    info->cpuSlots |= static_cast<u8>(1 << i);
                }
            }
        }
        info->frames = static_cast<u16>(longest);
        FileHeader* h = reinterpret_cast<FileHeader*>(s_buffer);
        h->magic = kRecordingMagic;
        h->format = kRecordingFormat;
        h->headerSize = sizeof(FileHeader);
        h->size = at;
        h->checksum = checksum(s_buffer + kChecksumFrom, at - kChecksumFrom);
        s_size = at;
        TM_LOG("storage: export %s: kinds %u/%u, costumes %u.%u/%u.%u (select %u: %u.%u/%u.%u), icons %x/%x, stage %u "
               "alt %x hazOff %u, %u slots, %u frames, %u bytes\n",
               s_name, info->hmnKind, info->cpuKind, info->hmnColorFile, info->hmnColor, info->cpuColorFile,
               info->cpuColor, info->hasSelect, info->hmnSelect[0], info->hmnSelect[1], info->cpuSelect[0],
               info->cpuSelect[1], info->hmnIcon, info->cpuIcon, stage, info->altStage, info->hazardOff, slots,
               longest, at);
        if (!s_job.makeFolder())
        {
            freeTemp();
            notice(kNoticeWarn, "The SD card is busy: try again");
            return;
        }
        s_phase = kExportFolder;
    }

    // ---- import ----

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static bool finiteIn(float v, float lo, float hi)
    {
        union {
            float f;
            u32 u;
        } bits;
        bits.f = v;
        if ((bits.u & 0x7F800000u) == 0x7F800000u)
        {
            return false; // NaN or Inf
        }
        return v >= lo && v <= hi;
    }

    static bool finiteBitsIn(u32 u, float lo, float hi)
    {
        union {
            float f;
            u32 u;
        } bits;
        bits.u = u;
        return finiteIn(bits.f, lo, hi);
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    static bool validSlot(const u8* f, u32 size, u32& at)
    {
        if (at > size || size - at < sizeof(SlotHead))
        {
            return false;
        }
        const SlotHead* sh = reinterpret_cast<const SlotHead*>(f + at);
        if (sh->side >= record::kSides || sh->index >= record::kSlotCount || sh->lrCount > record::kPaletteSize
            || sh->end > record::kMaxFrames || sh->end <= sh->start)
        {
            return false;
        }
        u32 floats = 0;
        for (int t = 0; t < record::kTables; t++)
        {
            if (sh->count[t] > record::kPaletteSize)
            {
                return false;
            }
            floats += sh->count[t];
        }
        u32 frames = sh->end - sh->start;
        u32 bytes = sizeof(SlotHead) + 4u * floats + 8u * sh->lrCount + 8u * frames;
        if (size - at < bytes)
        {
            return false;
        }
        u32 cur = at + sizeof(SlotHead);
        for (int t = 0; t < record::kTables; t++)
        {
            const u32* tab = reinterpret_cast<const u32*>(f + cur);
            for (u16 i = 0; i < sh->count[t]; i++)
            {
                if (!finiteBitsIn(tab[i], -2.0f, 2.0f))
                {
                    return false;
                }
            }
            cur += 4u * sh->count[t];
        }
        const u32* lr = reinterpret_cast<const u32*>(f + cur);
        for (u32 i = 0; i < 2u * sh->lrCount; i++)
        {
            if (!finiteBitsIn(lr[i], -2.0f, 2.0f))
            {
                return false;
            }
        }
        cur += 8u * sh->lrCount;
        const record::Packed* packed = reinterpret_cast<const record::Packed*>(f + cur);
        for (u32 i = 0; i < frames; i++)
        {
            if (packed[i].x >= sh->count[record::kTableX] || packed[i].y >= sh->count[record::kTableY]
                || packed[i].cx >= sh->count[record::kTableCx] || packed[i].cy >= sh->count[record::kTableCy]
                || packed[i].lr >= sh->lrCount)
            {
                return false;
            }
        }
        at += bytes;
        return true;
    }

    static bool validPlacement(const Placement& p)
    {
        return finiteIn(p.x, -10000.0f, 10000.0f) && finiteIn(p.y, -10000.0f, 10000.0f)
               && finiteIn(p.z, -10000.0f, 10000.0f) && finiteIn(p.lr, -1.0f, 1.0f)
               && finiteIn(p.damage, 0.0f, 999.0f)
               && (p.haveShield == 0 || finiteIn(p.shield, -200.0f, 200.0f));
    }

    static const char* applyImport(const u8* f, u32 size)
    {
        u32 table = kRecordingHeadBytes;
        if (size < kRecordingMinBytes)
        {
            return "the file is damaged";
        }
        const RecordingInfo* info = reinterpret_cast<const RecordingInfo*>(f + sizeof(FileHeader));
        const RecordingHead* head = reinterpret_cast<const RecordingHead*>(f + sizeof(FileHeader) + sizeof(RecordingInfo));
        if (!validAreas(f, size, table, head->areaCount))
        {
            return "the file is damaged";
        }
        u32 minPlacements = table + static_cast<u32>(head->areaCount) * sizeof(AreaEntry);
        if ((head->placements & 3u) != 0u || head->placements < minPlacements || head->placements > size
            || size - head->placements < 2 * sizeof(Placement))
        {
            return "the file is damaged";
        }
        const Placement* p = reinterpret_cast<const Placement*>(f + head->placements);
        if (!validPlacement(p[0]) || !validPlacement(p[1]))
        {
            return "the file is damaged";
        }
        // Validate every slot before mutating any slot or setting.
        u32 checkAt = head->placements + 2 * sizeof(Placement);
        for (u16 k = 0; k < head->slotCount; k++)
        {
            if (!validSlot(f, size, checkAt))
            {
                return "the file is damaged";
            }
        }
        if (!record::storageReady())
        {
            return "no room for the recording in memory";
        }
        for (int side = 0; side < record::kSides; side++)
        {
            for (int i = 0; i < record::kSlotCount; i++)
            {
                record::slotClear(*record::slotAt(side, i));
            }
        }
        u32 at = head->placements + 2 * sizeof(Placement);
        for (u16 k = 0; k < head->slotCount; k++)
        {
            const SlotHead* sh = reinterpret_cast<const SlotHead*>(f + at);
            u32 frames = sh->end - sh->start;
            record::Slot* s = record::slotAt(sh->side, sh->index);
            at += sizeof(SlotHead);
            for (int t = 0; t < record::kTables; t++)
            {
                memset(s->table[t], 0, sizeof(s->table[t]));
                memcpy(s->table[t], f + at, 4u * sh->count[t]);
                s->count[t] = sh->count[t];
                at += 4u * sh->count[t];
            }
            memset(s->lr, 0, sizeof(s->lr));
            memcpy(s->lr, f + at, 8u * sh->lrCount);
            s->lrCount = sh->lrCount;
            at += 8u * sh->lrCount;
            memcpy(&s->frames[sh->start], f + at, 8u * frames);
            at += 8u * frames;
            s->start = sh->start;
            s->end = sh->end;
            s->lossy = sh->lossy;
        }
        // The Recording, CPU Options and CPU tables areas (kRecordingAreas;
        // the CPU's damage and lock stay).
        Settings& s = main::settings();
        u16 percent = s.cpu.percent;
        u8 lock = s.cpu.lockPercent;
        AreaStats stats;
        if (!readAreas(f, size, table, head->areaCount, reinterpret_cast<u8*>(&s), kRecordingAreas, stats))
        {
            return "the file is damaged";
        }
        s.cpu.percent = percent;
        s.cpu.lockPercent = lock;
        ui::clampSettings();
        main::sanitizeSettings(s);
        ui::retireDroppedValues();
        cpu::advancedAttach();

        record::LitePlacement place[2];
        for (int i = 0; i < 2; i++)
        {
            place[i].x = p[i].x;
            place[i].y = p[i].y;
            place[i].z = p[i].z;
            place[i].lr = p[i].lr;
            place[i].damage = p[i].damage;
            place[i].shield = p[i].haveShield != 0 ? p[i].shield : 0.0f;
            place[i].haveShield = p[i].haveShield != 0;
        }
        record::importStart(place);
        char name[sizeof(info->name)];
        u32 n = 0;
        while (n + 1 < sizeof(name) && info->name[n] != '\0')
        {
            name[n] = info->name[n];
            n++;
        }
        name[n] = '\0';
        TM_LOG("storage: import %s: kinds %u/%u, stage %u, %u slots, %u frames; areas %u copied, %u reset\n", s_name,
               info->hmnKind, info->cpuKind, info->stage, head->slotCount, info->frames, stats.copied, stats.reset);
        notice(kNoticeOk, "Loaded %s", name);
        return NULL;
    }

    static void startImportRead()
    {
        game::MatchState m = game::matchState();
        if (!m.running || m.paused || lab::menuOpen())
        {
            return;
        }
        if (s_jobStale || s_job.busy())
        {
            return;
        }
        bool last = ++s_waited >= kImportWaitFrames || record::storageGaveUp();
        if (!record::storageReady() && !last)
        {
            return;
        }
        s_buffer = record::storageReady() ? allocTemp(padded(s_size), !last) : NULL;
        if (s_buffer == NULL && !last)
        {
            return;
        }
        if (s_buffer == NULL)
        {
            s_phase = kIdle;
            TM_LOG("storage: import %s gave up after %d frames: no room\n", s_name, s_waited);
            failNotice(false, resultText(kNoMemory));
            return;
        }
        if (!s_job.read(s_name, s_buffer, s_size))
        {
            freeTemp();
            s_phase = kIdle;
            failNotice(false, resultText(kBusy));
            return;
        }
        s_phase = kImportRead;
    }

    static void importRead(int result, int status)
    {
        const char* why = NULL;
        if (result != kOk)
        {
            TM_LOG("storage: import %s: read status %d\n", s_name, status);
            why = resultText(result == kMissing ? kCorrupt : result);
        }
        else
        {
            int r = checkHeader(s_buffer, s_size, kRecordingMagic, kRecordingFormat, true);
            why = r != kOk ? resultText(r) : applyImport(s_buffer, s_size);
        }
        freeReadBuffer(s_buffer, result, status);
        s_phase = kIdle;
        if (why != NULL)
        {
            TM_LOG("storage: import %s failed: %s\n", s_name, why);
            failNotice(false, why);
        }
    }

    // ---- per frame ----

    void recordingAttach(u8* block)
    {
        s_phase = kIdle;
        s_jobStale = s_job.busy();
        s_buffer = NULL;
        Handoff* h = blockHandoff(block);
        if (h == NULL || h->magic != kHandoffMagic)
        {
            return;
        }
        // Taken once, whatever happens next.
        h->magic = 0;
        u32 n = 0;
        while (n + 1 < kNameMax && h->name[n] != '\0')
        {
            s_name[n] = h->name[n];
            n++;
        }
        s_name[n] = '\0';
        s_size = h->size;
        if (s_size < kRecordingListBytes || s_size > kTempMax)
        {
            TM_LOG("storage: import %s refused: size %u\n", s_name, s_size);
            return;
        }
        s_phase = kImportWait;
        s_waited = 0;
        TM_LOG("storage: import %s (%u bytes) waits for the match\n", s_name, s_size);
    }

    void recordingUpdate()
    {
        int result = kOk;
        int status = 0;
        if (s_jobStale)
        {
            if (!s_job.poll(result, status))
            {
                return;
            }
            s_jobStale = false;
        }
        if (s_phase == kImportWait)
        {
            startImportRead();
            return;
        }
        if (s_phase == kIdle || !s_job.poll(result, status))
        {
            return;
        }
        switch (s_phase)
        {
        case kExportFolder:
            if (!s_job.write(s_name, s_buffer, s_size))
            {
                freeTemp();
                s_phase = kIdle;
                failNotice(true, resultText(kBusy));
                break;
            }
            s_phase = kExportWrite;
            break;
        case kExportWrite:
            freeTemp();
            s_phase = kIdle;
            if (result == kOk)
            {
                TM_LOG("storage: exported %s (%u bytes)\n", s_name, s_size);
                notice(kNoticeOk, "Exported %s - load from Training Menu (X)", s_name);
            }
            else
            {
                TM_LOG("storage: export %s failed: status %d\n", s_name, status);
                failNotice(true, resultText(result));
            }
            break;
        case kImportRead:
            importRead(result, status);
            break;
        default:
            break;
        }
    }

    void recordingDetach()
    {
        int status = 0;
        if (s_phase == kExportFolder || s_phase == kExportWrite || s_phase == kImportRead)
        {
            int r = s_job.wait(kWaitMs, status);
            if (s_phase == kExportFolder && r != kBusy)
            {
                r = s_job.write(s_name, s_buffer, s_size) ? s_job.wait(kWaitMs, status) : kBusy;
            }
            if (r == kBusy)
            {
                s_jobStale = true;
            }
            TM_LOG("storage: %s finished at detach: %s\n", s_phase == kImportRead ? "import" : "export",
                   resultText(r));
            if (s_phase == kImportRead)
            {
                freeReadBuffer(s_buffer, r, status);
            }
            else if (r == kBusy)
            {
                // Never free memory the file thread may still use.
                s_buffer = NULL;
            }
        }
        freeTemp();
        s_phase = kIdle;
    }
}
}
