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
        kOsdGen = 1,
        kWaitMs = 3000,
        kStickDeadZone = 40,
        kVisibleRows = 10,
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
        u8 offset; // offsetof(OsdSettings, field)
        u8 minVal;
        u8 maxVal;
        const char* const* labels;
    };

#define TM_QOFF(f) static_cast<u8>(offsetof(OsdSettings, f))
    static const QuickRow kRows[] = {
        {"OSD Preset",          kKindChoice, TM_QOFF(preset),              0, main::kOsdPresetCount - 1,   kPresets},
        {"OSD Position",        kKindChoice, TM_QOFF(position),            0, osd::kPositionCount - 1,     kPositions},
        {"Max OSDs",            kKindInt,    TM_QOFF(maxMessages),         1, osd::kPerPlayer,             NULL},
        {"Overhead Timing Bar", kKindChoice, TM_QOFF(timingMeter),         0, main::kTimingMeterCount - 1, kTiming},
        {"Tech Sparkle",        kKindChoice, TM_QOFF(techSparkle),         0, main::kTechSparkleCount - 1, kSparkle},
        {"Wavedash",            kKindToggle, TM_QOFF(wavedash),            0, 1,                           NULL},
        {"L-Cancel",            kKindToggle, TM_QOFF(lCancel),             0, 1,                           NULL},
        {"Ledgedash Info",      kKindToggle, TM_QOFF(ledgedashInfo),       0, 1,                           NULL},
        {"Act OoS Frame",       kKindToggle, TM_QOFF(actOos),              0, 1,                           NULL},
        {"Shield Advantage",    kKindToggle, TM_QOFF(frameAdvantage),      0, 1,                           NULL},
        {"Hit Advantage",       kKindToggle, TM_QOFF(frameAdvantageOnHit), 0, 1,                           NULL},
        {"Dashback",            kKindToggle, TM_QOFF(dashback),            0, 1,                           NULL},
        {"Powershield",         kKindToggle, TM_QOFF(powershield),         0, 1,                           NULL},
        {"Act OoLag",           kKindToggle, TM_QOFF(actOoLag),            0, 1,                           NULL},
        {"Act OoAirborne",      kKindToggle, TM_QOFF(actOoAirborne),       0, 1,                           NULL},
        {"Act OoHitstun",       kKindToggle, TM_QOFF(actOoHitstun),        0, 1,                           NULL},
        {"Jump Cancel",         kKindToggle, TM_QOFF(jumpCancel),          0, 1,                           NULL},
        {"Fastfall Timing",     kKindToggle, TM_QOFF(fastfall),            0, 1,                           NULL},
        {"Boost Grab / DACUS",  kKindToggle, TM_QOFF(boostGrab),           0, 1,                           NULL},
        {"SDI Inputs",          kKindToggle, TM_QOFF(sdi),                 0, 1,                           NULL},
        {"Tech Lockout",        kKindToggle, TM_QOFF(lockoutTimers),       0, 1,                           NULL},
        {"Combo Counter",       kKindToggle, TM_QOFF(comboCounter),        0, 1,                           NULL},
        {"Grab Escape",         kKindToggle, TM_QOFF(grabBreakout),        0, 1,                           NULL},
        {"Item Throw",          kKindToggle, TM_QOFF(itemThrow),           0, 1,                           NULL}
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
        0x03B47u, // Movement
        0x2C498u, // Defense
        0x00000u  // All Off
    };

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

    // ---- Runtime state (menu module) ----

    enum Phase { kIdle, kFolder, kWrite };
    struct Repeat {
        u16 held;
        u16 next;
    };

    static storage::Job s_job;
    static u8 s_cardBuf[storage::kSettingsFileCap] __attribute__((aligned(32)));
    static u8 s_writeBuf[storage::kSettingsFileCap] __attribute__((aligned(32)));
    static u32 s_cardSize = 0;
    static u32 s_cardSeq = 0;
    static u8 s_cardSlot = 1;
    static bool s_hasCard = false;
    static bool s_newerOnCard = false;
    static bool s_olderLayoutOnCard = false;

    static int s_phase = kIdle;
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
            storage::freeReadBuffer(buf, r, status);
            return r == storage::kBusy ? storage::kMissing : r;
        }
        const FileHeader* h = reinterpret_cast<const FileHeader*>(buf);
        r = storage::checkHeader(buf, sizeof(FileHeader), storage::kSettingsMagic, storage::kSettingsFormat, false);
        if (r != storage::kOk)
        {
            if (r == storage::kNewer)
            {
                seq = h->sequence;
            }
            storage::freeReadBuffer(buf, storage::kOk, 0);
            return r;
        }
        u32 sz = h->size;
        if (sz > storage::kSettingsFileCap || sz < sizeof(FileHeader) + sizeof(SettingsHead))
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
            storage::freeReadBuffer(buf, r, status);
            return r == storage::kMissing ? storage::kCorrupt : r;
        }
        r = storage::checkHeader(buf, sz, storage::kSettingsMagic, storage::kSettingsFormat, true);
        if (r == storage::kOk)
        {
            const SettingsHead* head = reinterpret_cast<const SettingsHead*>(buf + sizeof(FileHeader));
            if (!validAreaTable(buf, sz, sizeof(FileHeader) + sizeof(SettingsHead), head->areaCount))
            {
                r = storage::kCorrupt;
            }
            else
            {
                seq = h->sequence;
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
            return;
        }
        int status = 0;
        int r = s_job.wait(kWaitMs, status);
        if (s_phase == kFolder && r != storage::kBusy)
        {
            s_phase = kWrite;
            r = s_job.write(kNames[s_pendingSlot], s_writeBuf, s_pendingSize) ? s_job.wait(kWaitMs, status)
                                                                              : storage::kBusy;
        }
        if (r == storage::kOk)
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
        s_phase = kIdle;
    }

    static void loadFromCardAndLive()
    {
        finishPendingWrite();
        defaultOsdSettings(s_osd);
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
                        break;
                    }
                }
            }
        }

        // Live Settings block wins whenever a match has already initialized it.
        Settings* live = liveSettings();
        if (live != NULL)
        {
            memcpy(&s_osd, &live->osd, sizeof(OsdSettings));
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
        }

        // 2. Patch kAreaOsd in-place on SD (never clobbering v1 pending upgrade or newer save).
        if (s_newerOnCard || s_olderLayoutOnCard)
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
        if (!patchOsdAreaInFile(s_hasCard ? s_cardBuf : NULL, s_hasCard ? s_cardSize : 0, s_osd, s_writeBuf,
                                sizeof(s_writeBuf), nextSeq, outSz))
        {
            TM_LOG("osdquick: patch failed\n");
            return;
        }
        s_pendingSlot = static_cast<u8>(s_cardSlot ^ 1);
        s_pendingSeq = nextSeq;
        s_pendingSize = outSz;
        s_phase = kFolder;
        if (!s_job.makeFolder())
        {
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

    static void adjustRow(int idx, int delta)
    {
        if (s_newerOnCard || s_olderLayoutOnCard)
        {
            sound(game::kSoundError);
            return;
        }
        const QuickRow& r = kRows[idx];
        u8* raw = reinterpret_cast<u8*>(&s_osd);
        int cur = raw[r.offset];
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
        raw[r.offset] = static_cast<u8>(next);
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
        }
        s_dirty = true;
        sound(game::kSoundMove);
    }

    void attach()
    {
        s_open = false;
        s_dirty = false;
        s_phase = kIdle;
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
                int cur = reinterpret_cast<const u8*>(&s_osd)[kRows[s_cursor].offset];
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
        if (s_phase == kIdle || !s_job.poll(result, status))
        {
            return;
        }
        if (s_phase == kFolder)
        {
            s_phase = kWrite;
            if (!s_job.write(kNames[s_pendingSlot], s_writeBuf, s_pendingSize))
            {
                s_phase = kIdle;
            }
            return;
        }
        s_phase = kIdle;
        if (result == storage::kOk)
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
    }

    void draw()
    {
        using namespace ui::style;
        float rowScale = scaleForCap(kLabRowCap);
        if (gfx::lineHeight(rowScale) <= 0.0f)
        {
            return;
        }

        // Dark backdrop + Brawl/P+ double-framed dark chrome panel (5% CRT safe).
        gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, 0x050C10B8u);
        const float left43 = 60.0f;
        const float right43 = 580.0f;
        const float topY = 36.0f;
        const float botY = 444.0f;
        float left = cx(left43);
        float right = cx(right43);
        float panelW = right - left;
        float bOut = w(2.0f);
        float bIn = w(2.0f);

        gfx::fillRect(left, topY, panelW, 2.0f, kLabBorderColor);
        gfx::fillRect(left, botY - 2.0f, panelW, 2.0f, kLabBorderColor);
        gfx::fillRect(left, topY + 2.0f, bOut, botY - topY - 4.0f, kLabBorderColor);
        gfx::fillRect(right - bOut, topY + 2.0f, bOut, botY - topY - 4.0f, kLabBorderColor);

        float il = left + bOut;
        float iw = panelW - 2.0f * bOut;
        gfx::fillRect(il, topY + 2.0f, iw, 2.0f, kLabAccent);
        gfx::fillRect(il, botY - 4.0f, iw, 2.0f, kLabAccent);
        gfx::fillRect(il, topY + 4.0f, bIn, botY - topY - 8.0f, kLabAccent);
        gfx::fillRect(il + iw - bIn, topY + 4.0f, bIn, botY - topY - 8.0f, kLabAccent);

        float bodyX = il + bIn;
        float bodyW = iw - 2.0f * bIn;
        gfx::fillGradV(bodyX, topY + 4.0f, bodyW, 44.0f, kLabHeaderTop, kLabHeaderBot);
        gfx::fillRect(bodyX, topY + 48.0f, bodyW, 2.0f, kLabDivider);
        gfx::fillGradV(bodyX, topY + 50.0f, bodyW, botY - 40.0f - (topY + 50.0f), kLabBaseTop, kLabBaseBot);
        gfx::fillRect(bodyX, botY - 40.0f, bodyW, 2.0f, kLabDivider);
        gfx::fillGradV(bodyX, botY - 38.0f, bodyW, 34.0f, kLabFooterTopCol, kLabFooterBotCol);

        // Header Brawl Display title + optional status tag.
        float titleScale = displayScaleForCap(13.5f);
        gfx::displayText(cx(left43 + 18.0f), displayYForCap(topY + 19.0f, titleScale), "QUICK OSDS", kLabTitleTop,
                         kLabTitleBot, titleScale, titleScale, 1.2f, kLabTitleEdge);
        const char* sub = s_statusNote != NULL ? s_statusNote : "Saves OSDs to SD (other settings kept)";
        float subScale = scaleForCap(8.5f);
        float subW = gfx::textWidth(sub, subScale);
        float subMax = w(310.0f);
        float subSx = subW > subMax && subW > 0.0f ? subScale * subMax / subW : subScale;
        float subDrawW = subW > subMax ? subMax : subW;
        gfx::text(cx(right43 - 18.0f) - subDrawW, textYForCap(topY + 22.0f, subScale), sub,
                  s_statusNote != NULL ? kLabFocusGold : kLabMutedText, subSx, subScale);

        // 10 visible rows with metallic gold cursor bar & sculpted value pills.
        const float rowTop = topY + 58.0f;
        const float rowPitch = 30.0f;
        const float rowH = 25.0f;
        const float hlLeft = cx(left43 + 14.0f);
        const float hlW = w(right43 - left43 - 40.0f);
        const float nameX = cx(left43 + 30.0f);
        const float nameMaxW = w(240.0f);
        const float pillLeft = cx(382.0f);
        const float pillW = w(160.0f);
        const float pillCenter = pillLeft + pillW * 0.5f;
        const float valMaxW = w(136.0f);
        float capOff = (rowH - kLabRowCap) * 0.5f;

        for (int r = 0; r < kVisibleRows && s_scroll + r < kRowCount; r++)
        {
            int idx = s_scroll + r;
            const QuickRow& row = kRows[idx];
            float y = rowTop + r * rowPitch;
            bool sel = idx == s_cursor;
            if (sel)
            {
                drawBevelBar(hlLeft, y, hlW, rowH, kLabGoldTop, kLabGoldBot, kLabGoldSheen, kLabGoldEdge);
                drawCursorTri(cx(left43 + 17.0f), y + rowH * 0.5f, 8.5f);
            }
            else
            {
                drawBevelBar(hlLeft, y, hlW, rowH, 0x11242EE0u, 0x0A161CE0u, 0x3A687A30u, kLabBevelDark);
            }

            u8 v = reinterpret_cast<const u8*>(&s_osd)[row.offset];
            bool isOn = row.kind == kKindToggle && v != 0;
            float pad = w(1.5f);
            gfx::fillRect(pillLeft, y + 1.0f, pillW, rowH - 2.0f, kLabPillBorder);
            gfx::fillGradV(pillLeft + pad, y + 3.0f, pillW - 2.0f * pad, rowH - 6.0f,
                           isOn ? kLabPillOnTop : kLabPillOffTop, isOn ? kLabPillOnBot : kLabPillOffBot);
            gfx::fillRect(pillLeft + pad, y + 3.0f, pillW - 2.0f * pad, 2.0f,
                          isOn ? kLabPillOnSheen : kLabPillOffSheen);

            float ty = textYForCap(y + capOff, rowScale);
            float nw = boldWidth(row.name, rowScale);
            float nsx = nw > nameMaxW && nw > 0.0f ? rowScale * nameMaxW / nw : rowScale;
            boldText(nameX, ty, row.name, sel ? kLabGoldInk : kText, nsx, rowScale);

            char buf[24];
            const char* valTxt = buf;
            if (row.kind == kKindToggle)
            {
                valTxt = v != 0 ? "On" : "Off";
            }
            else if (row.kind == kKindInt)
            {
                snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
            }
            else
            {
                valTxt = (v <= row.maxVal && row.labels != NULL) ? row.labels[v] : "?";
            }
            float vw = boldWidth(valTxt, rowScale);
            float vsx = vw > valMaxW && vw > 0.0f ? rowScale * valMaxW / vw : rowScale;
            float drawVw = vw > valMaxW ? valMaxW : vw;
            boldText(pillCenter - drawVw * 0.5f, ty, valTxt, kLabWhiteText, vsx, rowScale);
        }

        // Scrollbar.
        if (kRowCount > kVisibleRows)
        {
            float sbX = cx(right43 - 20.0f);
            float sbW = w(7.0f);
            float sbH = (kVisibleRows - 1) * rowPitch + rowH;
            gfx::fillRect(sbX, rowTop, sbW, sbH, kLabDivider);
            float inner = sbH - 4.0f;
            float thumbH = inner * kVisibleRows / kRowCount;
            if (thumbH < 18.0f)
            {
                thumbH = 18.0f;
            }
            int maxScroll = kRowCount - kVisibleRows;
            float thumbY = rowTop + 2.0f + (inner - thumbH) * s_scroll / maxScroll;
            gfx::fillGradV(sbX + w(1.0f), thumbY, sbW - w(2.0f), thumbH, kLabTealTop, kLabTealBot);
        }

        // Glossy Brawl footer badges.
        const char* okGlyph = s_type == gfPadType::WIIMOTE ? "2" : "A";
        const char* backGlyph = s_type == gfPadType::WIIMOTE ? "1" : "B";
        float fy = botY - 21.0f;
        float bCap = 9.0f;
        float bScale = scaleForCap(bCap);
        float lScale = scaleForCap(10.0f);
        float fx = cx(left43 + 18.0f);
        float bw = w(17.0f);

        drawGlossyRoundBadge(fx + bw * 0.5f, fy, 8.5f, kLabBtnATop, kLabBtnABot);
        gfx::text(fx + (bw - gfx::textWidth(okGlyph, bScale)) * 0.5f, textYForCap(fy - bCap * 0.5f, bScale), okGlyph,
                  kLabWhiteText, bScale);
        fx = gfx::text(fx + bw + w(6.0f), textYForCap(fy - 5.0f, lScale), "Toggle / Cycle", kText, lScale) + w(20.0f);

        drawGlossyRoundBadge(fx + bw * 0.5f, fy, 8.5f, kLabBtnBTop, kLabBtnBBot);
        gfx::text(fx + (bw - gfx::textWidth(backGlyph, bScale)) * 0.5f, textYForCap(fy - bCap * 0.5f, bScale),
                  backGlyph, kLabWhiteText, bScale);
        const char* backLabel = (s_newerOnCard || s_olderLayoutOnCard) ? "Back" : "Back & Save";
        gfx::text(fx + bw + w(6.0f), textYForCap(fy - 5.0f, lScale), backLabel, kText, lScale);
    }
}
}
}
