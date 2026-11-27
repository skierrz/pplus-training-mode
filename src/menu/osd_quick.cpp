#include "menu/osd_quick.h"

#include <cstdio>
#include <gf/gf_pad_status.h>
#include <stddef.h>
#include <string.h>

#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/storage/areas.h"
#include "main/storage/block.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/ui/osd.h"
#include "main/ui/osd_place.h"
#include "main/ui/style.h"
#include "menu/services.h"
#include "menu/sound.h"
#include "shared/game/menu_sound.h"
#include "shared/game/pad_map.h"
#include "shared/log.h"

namespace tm {
namespace menu {
namespace osdQuick {
    using main::OsdSettings;
    using main::Settings;
    using storage::AreaEntry;
    using storage::FileHeader;
    using storage::SettingsHead;

    enum {
        kOsdUsed = offsetof(OsdSettings, reserved),
        kCpuTechOsdOff = offsetof(main::CpuSettings, options.techTrackerOsd),
        kRowOffTechTracker = 0xFF,
        kOsdGen = 1,
        kWaitMs = 3000,
        kStickDeadZone = 40,
        kVisibleRows = 9,
        kRepeatStart = ui::style::kRepeatStart,
        kRepeatEvery = ui::style::kRepeatEvery
    };
    TM_STATIC_ASSERT(kOsdUsed == 30, tm_osd_quick_used_bytes);

    static const char* const kNames[2] = {"settings-a.bin", "settings-b.bin"};
    static const char* const kPresets[] = {"Custom", "Essentials", "Movement", "Defense", "All Off"};
    static const char* const kPositions[] = {"HUD", "Sides", "Top Left", "Top Right"};
    static const char* const kTiming[] = {"Off", "All", "L-Cancel", "Wavedash", "Act-Out"};
    static const char* const kSparkle[] = {"Off", "Perfect Wavedash", "Shorthop", "Both"};

    enum RowKind { kKindChoice = 0, kKindInt = 1, kKindToggle = 2 };

    struct QuickRow {
        const char* name;
        u8 kind;
        u8 offset; // offsetof(OsdSettings, field), or kRowOffTechTracker for cpu.options.techTrackerOsd
        u8 minVal;
        u8 maxVal;
        const char* const* labels;
        const char* help;
    };

#define TM_QOFF(f) static_cast<u8>(offsetof(OsdSettings, f))
    static const QuickRow kRows[] = {
        {"OSD Preset",          kKindChoice, TM_QOFF(preset),              0, main::kOsdPresetCount - 1,   kPresets,
         "Apply a curated bundle of on-screen displays (OSDs)."},
        {"OSD Position",        kKindChoice, TM_QOFF(position),            0, osd::kPositionCount - 1,     kPositions,
         "Where OSD notifications appear on screen during matches."},
        {"Max OSDs",            kKindInt,    TM_QOFF(maxMessages),         1, osd::kPerPlayer,             NULL,
         "Maximum simultaneous OSD messages stacked per player."},
        {"Overhead Timing Bar", kKindChoice, TM_QOFF(timingMeter),         0, main::kTimingMeterCount - 1, kTiming,
         "Floating bar over fighters for L-Cancel, Wavedash, or Act-Out."},
        {"Tech Sparkle",        kKindChoice, TM_QOFF(techSparkle),         0, main::kTechSparkleCount - 1, kSparkle,
         "Visual sparkle cue on Frame-1 Wavedashes or crisp Shorthops."},
        {"P+ Tech OSD",         kKindToggle, kRowOffTechTracker,           0, 1,                           NULL,
         "Show Project+ tech readouts (RAR, DACUS, B-Reverse, CC/ASDI)."},
        {"Wavedash",            kKindToggle, TM_QOFF(wavedash),            0, 1,                           NULL,
         "Show Wavedash timing frame and airdodge angle."},
        {"L-Cancel",            kKindToggle, TM_QOFF(lCancel),             0, 1,                           NULL,
         "Show L-Cancel timing window (1-7f) and success rate."},
        {"Ledgedash Info",      kKindToggle, TM_QOFF(ledgedashInfo),       0, 1,                           NULL,
         "Show GALINT (invincible frames actionable onstage) after ledge."},
        {"Act OoS Frame",       kKindToggle, TM_QOFF(actOos),              0, 1,                           NULL,
         "Show frames taken to act Out of Shield (OoS) after blockstun."},
        {"Shield Advantage",    kKindToggle, TM_QOFF(frameAdvantage),      0, 1,                           NULL,
         "Show +/- frame advantage when an attack hits a shield."},
        {"Hit Advantage",       kKindToggle, TM_QOFF(frameAdvantageOnHit), 0, 1,                           NULL,
         "Show +/- frame advantage when an attack connects on hit."},
        {"Advantage Tolerance", kKindInt,    TM_QOFF(advantageTolerance),  0, 5,                           NULL,
         "Frames of slack before Shield/Hit Advantage stops measuring."},
        {"Dashback",            kKindToggle, TM_QOFF(dashback),            0, 1,                           NULL,
         "Report direct dashback vs slow-turn frames."},
        {"Powershield",         kKindToggle, TM_QOFF(powershield),         0, 1,                           NULL,
         "Show 2-frame Powershield timing and early/buffered misses."},
        {"Act OoLag",           kKindToggle, TM_QOFF(actOoLag),            0, 1,                           NULL,
         "Show frames waited before acting out of landing or attack lag."},
        {"Act OoAirborne",      kKindToggle, TM_QOFF(actOoAirborne),       0, 1,                           NULL,
         "Show first-frame aerial or special timing after leaving ground."},
        {"Act OoHitstun",       kKindToggle, TM_QOFF(actOoHitstun),        0, 1,                           NULL,
         "Show frames waited before acting out of hitstun or tumble."},
        {"Jump Cancel",         kKindToggle, TM_QOFF(jumpCancel),          0, 1,                           NULL,
         "Show jump-cancel grab and up-smash timing frames."},
        {"Fastfall Timing",     kKindToggle, TM_QOFF(fastfall),            0, 1,                           NULL,
         "Show how many frames after apex you triggered fastfall."},
        {"Boost Grab / DACUS",  kKindToggle, TM_QOFF(boostGrab),           0, 1,                           NULL,
         "Show Dash Attack Cancelled Up Smash (DACUS) and Boost Grab frames."},
        {"SDI Inputs",          kKindToggle, TM_QOFF(sdi),                 0, 1,                           NULL,
         "Count Smash Directional Influence (SDI) shifts during hitlag."},
        {"Tech Lockout",        kKindToggle, TM_QOFF(lockoutTimers),       0, 1,                           NULL,
         "Warn when a tech press lands inside the 40-frame lockout window."},
        {"Combo Counter",       kKindToggle, TM_QOFF(comboCounter),        0, 1,                           NULL,
         "Show true-combo hit count and total damage dealt."},
        {"Grab Escape",         kKindToggle, TM_QOFF(grabBreakout),        0, 1,                           NULL,
         "Show grab mash breakout speed and frames spent grabbed."},
        {"Item Throw",          kKindToggle, TM_QOFF(itemThrow),           0, 1,                           NULL,
         "Show item throw and glide-toss execution timing."}
    };
#undef TM_QOFF
    enum { kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0])) };

    // Matches osd_menu.cpp's 19 OsdSettings toggle offsets (bit 19 is CPU techTrackerOsd).
    static const u8 kPresetOffsets[19] = {
        static_cast<u8>(offsetof(OsdSettings, wavedash)),
        static_cast<u8>(offsetof(OsdSettings, lCancel)),
        static_cast<u8>(offsetof(OsdSettings, ledgedashInfo)),
        static_cast<u8>(offsetof(OsdSettings, actOos)),
        static_cast<u8>(offsetof(OsdSettings, frameAdvantage)),
        static_cast<u8>(offsetof(OsdSettings, frameAdvantageOnHit)),
        static_cast<u8>(offsetof(OsdSettings, dashback)),
        static_cast<u8>(offsetof(OsdSettings, powershield)),
        static_cast<u8>(offsetof(OsdSettings, actOoLag)),
        static_cast<u8>(offsetof(OsdSettings, actOoAirborne)),
        static_cast<u8>(offsetof(OsdSettings, actOoHitstun)),
        static_cast<u8>(offsetof(OsdSettings, jumpCancel)),
        static_cast<u8>(offsetof(OsdSettings, fastfall)),
        static_cast<u8>(offsetof(OsdSettings, boostGrab)),
        static_cast<u8>(offsetof(OsdSettings, sdi)),
        static_cast<u8>(offsetof(OsdSettings, lockoutTimers)),
        static_cast<u8>(offsetof(OsdSettings, comboCounter)),
        static_cast<u8>(offsetof(OsdSettings, grabBreakout)),
        static_cast<u8>(offsetof(OsdSettings, itemThrow))
    };
    static const u32 kPresetMasks[main::kOsdPresetCount - 1] = {
        0x0003Fu, // Essentials
        0x83B47u, // Movement (includes P+ Tech OSD bit 19)
        0x2C498u, // Defense
        0x00000u  // All Off
    };

    static u8 s_techTrackerOsd = 0;

    void defaultOsdSettings(OsdSettings& out)
    {
        memset(&out, 0, sizeof(out));
        out.position = osd::kPositionSides;
        out.frameAdvantage = 1;
        out.frameAdvantageOnHit = 1;
        out.advantageTolerance = 1;
        out.wavedash = 1;
        out.lCancel = 1;
        out.actOos = 1;
        out.ledgedashInfo = 1;
        out.maxMessages = 4;
        out.bgColor = main::kOsdBgTeal;
        out.bgAlpha = main::kDefaultOsdAlpha;
        out.menuAccent = main::kMenuAccentTeal;
        out.infoBgColor = main::kInfoBgDefault;
        out.infoBgAlpha = main::kInfoAlphaDefault;
        out.timingMeter = main::kTimingMeterOff;
        out.preset = main::kOsdPresetEssentials;
        out.techSparkle = main::kTechSparkleWavedash;
    }

    static void applyPreset(OsdSettings& osd, u8 preset)
    {
        if (preset == main::kOsdPresetCustom || preset >= main::kOsdPresetCount)
        {
            return;
        }
        u32 mask = kPresetMasks[preset - 1];
        u8* raw = reinterpret_cast<u8*>(&osd);
        for (int i = 0; i < 19; i++)
        {
            raw[kPresetOffsets[i]] = static_cast<u8>((mask >> i) & 1u);
        }
        s_techTrackerOsd = static_cast<u8>((mask >> 19) & 1u);
    }

    static bool validAreaTable(const u8* file, u32 size, u32 tableOff, u16 count)
    {
        if (tableOff > size || count > storage::kMaxAreas || tableOff + count * sizeof(AreaEntry) > size)
        {
            return false;
        }
        const AreaEntry* entries = reinterpret_cast<const AreaEntry*>(file + tableOff);
        for (u16 i = 0; i < count; i++)
        {
            if (entries[i].offset > size || entries[i].used > size - entries[i].offset)
            {
                return false;
            }
        }
        return true;
    }

    bool patchOsdAreaInFile(const u8* inFile, u32 inSize, const OsdSettings& osd, u8* outFile, u32 outCap,
                            u32 nextSeq, u32& outSize)
    {
        outSize = 0;
        if (outFile == NULL || outCap < sizeof(FileHeader) + sizeof(SettingsHead))
        {
            return false;
        }
        u32 tableOff = sizeof(FileHeader) + sizeof(SettingsHead);
        memset(outFile, 0, outCap);
        FileHeader* oh = reinterpret_cast<FileHeader*>(outFile);
        SettingsHead* ohead = reinterpret_cast<SettingsHead*>(outFile + sizeof(FileHeader));

        if (inFile == NULL || inSize == 0)
        {
            u32 room = (kOsdUsed + 3u) & ~3u;
            u32 total = tableOff + sizeof(AreaEntry) + room;
            if (total > outCap)
            {
                return false;
            }
            AreaEntry* e = reinterpret_cast<AreaEntry*>(outFile + tableOff);
            e->id = storage::kAreaOsd;
            e->generation = kOsdGen;
            e->used = kOsdUsed;
            e->offset = tableOff + sizeof(AreaEntry);
            memcpy(outFile + e->offset, &osd, kOsdUsed);
            ohead->areaCount = 1;
            ohead->layout = main::kSettingsVersion;
            oh->magic = storage::kSettingsMagic;
            oh->format = storage::kSettingsFormat;
            oh->headerSize = sizeof(FileHeader);
            oh->size = total;
            oh->sequence = nextSeq;
            oh->checksum = storage::checksum(outFile + storage::kChecksumFrom, total - storage::kChecksumFrom);
            outSize = total;
            return true;
        }

        if (inSize < tableOff)
        {
            return false;
        }
        const SettingsHead* ihead = reinterpret_cast<const SettingsHead*>(inFile + sizeof(FileHeader));
        if (!validAreaTable(inFile, inSize, tableOff, ihead->areaCount))
        {
            return false;
        }
        const AreaEntry* inEntries = reinterpret_cast<const AreaEntry*>(inFile + tableOff);
        bool hadOsd = false;
        for (u16 i = 0; i < ihead->areaCount; i++)
        {
            if (inEntries[i].id == storage::kAreaOsd)
            {
                hadOsd = true;
                break;
            }
        }
        u16 outCount = static_cast<u16>(ihead->areaCount + (hadOsd ? 0u : 1u));
        if (outCount > storage::kMaxAreas)
        {
            return false;
        }
        u32 at = tableOff + outCount * sizeof(AreaEntry);
        if (at > outCap)
        {
            return false;
        }
        AreaEntry* outEntries = reinterpret_cast<AreaEntry*>(outFile + tableOff);
        u16 n = 0;
        for (u16 i = 0; i < ihead->areaCount; i++)
        {
            const AreaEntry& src = inEntries[i];
            if (src.id == storage::kAreaOsd)
            {
                u32 room = (kOsdUsed + 3u) & ~3u;
                if (at + room > outCap)
                {
                    return false;
                }
                outEntries[n].id = storage::kAreaOsd;
                outEntries[n].generation = kOsdGen;
                outEntries[n].used = kOsdUsed;
                outEntries[n].offset = at;
                memcpy(outFile + at, &osd, kOsdUsed);
                at += room;
                n++;
            }
            else
            {
                // Copy every other area verbatim: zero clobber of the other 15 areas.
                u32 room = (static_cast<u32>(src.used) + 3u) & ~3u;
                if (at + room > outCap)
                {
                    return false;
                }
                outEntries[n].id = src.id;
                outEntries[n].generation = src.generation;
                outEntries[n].used = src.used;
                outEntries[n].offset = at;
                if (src.used > 0)
                {
                    memcpy(outFile + at, inFile + src.offset, src.used);
                }
                at += room;
                n++;
            }
        }
        if (!hadOsd)
        {
            u32 room = (kOsdUsed + 3u) & ~3u;
            if (at + room > outCap)
            {
                return false;
            }
            outEntries[n].id = storage::kAreaOsd;
            outEntries[n].generation = kOsdGen;
            outEntries[n].used = kOsdUsed;
            outEntries[n].offset = at;
            memcpy(outFile + at, &osd, kOsdUsed);
            at += room;
            n++;
        }
        ohead->areaCount = n;
        ohead->layout = main::kSettingsVersion;
        oh->magic = storage::kSettingsMagic;
        oh->format = storage::kSettingsFormat;
        oh->headerSize = sizeof(FileHeader);
        oh->size = at;
        oh->sequence = nextSeq;
        oh->checksum = storage::checksum(outFile + storage::kChecksumFrom, at - storage::kChecksumFrom);
        outSize = at;
        return true;
    }

    static bool patchOsdAndCpuInFile(const u8* inFile, u32 inSize, const OsdSettings& osd, s8 techTrackerOsd,
                                     u8* outFile, u32 outCap, u32 nextSeq, u32& outSize)
    {
        if (!patchOsdAreaInFile(inFile, inSize, osd, outFile, outCap, nextSeq, outSize))
        {
            return false;
        }
        if (techTrackerOsd >= 0 && outSize >= sizeof(FileHeader) + sizeof(SettingsHead))
        {
            SettingsHead* ohead = reinterpret_cast<SettingsHead*>(outFile + sizeof(FileHeader));
            u32 tableOff = sizeof(FileHeader) + sizeof(SettingsHead);
            AreaEntry* entries = reinterpret_cast<AreaEntry*>(outFile + tableOff);
            for (u16 i = 0; i < ohead->areaCount; i++)
            {
                if (entries[i].id == storage::kAreaCpu && entries[i].generation == 1
                    && entries[i].used > kCpuTechOsdOff)
                {
                    outFile[entries[i].offset + kCpuTechOsdOff] = static_cast<u8>(techTrackerOsd ? 1u : 0u);
                    FileHeader* oh = reinterpret_cast<FileHeader*>(outFile);
                    oh->checksum = storage::checksum(outFile + storage::kChecksumFrom, outSize - storage::kChecksumFrom);
                    break;
                }
            }
        }
        return true;
    }

    // ---- Runtime state (menu module) ----

    enum Phase { kIdle, kFolder, kWrite };
    struct Repeat {
        u16 held;
        u16 next;
    };

    static storage::Job s_job;
    static u8 s_cardBuf[storage::kSettingsFileCap] __attribute__((aligned(32)));
    // Heap-allocated in Heaps::Network so module unload after a kBusy timeout
    // never frees active SDIO DMA memory.
    static u8* s_writeBuf = NULL;
    static u32 s_cardSize = 0;
    static u32 s_cardSeq = 0;
    static u8 s_cardSlot = 1;
    static bool s_hasCard = false;
    static bool s_newerOnCard = false;
    static bool s_olderLayoutOnCard = false;

    static int s_phase = kIdle;
    static bool s_jobStale = false;
    static u8 s_pendingSlot = 0;
    static u32 s_pendingSeq = 0;
    static u32 s_pendingSize = 0;

    static bool s_open = false;
    static bool s_dirty = false;
    static u32 s_type = gfPadType::GCC;
    static int s_cursor = 0;
    static int s_scroll = 0;
    static Repeat s_rep[4];
    static OsdSettings s_osd;
    static const char* s_statusNote = NULL;

    static Settings* liveSettings()
    {
        u8* block = services().settings();
        if (block == NULL)
        {
            return NULL;
        }
        Settings* s = reinterpret_cast<Settings*>(block + block::kSettings);
        if (s->magic == main::kSettingsMagic && s->version == main::kSettingsVersion && s->size == sizeof(Settings))
        {
            return s;
        }
        return NULL;
    }

    static int readCardSlot(int slot, u32& seq, u32& fileSize, u8* dst)
    {
        int status = 0;
        seq = 0;
        fileSize = 0;
        if (s_jobStale || s_job.busy())
        {
            return storage::kMissing;
        }
        u8* buf = storage::allocReadBuffer(storage::kSettingsFileCap);
        if (buf == NULL)
        {
            return storage::kNoMemory;
        }
        if (!s_job.read(kNames[slot], buf, sizeof(FileHeader)))
        {
            storage::freeReadBuffer(buf, storage::kOk, 0);
            return storage::kMissing;
        }
        int r = s_job.wait(kWaitMs, status);
        if (r != storage::kOk)
        {
            if (r == storage::kBusy)
            {
                s_jobStale = true;
            }
            storage::freeReadBuffer(buf, r, status);
            return r == storage::kBusy ? storage::kMissing : r;
        }
        const FileHeader* h = reinterpret_cast<const FileHeader*>(buf);
        r = storage::checkHeader(buf, sizeof(FileHeader), storage::kSettingsMagic, storage::kSettingsFormat, false);
        if (r != storage::kOk && r != storage::kNewer)
        {
            storage::freeReadBuffer(buf, storage::kOk, 0);
            return r;
        }
        u32 minSz = r == storage::kNewer ? sizeof(FileHeader) : (sizeof(FileHeader) + sizeof(SettingsHead));
        u32 sz = h->size;
        if (sz > storage::kSettingsFileCap || sz < minSz)
        {
            storage::freeReadBuffer(buf, storage::kOk, 0);
            return storage::kCorrupt;
        }
        if (!s_job.read(kNames[slot], buf, sz))
        {
            storage::freeReadBuffer(buf, storage::kOk, 0);
            return storage::kCorrupt;
        }
        r = s_job.wait(kWaitMs, status);
        if (r != storage::kOk)
        {
            if (r == storage::kBusy)
            {
                s_jobStale = true;
            }
            storage::freeReadBuffer(buf, r, status);
            return (r == storage::kMissing || r == storage::kBusy) ? storage::kCorrupt : r;
        }
        r = storage::checkHeader(buf, sz, storage::kSettingsMagic, storage::kSettingsFormat, true);
        if (r == storage::kNewer)
        {
            seq = reinterpret_cast<const FileHeader*>(buf)->sequence;
        }
        else if (r == storage::kOk)
        {
            const SettingsHead* head = reinterpret_cast<const SettingsHead*>(buf + sizeof(FileHeader));
            if (!validAreaTable(buf, sz, sizeof(FileHeader) + sizeof(SettingsHead), head->areaCount))
            {
                r = storage::kCorrupt;
            }
            else
            {
                seq = reinterpret_cast<const FileHeader*>(buf)->sequence;
                fileSize = sz;
                memcpy(dst, buf, sz);
            }
        }
        storage::freeReadBuffer(buf, storage::kOk, 0);
        return r;
    }

    static void finishPendingWrite()
    {
        if (s_phase == kIdle)
        {
            storage::freeWriteBuffer(s_writeBuf, storage::kOk);
            return;
        }
        int status = 0;
        int r = s_job.wait(kWaitMs, status);
        if (s_phase == kFolder && r != storage::kBusy && s_writeBuf != NULL)
        {
            s_phase = kWrite;
            r = s_job.write(kNames[s_pendingSlot], s_writeBuf, s_pendingSize) ? s_job.wait(kWaitMs, status)
                                                                              : storage::kBusy;
        }
        if (r == storage::kBusy)
        {
            s_jobStale = true;
        }
        if (r == storage::kOk && s_writeBuf != NULL)
        {
            s_cardSlot = s_pendingSlot;
            s_cardSeq = s_pendingSeq;
            s_cardSize = s_pendingSize;
            s_hasCard = true;
            memcpy(s_cardBuf, s_writeBuf, s_pendingSize);
            storage::State* st = storage::blockState(services().settings());
            if (st != NULL && st->magic == storage::kStateMagic)
            {
                // Keep alternating slot/sequence in sync; st->baseline is a full-settings
                // checksum owned by settings_file.cpp (recomputed on next match save).
                st->slot = s_cardSlot;
                st->sequence = s_cardSeq;
            }
            TM_LOG("osdquick: saved to %s (save %u, %u bytes)\n", kNames[s_cardSlot], s_cardSeq, s_cardSize);
        }
        storage::freeWriteBuffer(s_writeBuf, r);
        s_phase = kIdle;
    }

    static void loadFromCardAndLive()
    {
        finishPendingWrite();
        defaultOsdSettings(s_osd);
        s_techTrackerOsd = 0;
        s_hasCard = false;
        s_cardSize = 0;
        s_cardSeq = 0;
        s_cardSlot = 1;
        s_newerOnCard = false;
        s_olderLayoutOnCard = false;
        s_statusNote = NULL;

        u8 tmp[storage::kSettingsFileCap];
        int results[2];
        u32 seqs[2];
        u32 sizes[2];
        int best = -1;
        for (int slot = 0; slot < 2; slot++)
        {
            results[slot] = readCardSlot(slot, seqs[slot], sizes[slot], tmp);
            bool usable = results[slot] == storage::kOk || results[slot] == storage::kNewer;
            if (usable && (best < 0 || storage::sequenceNewer(seqs[slot], seqs[best])))
            {
                best = slot;
                if (results[slot] == storage::kOk)
                {
                    memcpy(s_cardBuf, tmp, sizes[slot]);
                    s_cardSize = sizes[slot];
                }
            }
        }
        bool onlyBDamaged = results[1] == storage::kCorrupt && results[0] != storage::kCorrupt;
        s_cardSlot = static_cast<u8>(best >= 0 ? best : (onlyBDamaged ? 0 : 1));
        s_cardSeq = best >= 0 ? seqs[best] : 0;

        if (best >= 0 && results[best] == storage::kNewer)
        {
            s_newerOnCard = true;
            s_statusNote = "Newer save on SD: read-only";
        }
        else if (best >= 0 && results[best] == storage::kOk)
        {
            const SettingsHead* head = reinterpret_cast<const SettingsHead*>(s_cardBuf + sizeof(FileHeader));
            if (head->layout > main::kSettingsVersion)
            {
                s_newerOnCard = true;
                s_statusNote = "Newer save on SD: read-only";
            }
            else if (head->layout < main::kSettingsVersion)
            {
                s_olderLayoutOnCard = true;
                s_statusNote = "v1 save on SD: upgrade in match";
            }
            else
            {
                s_hasCard = true;
                u32 tableOff = sizeof(FileHeader) + sizeof(SettingsHead);
                const AreaEntry* entries = reinterpret_cast<const AreaEntry*>(s_cardBuf + tableOff);
                for (u16 i = 0; i < head->areaCount; i++)
                {
                    if (entries[i].id == storage::kAreaOsd && entries[i].generation == kOsdGen)
                    {
                        u32 n = entries[i].used < kOsdUsed ? entries[i].used : kOsdUsed;
                        memcpy(&s_osd, s_cardBuf + entries[i].offset, n);
                    }
                    else if (entries[i].id == storage::kAreaCpu && entries[i].generation == 1
                             && entries[i].used > kCpuTechOsdOff)
                    {
                        s_techTrackerOsd = s_cardBuf[entries[i].offset + kCpuTechOsdOff] != 0 ? 1u : 0u;
                    }
                }
            }
        }

        // Live Settings block wins whenever a match has already initialized it.
        Settings* live = liveSettings();
        if (live != NULL)
        {
            memcpy(&s_osd, &live->osd, sizeof(OsdSettings));
            s_techTrackerOsd = live->cpu.options.techTrackerOsd != 0 ? 1u : 0u;
        }
    }

    static void flushChanges()
    {
        if (!s_dirty)
        {
            return;
        }
        s_dirty = false;

        // 1. Update live Settings block immediately if initialized.
        Settings* live = liveSettings();
        if (live != NULL)
        {
            memcpy(&live->osd, &s_osd, sizeof(OsdSettings));
            live->cpu.options.techTrackerOsd = s_techTrackerOsd;
        }

        // 2. Patch kAreaOsd in-place on SD (never clobbering v1 pending upgrade or newer save).
        if (s_newerOnCard || s_olderLayoutOnCard || s_phase != kIdle || s_jobStale || s_job.busy())
        {
            return;
        }
        int w = storage::writable();
        if (w != storage::kOk)
        {
            TM_LOG("osdquick: not saved: %s\n", storage::resultText(w));
            return;
        }
        u32 nextSeq = storage::nextSequence(s_cardSeq);
        u32 outSz = 0;
        storage::freeWriteBuffer(s_writeBuf, storage::kOk);
        s_writeBuf = storage::allocWriteBuffer(storage::kSettingsFileCap);
        if (s_writeBuf == NULL)
        {
            TM_LOG("osdquick: not saved: %s\n", storage::resultText(storage::kNoMemory));
            return;
        }
        if (!patchOsdAndCpuInFile(s_hasCard ? s_cardBuf : NULL, s_hasCard ? s_cardSize : 0, s_osd,
                                  static_cast<s8>(s_techTrackerOsd), s_writeBuf, storage::kSettingsFileCap, nextSeq,
                                  outSz))
        {
            storage::freeWriteBuffer(s_writeBuf, storage::kOk);
            TM_LOG("osdquick: patch failed\n");
            return;
        }
        s_pendingSlot = static_cast<u8>(s_cardSlot ^ 1);
        s_pendingSeq = nextSeq;
        s_pendingSize = outSz;
        s_phase = kFolder;
        if (!s_job.makeFolder())
        {
            storage::freeWriteBuffer(s_writeBuf, storage::kWriteFailed);
            s_phase = kIdle;
        }
    }

    static int repeat(Repeat& r, bool down)
    {
        if (!down)
        {
            r.held = 0;
            return 0;
        }
        r.held++;
        if (r.held == 1)
        {
            r.next = kRepeatStart + 1;
            return 1;
        }
        if (r.held < r.next)
        {
            return 0;
        }
        r.next = r.held + kRepeatEvery;
        return 1;
    }

    static void keepVisible()
    {
        if (s_cursor < s_scroll)
        {
            s_scroll = s_cursor;
        }
        if (s_cursor >= s_scroll + kVisibleRows)
        {
            s_scroll = s_cursor - kVisibleRows + 1;
        }
    }

    static bool rowEnabled(int idx)
    {
        if (kRows[idx].offset == offsetof(OsdSettings, advantageTolerance))
        {
            return s_osd.frameAdvantage != 0 || s_osd.frameAdvantageOnHit != 0;
        }
        return true;
    }

    static u8 readRowValue(const QuickRow& r)
    {
        return r.offset == kRowOffTechTracker ? s_techTrackerOsd : reinterpret_cast<const u8*>(&s_osd)[r.offset];
    }

    static void writeRowValue(const QuickRow& r, u8 v)
    {
        if (r.offset == kRowOffTechTracker)
        {
            s_techTrackerOsd = v;
        }
        else
        {
            reinterpret_cast<u8*>(&s_osd)[r.offset] = v;
        }
    }

    static bool quickRowHighlighted(const QuickRow& r, u8 v)
    {
        if (r.kind == kKindToggle || r.offset == offsetof(OsdSettings, timingMeter)
            || r.offset == offsetof(OsdSettings, techSparkle))
        {
            return v != 0;
        }
        if (r.offset == offsetof(OsdSettings, preset))
        {
            return v != main::kOsdPresetEssentials;
        }
        if (r.offset == offsetof(OsdSettings, position))
        {
            return v != osd::kPositionSides;
        }
        if (r.offset == offsetof(OsdSettings, maxMessages))
        {
            return v != 4;
        }
        if (r.offset == offsetof(OsdSettings, advantageTolerance))
        {
            return v != 1;
        }
        return v != 0;
    }

    static void adjustRow(int idx, int delta)
    {
        if (s_newerOnCard || s_olderLayoutOnCard || !rowEnabled(idx))
        {
            sound(game::kSoundError);
            return;
        }
        const QuickRow& r = kRows[idx];
        int cur = readRowValue(r);
        int next = cur;
        if (r.kind == kKindToggle)
        {
            next = cur ? 0 : 1;
        }
        else
        {
            next = cur + delta;
            if (next < r.minVal)
            {
                next = r.minVal;
            }
            if (next > r.maxVal)
            {
                next = r.maxVal;
            }
        }
        if (next == cur)
        {
            return;
        }
        writeRowValue(r, static_cast<u8>(next));
        if (r.offset == offsetof(OsdSettings, preset))
        {
            applyPreset(s_osd, static_cast<u8>(next));
        }
        else if (r.kind == kKindToggle)
        {
            s_osd.preset = main::kOsdPresetCustom;
        }
        // Apply immediately to live Settings block too.
        Settings* live = liveSettings();
        if (live != NULL)
        {
            memcpy(&live->osd, &s_osd, sizeof(OsdSettings));
            live->cpu.options.techTrackerOsd = s_techTrackerOsd;
        }
        s_dirty = true;
        sound(game::kSoundMove);
    }

    void attach()
    {
        s_open = false;
        s_dirty = false;
        s_phase = kIdle;
        s_jobStale = s_job.busy();
        s_cursor = 0;
        s_scroll = 0;
        memset(s_rep, 0, sizeof(s_rep));
        s_statusNote = NULL;
    }

    void detach()
    {
        flushChanges();
        finishPendingWrite();
        s_open = false;
    }

    void open(int port, u32 controllerType)
    {
        loadFromCardAndLive();
        s_open = true;
        s_dirty = false;
        s_type = controllerType;
        s_cursor = 0;
        s_scroll = 0;
        memset(s_rep, 0, sizeof(s_rep));
        gfx::forgetWidths();
        TM_LOG("osdquick: opened for port %d\n", port + 1);
    }

    bool isOpen()
    {
        return s_open;
    }

    void setControllerType(u32 controllerType)
    {
        if (s_type != controllerType)
        {
            s_type = controllerType;
            gfx::forgetWidths();
        }
    }

    void step(int, const Input& in)
    {
        if (!s_open)
        {
            return;
        }
        s_type = in.type;
        const game::PadMap& map = game::padMap(in.type);
        bool up = (in.held & map.up) || in.stickY > kStickDeadZone;
        bool down = (in.held & map.down) || in.stickY < -kStickDeadZone;
        bool left = (in.held & game::kPadDLeft) || in.stickX < -kStickDeadZone;
        bool right = (in.held & game::kPadDRight) || in.stickX > kStickDeadZone;
        if (repeat(s_rep[0], up && !down) && s_cursor > 0)
        {
            s_cursor--;
            keepVisible();
            sound(game::kSoundMove);
        }
        if (repeat(s_rep[1], down && !up) && s_cursor + 1 < kRowCount)
        {
            s_cursor++;
            keepVisible();
            sound(game::kSoundMove);
        }
        if (repeat(s_rep[2], left && !right))
        {
            adjustRow(s_cursor, -1);
        }
        if (repeat(s_rep[3], right && !left))
        {
            adjustRow(s_cursor, 1);
        }
        if ((in.pressed & map.confirm) != 0)
        {
            if (kRows[s_cursor].kind == kKindToggle)
            {
                adjustRow(s_cursor, 1);
            }
            else
            {
                int cur = readRowValue(kRows[s_cursor]);
                int next = cur >= kRows[s_cursor].maxVal ? kRows[s_cursor].minVal : cur + 1;
                adjustRow(s_cursor, next - cur);
            }
            return;
        }
        if ((in.pressed & map.back) != 0)
        {
            flushChanges();
            s_open = false;
            sound(game::kSoundMove); // internal sub-screen return to Boot Menu
            TM_LOG("osdquick: closed\n");
        }
    }

    void update()
    {
        int result = storage::kOk;
        int status = 0;
        if (s_jobStale)
        {
            if (s_job.poll(result, status))
            {
                s_jobStale = false;
            }
            return;
        }
        if (s_phase == kIdle || !s_job.poll(result, status))
        {
            return;
        }
        if (s_phase == kFolder)
        {
            s_phase = kWrite;
            if (s_writeBuf == NULL || !s_job.write(kNames[s_pendingSlot], s_writeBuf, s_pendingSize))
            {
                storage::freeWriteBuffer(s_writeBuf, storage::kWriteFailed);
                s_phase = kIdle;
            }
            return;
        }
        s_phase = kIdle;
        if (result == storage::kOk && s_writeBuf != NULL)
        {
            s_cardSlot = s_pendingSlot;
            s_cardSeq = s_pendingSeq;
            s_cardSize = s_pendingSize;
            s_hasCard = true;
            memcpy(s_cardBuf, s_writeBuf, s_pendingSize);
            storage::State* st = storage::blockState(services().settings());
            if (st != NULL && st->magic == storage::kStateMagic)
            {
                // Keep alternating slot/sequence in sync; st->baseline is a full-settings
                // checksum owned by settings_file.cpp (recomputed on next match save).
                st->slot = s_cardSlot;
                st->sequence = s_cardSeq;
            }
            TM_LOG("osdquick: saved to %s (save %u, %u bytes)\n", kNames[s_cardSlot], s_cardSeq, s_cardSize);
        }
        else
        {
            TM_LOG("osdquick: save failed (status %d)\n", status);
        }
        storage::freeWriteBuffer(s_writeBuf, result);
    }

    void draw()
    {
        using namespace ui::style;
        float rowScale = scaleForCap(kLabRowCap);
        if (gfx::lineHeight(rowScale) <= 0.0f)
        {
            return;
        }

        // Smoked-charcoal v1 Direction A modal panel (5% CRT safe, >= 2px scanlines).
        static const ModalFrame kFrame = {
            56.0f, 584.0f, 34.0f, 446.0f, 71.0f, 379.0f, 407.0f, 326.0f, 0x070B12B8u
        };
        const float inL43 = kFrame.left43 + kLabBorder;
        const float inR43 = kFrame.right43 - kLabBorder;
        const float inTop = kFrame.topY + kLabBorder;
        const float inBot = kFrame.botY - kLabBorder;
        const float bodyTop = kFrame.hdrBot + kFibHair;
        const float descTop = kFrame.descTop;
        const float footTop = kFrame.footTop;
        drawModalFrame(kFrame, "QUICK OVERLAYS");

        float hdrMid = (inTop + kFrame.hdrBot) * 0.5f;
        const char* sub = s_statusNote != NULL ? s_statusNote : "Saves to SD on exit";
        float subScale = scaleForCap(kTypeMeta);
        float subW = gfx::textWidth(sub, subScale);
        float subMax = w(inR43 - kFrame.ribbonRight43 - kFibPad);
        float subDrawW = subW > subMax ? subMax : subW;
        drawFitText(cx(inR43 - kFibPad) - subDrawW, textYForCap(hdrMid - kTypeMeta * 0.5f, subScale), sub,
                    s_statusNote != NULL ? kLabFocusGold : kLabMutedText, subScale, subMax);

        // 9 visible slanted rows matching menu.cpp (Solo-Gold focused / dark-slate unselected).
        const float rowH = 25.0f;
        const float rowPitch = rowH + kFibTight; // 30 px (5 px = F5 gap)
        const float rowTop = bodyTop + 20.0f;    // centers 9 rows (265 px) inside 73..379
        const float hlLeft43 = inL43 + kFibGap;
        const float sbX43 = inR43 - kFibGap - kFibGap;
        const float hlRight43 = sbX43 - kFibTight;
        const float nameX = cx(hlLeft43 + 26.0f);
        const float nameMaxW = w(257.0f);
        const float pillLeft43 = 384.0f;
        const float pillRight43 = hlRight43;
        const float pillCenter = (cx(pillLeft43) + cx(pillRight43)) * 0.5f;
        const float valMaxW = w(132.0f);
        float capOff = (rowH - kLabRowCap) * 0.5f;

        for (int r = 0; r < kVisibleRows && s_scroll + r < kRowCount; r++)
        {
            int idx = s_scroll + r;
            const QuickRow& row = kRows[idx];
            float y = rowTop + r * rowPitch;
            bool sel = idx == s_cursor;
            bool on = rowEnabled(idx);
            if (sel)
            {
                drawGoldBar(hlLeft43, hlRight43, y, rowH, 8.5f, false, pillLeft43 + 4.5f);
            }
            else
            {
                drawSlantBar(hlLeft43, hlRight43, y, rowH, false, kLabRowSlateTop, kLabRowSlateBot, kLabRowSlateEdge);
            }

            u8 v = readRowValue(row);
            bool isHi = quickRowHighlighted(row, v);
            drawValuePill(pillLeft43, pillRight43, y, rowH, isHi, on, sel);

            if (sel && on)
            {
                drawValueArrows(pillLeft43, pillRight43, y, rowH, isHi ? kLabFocusGold : kLabGoldInk);
            }

            float ty = textYForCap(y + capOff, rowScale);
            gfx::Rgba nameCol = sel ? (on ? kLabGoldInk : kLabGoldInkMuted) : (on ? kText : kTextDisabled);
            drawFitText(nameX, ty, row.name, nameCol, rowScale, nameMaxW);

            char buf[24];
            const char* valTxt = buf;
            if (row.kind == kKindToggle)
            {
                valTxt = v != 0 ? "On" : "Off";
            }
            else if (row.kind == kKindInt)
            {
                snprintf(buf, sizeof(buf), row.offset == offsetof(OsdSettings, advantageTolerance) ? "%uf" : "%u",
                         static_cast<unsigned>(v));
            }
            else
            {
                valTxt = (v <= row.maxVal && row.labels != NULL) ? row.labels[v] : "?";
            }
            gfx::Rgba valCol = on ? ((sel && !isHi) ? kLabGoldInk : kLabWhiteText) : kTextDisabled;
            drawFitText(pillCenter, ty, valTxt, valCol, rowScale, valMaxW, true);
        }

        // Scrollbar.
        drawScrollBarAt(sbX43, rowTop, (kVisibleRows - 1) * rowPitch + rowH, kRowCount, s_scroll, kVisibleRows,
                        kLabDivider, kLabTealTop, kLabTealBot);

        // Per-row 1-line help description bar above the footer.
        const char* help = (s_cursor >= 0 && s_cursor < kRowCount && kRows[s_cursor].help != NULL)
                               ? kRows[s_cursor].help
                               : "Use D-Pad or Stick to toggle or cycle overlays.";
        float hScale = scaleForCap(kTypeMeta);
        float hy = textYForCap(descTop + kFibHair + (footTop - descTop - kFibHair - kTypeMeta) * 0.5f, hScale);
        drawFitText(cx(inL43 + kFibPad), hy, help, kLabDescText, hScale, w(inR43 - inL43 - 2.0f * kFibPad));

        // Glossy Brawl footer badges.
        const char* okGlyph = s_type == gfPadType::WIIMOTE ? "2" : "A";
        const char* backGlyph = s_type == gfPadType::WIIMOTE ? "1" : "B";
        float fy = footTop + kFibHair + (inBot - (footTop + kFibHair)) * 0.5f;
        float fx = cx(inL43 + kFibPad);
        u8 flags = kBadgeRound | kBadgeTightGap;
        fx = drawFooterBadge(fx, fy, 17.0f, okGlyph, "Toggle / Cycle", kBtnA, flags);
        const char* backLabel = (s_newerOnCard || s_olderLayoutOnCard) ? "Back" : "Back & Save";
        drawFooterBadge(fx, fy, 17.0f, backGlyph, backLabel, kBtnB, flags);
    }
}
}
}
