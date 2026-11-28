#include "menu/osd_quick.h"

#include <cstdio>
#include <gf/gf_pad_status.h>
#include <stddef.h>
#include <string.h>

#include "main/cpu/cpu_settings.h"
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
    using main::DisplaySettings;
    using main::GeneralSettings;
    using main::OsdPresetsSettings;
    using main::OsdSettings;
    using main::Settings;
    using storage::AreaEntry;
    using storage::FileHeader;
    using storage::SettingsHead;

    enum {
        kGeneralUsed = offsetof(GeneralSettings, reserved),
        kDisplayUsed = sizeof(DisplaySettings),
        kOsdUsed = offsetof(OsdSettings, reserved),
        kOsdPresetsUsed = offsetof(OsdPresetsSettings, reserved),
        kCpuTechOsdOff = offsetof(main::CpuSettings, options.techTrackerOsd),
        kAreaGen = 1,
        kWaitMs = 3000,
        kStickDeadZone = 40,
        kVisibleRows = 9,
        kRepeatStart = ui::style::kRepeatStart,
        kRepeatEvery = ui::style::kRepeatEvery
    };
    TM_STATIC_ASSERT(kGeneralUsed == 15, tm_osd_quick_general_used_bytes);
    TM_STATIC_ASSERT(kOsdUsed == 30, tm_osd_quick_used_bytes);
    TM_STATIC_ASSERT(kOsdPresetsUsed == 12, tm_osd_quick_presets_used_bytes);

    static const char* const kNames[2] = {"settings-a.bin", "settings-b.bin"};
    static const char* const kPresets[main::kOsdPresetCount] = {
        "Custom", "Essentials", "Movement", "Defense", "All Off", "Custom 1", "Custom 2", "Custom 3"
    };
    static const char* const kPositions[] = {"HUD", "Sides", "Top Left", "Top Right"};
    static const char* const kWho[] = {"Off", "P1", "CPU", "Both"};
    static const char* const kFrameMeter[] = {"Off", "P1 Top", "CPU Top"};
    static const char* const kTiming[] = {"Off", "All", "L-Cancel", "Wavedash", "Act-Out"};
    static const char* const kSparkle[] = {"Off", "Perfect Wavedash", "Shorthop", "Both"};

    enum RowKind { kKindChoice = 0, kKindInt = 1, kKindToggle = 2, kKindSub = 3 };
    enum DataSrc { kSrcOsd = 0, kSrcDisplay = 1, kSrcGeneral = 2, kSrcTechOsd = 3, kSrcNone = 4 };

    struct QuickRow {
        const char* name;
        u8 kind;
        u8 source;
        u8 offset;
        u8 minVal;
        u8 maxVal;
        const char* const* labels;
    };

#define TM_QOFF(f) static_cast<u8>(offsetof(OsdSettings, f))
#define TM_DOFF(f) static_cast<u8>(offsetof(DisplaySettings, f))
#define TM_GOFF(f) static_cast<u8>(offsetof(GeneralSettings, f))

    // Tight 7-row Quick Setup + All OSDs sub-view (zero scrolling on main open).
    static const QuickRow kMainRows[] = {
        {"OSD Preset",          kKindChoice, kSrcOsd,     TM_QOFF(preset),      0, main::kOsdPresetCount - 1,   kPresets},
        {"OSD Position",        kKindChoice, kSrcOsd,     TM_QOFF(position),    0, osd::kPositionCount - 1,     kPositions},
        {"Input Viewer",        kKindChoice, kSrcDisplay, TM_DOFF(inputViewer), 0, 3,                           kWho},
        {"Frame Meter",         kKindChoice, kSrcGeneral, TM_GOFF(actionLog),   0, main::kActionLogTargetCount - 1, kFrameMeter},
        {"Hitboxes",            kKindChoice, kSrcDisplay, TM_DOFF(hitboxes),    0, 3,                           kWho},
        {"Overhead Timing Bar", kKindChoice, kSrcOsd,     TM_QOFF(timingMeter), 0, main::kTimingMeterCount - 1, kTiming},
        {"Tech Sparkle",        kKindChoice, kSrcOsd,     TM_QOFF(techSparkle), 0, main::kTechSparkleCount - 1, kSparkle},
        {"All OSDs",            kKindSub,    kSrcNone,    0,                    0, 0,                           NULL}
    };

    static const QuickRow kSubRows[] = {
        {"P+ Tech OSD",         kKindToggle, kSrcTechOsd, 0,                              0, 1,              NULL},
        {"Wavedash",            kKindToggle, kSrcOsd,     TM_QOFF(wavedash),              0, 1,              NULL},
        {"L-Cancel",            kKindToggle, kSrcOsd,     TM_QOFF(lCancel),               0, 1,              NULL},
        {"Ledgedash Info",      kKindToggle, kSrcOsd,     TM_QOFF(ledgedashInfo),         0, 1,              NULL},
        {"Act OoS Frame",       kKindToggle, kSrcOsd,     TM_QOFF(actOos),                0, 1,              NULL},
        {"Shield Advantage",    kKindToggle, kSrcOsd,     TM_QOFF(frameAdvantage),        0, 1,              NULL},
        {"Hit Advantage",       kKindToggle, kSrcOsd,     TM_QOFF(frameAdvantageOnHit),   0, 1,              NULL},
        {"Advantage Tolerance", kKindInt,    kSrcOsd,     TM_QOFF(advantageTolerance),    0, 5,              NULL},
        {"Dashback",            kKindToggle, kSrcOsd,     TM_QOFF(dashback),              0, 1,              NULL},
        {"Powershield",         kKindToggle, kSrcOsd,     TM_QOFF(powershield),           0, 1,              NULL},
        {"Act OoLag",           kKindToggle, kSrcOsd,     TM_QOFF(actOoLag),              0, 1,              NULL},
        {"Act OoAirborne",      kKindToggle, kSrcOsd,     TM_QOFF(actOoAirborne),         0, 1,              NULL},
        {"Act OoHitstun",       kKindToggle, kSrcOsd,     TM_QOFF(actOoHitstun),          0, 1,              NULL},
        {"Jump Cancel",         kKindToggle, kSrcOsd,     TM_QOFF(jumpCancel),            0, 1,              NULL},
        {"Fastfall Timing",     kKindToggle, kSrcOsd,     TM_QOFF(fastfall),              0, 1,              NULL},
        {"Boost Grab / DACUS",  kKindToggle, kSrcOsd,     TM_QOFF(boostGrab),             0, 1,              NULL},
        {"SDI Inputs",          kKindToggle, kSrcOsd,     TM_QOFF(sdi),                   0, 1,              NULL},
        {"Tech Lockout",        kKindToggle, kSrcOsd,     TM_QOFF(lockoutTimers),         0, 1,              NULL},
        {"Combo Counter",       kKindToggle, kSrcOsd,     TM_QOFF(comboCounter),          0, 1,              NULL},
        {"Grab Escape",         kKindToggle, kSrcOsd,     TM_QOFF(grabBreakout),          0, 1,              NULL},
        {"Item Throw",          kKindToggle, kSrcOsd,     TM_QOFF(itemThrow),             0, 1,              NULL},
        {"Max OSDs",            kKindInt,    kSrcOsd,     TM_QOFF(maxMessages),           1, osd::kPerPlayer, NULL}
    };

    static const char* const kMainHelp[] = {
        "Apply Essentials, Movement, Defense, or Custom 1-3.",
        "Where OSD notifications appear during matches.",
        "Live GameCube stick and button overlays.",
        "SF6-style frame activity timeline strip.",
        "Draw attack, grab, shield, and hurtbox bubbles.",
        "Floating timing bar above fighters.",
        "Visual flash on Frame-1 Wavedash or Shorthop.",
        "Open all 20 individual OSDs, Max OSDs, and Tolerance."
    };
#undef TM_QOFF
#undef TM_DOFF
#undef TM_GOFF

    enum {
        kMainCount = static_cast<int>(sizeof(kMainRows) / sizeof(kMainRows[0])),
        kSubCount = static_cast<int>(sizeof(kSubRows) / sizeof(kSubRows[0]))
    };

    // Matches osd_menu.cpp::kToggleOffsets[20] (bits 0..18 in OsdSettings, bit 19 in techTrackerOsd).
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
    static const u32 kPresetMasks[4] = {
        0x0003Fu, // Essentials
        0x83B47u, // Movement (includes P+ Tech OSD bit 19)
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

    void defaultCpuSettings(main::CpuSettings& out)
    {
        memset(&out, 0, sizeof(out));
        out.options.control = cpu::kControlLab;
        out.options.counterGround = 13;
        out.options.counterAir = 1;
        out.options.counterShield = 1;
        out.options.infiniteShields = cpu::kShieldsUntilHit;
        out.options.shieldHealth = cpu::kShieldFull;
        for (int i = 0; i < 4; i++)
        {
            out.options.techChance[i] = 25;
            out.options.getupChance[i] = 25;
            out.options.revSlot2Weight[i] = 50;
        }
        out.options.advHit = 1;
        out.options.advGround = 13;
        out.options.advAir = 1;
        out.options.advShield = 1;
        out.options.recovery = cpu::kRecoveryPPlusAi;
        out.options.autoLCancel = 1;
        out.options.shieldTiming = cpu::kShieldTimingFullMove;
    }

    void defaultQuickSetupData(QuickSetupData& out)
    {
        memset(&out, 0, sizeof(out));
        out.general.moveStaling = 1;
        out.general.ledgeHud = 1;
        out.general.ledgeGalint = 1;
        out.general.ledgePanel = 1;
        out.general.ledgeMaxFrames = 50;
        out.general.playerInfiniteShields = cpu::kShieldsUntilHit;
        out.display.hud = 1;
        out.display.models = 3;
        out.display.stageModel = 1;
        out.display.effects = 1;
        out.display.heatmapOpacity = 60;
        out.display.heatmapColorFrames = 5;
        defaultOsdSettings(out.osd);
        out.techTrackerOsd = 0;
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

    struct AreaPatchSpec {
        u8 id;
        u8 generation;
        u16 used;
        const void* data;
    };

    static bool patchAreasInFile(const u8* inFile, u32 inSize, const AreaPatchSpec* patches, int patchCount,
                                 s8 techTrackerOsd, u8* outFile, u32 outCap, u32 nextSeq, u32& outSize)
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

        bool seen[16];
        memset(seen, 0, sizeof(seen));
        u16 inCount = 0;
        const AreaEntry* inEntries = NULL;
        if (inFile != NULL && inSize > 0)
        {
            if (inSize < tableOff)
            {
                return false;
            }
            const SettingsHead* ihead = reinterpret_cast<const SettingsHead*>(inFile + sizeof(FileHeader));
            if (!validAreaTable(inFile, inSize, tableOff, ihead->areaCount))
            {
                return false;
            }
            inCount = ihead->areaCount;
            inEntries = reinterpret_cast<const AreaEntry*>(inFile + tableOff);
            for (u16 i = 0; i < inCount; i++)
            {
                for (int p = 0; p < patchCount; p++)
                {
                    if (inEntries[i].id == patches[p].id)
                    {
                        seen[p] = true;
                    }
                }
            }
        }

        u16 extra = 0;
        for (int p = 0; p < patchCount; p++)
        {
            if (!seen[p]) extra++;
        }
        u16 outCount = static_cast<u16>(inCount + extra);
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
        for (u16 i = 0; i < inCount; i++)
        {
            const AreaEntry& src = inEntries[i];
            int match = -1;
            for (int p = 0; p < patchCount; p++)
            {
                if (src.id == patches[p].id)
                {
                    match = p;
                    break;
                }
            }
            if (match >= 0)
            {
                const AreaPatchSpec& ps = patches[match];
                u32 room = (static_cast<u32>(ps.used) + 3u) & ~3u;
                if (at + room > outCap) return false;
                outEntries[n].id = ps.id;
                outEntries[n].generation = ps.generation;
                outEntries[n].used = ps.used;
                outEntries[n].offset = at;
                memcpy(outFile + at, ps.data, ps.used);
                at += room;
                n++;
            }
            else
            {
                u32 room = (static_cast<u32>(src.used) + 3u) & ~3u;
                if (at + room > outCap) return false;
                outEntries[n].id = src.id;
                outEntries[n].generation = src.generation;
                outEntries[n].used = src.used;
                outEntries[n].offset = at;
                if (src.used > 0)
                {
                    memcpy(outFile + at, inFile + src.offset, src.used);
                }
                if (techTrackerOsd >= 0 && src.id == storage::kAreaCpu && src.generation == 1
                    && src.used > kCpuTechOsdOff)
                {
                    outFile[at + kCpuTechOsdOff] = static_cast<u8>(techTrackerOsd ? 1u : 0u);
                }
                at += room;
                n++;
            }
        }
        for (int p = 0; p < patchCount; p++)
        {
            if (seen[p]) continue;
            const AreaPatchSpec& ps = patches[p];
            u32 room = (static_cast<u32>(ps.used) + 3u) & ~3u;
            if (at + room > outCap) return false;
            outEntries[n].id = ps.id;
            outEntries[n].generation = ps.generation;
            outEntries[n].used = ps.used;
            outEntries[n].offset = at;
            memcpy(outFile + at, ps.data, ps.used);
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

    bool patchQuickSetupAreasInFile(const u8* inFile, u32 inSize, const QuickSetupData& data, u8* outFile,
                                    u32 outCap, u32 nextSeq, u32& outSize)
    {
        bool hasCpuArea = false;
        u32 tableOff = sizeof(FileHeader) + sizeof(SettingsHead);
        if (inFile != NULL && inSize >= tableOff)
        {
            const SettingsHead* ihead = reinterpret_cast<const SettingsHead*>(inFile + sizeof(FileHeader));
            if (validAreaTable(inFile, inSize, tableOff, ihead->areaCount))
            {
                const AreaEntry* inEntries = reinterpret_cast<const AreaEntry*>(inFile + tableOff);
                for (u16 i = 0; i < ihead->areaCount; i++)
                {
                    if (inEntries[i].id == storage::kAreaCpu)
                    {
                        hasCpuArea = true;
                        break;
                    }
                }
            }
        }
        main::CpuSettings cpuDef;
        AreaPatchSpec patches[4] = {
            {storage::kAreaGeneral, kAreaGen, kGeneralUsed, &data.general},
            {storage::kAreaDisplay, kAreaGen, kDisplayUsed, &data.display},
            {storage::kAreaOsd, kAreaGen, kOsdUsed, &data.osd},
            {storage::kAreaCpu, kAreaGen, static_cast<u16>(sizeof(main::CpuSettings)), &cpuDef}
        };
        int patchCount = 3;
        if (!hasCpuArea && data.techTrackerOsd != 0)
        {
            defaultCpuSettings(cpuDef);
            cpuDef.options.techTrackerOsd = 1;
            patchCount = 4;
        }
        return patchAreasInFile(inFile, inSize, patches, patchCount, data.techTrackerOsd, outFile, outCap, nextSeq,
                                outSize);
    }

    bool patchOsdAreaInFile(const u8* inFile, u32 inSize, const OsdSettings& osd, u8* outFile, u32 outCap,
                            u32 nextSeq, u32& outSize)
    {
        const AreaPatchSpec patch = {storage::kAreaOsd, kAreaGen, kOsdUsed, &osd};
        return patchAreasInFile(inFile, inSize, &patch, 1, -1, outFile, outCap, nextSeq, outSize);
    }

    // ---- Runtime state (menu module) ----

    enum Phase { kIdle, kFolder, kWrite };
    struct Repeat {
        u16 held;
        u16 next;
    };

    static storage::Job s_job;
    static u8 s_cardBuf[storage::kSettingsFileCap] __attribute__((aligned(32)));
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
    static bool s_subView = false;
    static bool s_dirty = false;
    static u32 s_type = gfPadType::GCC;
    static int s_cursor[2] = {0, 0};
    static int s_scroll[2] = {0, 0};
    static Repeat s_rep[4];
    static QuickSetupData s_data;
    static OsdPresetsSettings s_osdPresets;
    static const char* s_statusNote = NULL;

    // Strict settings block validation guard (shared/block_layout.h).
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

    static void applyPresetMask(u8 preset)
    {
        if (preset == main::kOsdPresetCustom || preset >= main::kOsdPresetCount)
        {
            return;
        }
        u32 mask = 0u;
        if (preset >= main::kOsdPresetCustom1)
        {
            u32 slot = static_cast<u32>(preset - main::kOsdPresetCustom1);
            if (slot >= main::kOsdCustomSlotCount) return;
            u32 saved = s_osdPresets.customMask[slot];
            if ((saved & main::kOsdCustomSavedBit) == 0u)
            {
                return;
            }
            mask = saved & 0xFFFFFu;
        }
        else
        {
            mask = kPresetMasks[preset - 1];
        }
        u8* raw = reinterpret_cast<u8*>(&s_data.osd);
        for (int i = 0; i < 19; i++)
        {
            raw[kPresetOffsets[i]] = static_cast<u8>((mask >> i) & 1u);
        }
        s_data.techTrackerOsd = static_cast<s8>((mask >> 19) & 1u);
    }

    static int readCardSlot(int slot, u32& seq, u32& fileSize, u8* dst)
    {
        int status = 0;
        seq = 0;
        fileSize = 0;
        if (s_jobStale || s_job.busy()) return storage::kMissing;
        u8* buf = storage::allocReadBuffer(storage::kSettingsFileCap);
        if (buf == NULL) return storage::kNoMemory;
        if (!s_job.read(kNames[slot], buf, sizeof(FileHeader)))
        {
            storage::freeReadBuffer(buf, storage::kOk, 0);
            return storage::kMissing;
        }
        int r = s_job.wait(kWaitMs, status);
        if (r != storage::kOk)
        {
            if (r == storage::kBusy) s_jobStale = true;
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
            if (r == storage::kBusy) s_jobStale = true;
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
        if (r == storage::kBusy) s_jobStale = true;
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
        defaultQuickSetupData(s_data);
        memset(&s_osdPresets, 0, sizeof(s_osdPresets));
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
                s_statusNote = "Older save on SD: upgrade in match";
            }
            else
            {
                s_hasCard = true;
                u32 tableOff = sizeof(FileHeader) + sizeof(SettingsHead);
                const AreaEntry* entries = reinterpret_cast<const AreaEntry*>(s_cardBuf + tableOff);
                for (u16 i = 0; i < head->areaCount; i++)
                {
                    const AreaEntry& e = entries[i];
                    if (e.generation != kAreaGen) continue;
                    if (e.id == storage::kAreaGeneral)
                    {
                        u32 n = e.used < kGeneralUsed ? e.used : kGeneralUsed;
                        memcpy(&s_data.general, s_cardBuf + e.offset, n);
                    }
                    else if (e.id == storage::kAreaDisplay)
                    {
                        u32 n = e.used < kDisplayUsed ? e.used : kDisplayUsed;
                        memcpy(&s_data.display, s_cardBuf + e.offset, n);
                    }
                    else if (e.id == storage::kAreaOsd)
                    {
                        u32 n = e.used < kOsdUsed ? e.used : kOsdUsed;
                        memcpy(&s_data.osd, s_cardBuf + e.offset, n);
                    }
                    else if (e.id == storage::kAreaOsdPresets)
                    {
                        u32 n = e.used < kOsdPresetsUsed ? e.used : kOsdPresetsUsed;
                        memcpy(&s_osdPresets, s_cardBuf + e.offset, n);
                    }
                    else if (e.id == storage::kAreaCpu && e.used > kCpuTechOsdOff)
                    {
                        s_data.techTrackerOsd = s_cardBuf[e.offset + kCpuTechOsdOff] != 0 ? 1 : 0;
                    }
                }
            }
        }

        Settings* live = liveSettings();
        if (live != NULL)
        {
            s_data.general = live->general;
            s_data.display = live->display;
            s_data.osd = live->osd;
            s_osdPresets = live->osdPresets;
            s_data.techTrackerOsd = live->cpu.options.techTrackerOsd != 0 ? 1 : 0;
        }
    }

    static void pushLive()
    {
        Settings* live = liveSettings();
        if (live != NULL)
        {
            live->general = s_data.general;
            live->display = s_data.display;
            live->osd = s_data.osd;
            live->cpu.options.techTrackerOsd = s_data.techTrackerOsd ? 1 : 0;
        }
    }

    static void flushChanges()
    {
        if (!s_dirty) return;
        s_dirty = false;
        pushLive();

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
        if (!patchQuickSetupAreasInFile(s_hasCard ? s_cardBuf : NULL, s_hasCard ? s_cardSize : 0, s_data,
                                        s_writeBuf, storage::kSettingsFileCap, nextSeq, outSz))
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
        if (!down) { r.held = 0; return 0; }
        r.held++;
        if (r.held == 1) { r.next = kRepeatStart + 1; return 1; }
        if (r.held < r.next) return 0;
        r.next = r.held + kRepeatEvery;
        return 1;
    }

    static const QuickRow* activeRows(int& outCount)
    {
        if (s_subView)
        {
            outCount = kSubCount;
            return kSubRows;
        }
        outCount = kMainCount;
        return kMainRows;
    }

    static void keepVisible(int view)
    {
        if (s_cursor[view] < s_scroll[view]) s_scroll[view] = s_cursor[view];
        if (s_cursor[view] >= s_scroll[view] + kVisibleRows) s_scroll[view] = s_cursor[view] - kVisibleRows + 1;
    }

    static bool rowEnabled(const QuickRow& r)
    {
        if (r.source == kSrcOsd && r.offset == offsetof(OsdSettings, advantageTolerance))
        {
            return s_data.osd.frameAdvantage != 0 || s_data.osd.frameAdvantageOnHit != 0;
        }
        return true;
    }

    static u8 readRowValue(const QuickRow& r)
    {
        switch (r.source)
        {
        case kSrcOsd: return reinterpret_cast<const u8*>(&s_data.osd)[r.offset];
        case kSrcDisplay: return reinterpret_cast<const u8*>(&s_data.display)[r.offset];
        case kSrcGeneral: return reinterpret_cast<const u8*>(&s_data.general)[r.offset];
        case kSrcTechOsd: return s_data.techTrackerOsd ? 1 : 0;
        default: return 0;
        }
    }

    static void writeRowValue(const QuickRow& r, u8 v)
    {
        switch (r.source)
        {
        case kSrcOsd: reinterpret_cast<u8*>(&s_data.osd)[r.offset] = v; break;
        case kSrcDisplay: reinterpret_cast<u8*>(&s_data.display)[r.offset] = v; break;
        case kSrcGeneral: reinterpret_cast<u8*>(&s_data.general)[r.offset] = v; break;
        case kSrcTechOsd: s_data.techTrackerOsd = v ? 1 : 0; break;
        default: break;
        }
    }

    static int activeIndividualOsdCount()
    {
        int n = s_data.techTrackerOsd ? 1 : 0;
        const u8* raw = reinterpret_cast<const u8*>(&s_data.osd);
        for (int i = 0; i < 19; i++)
        {
            if (raw[kPresetOffsets[i]] != 0) n++;
        }
        return n;
    }

    static bool quickRowHighlighted(const QuickRow& r, u8 v)
    {
        if (r.kind == kKindSub) return false;
        if (r.kind == kKindToggle) return v != 0;
        if (r.source == kSrcOsd)
        {
            if (r.offset == offsetof(OsdSettings, preset)) return v != main::kOsdPresetEssentials;
            if (r.offset == offsetof(OsdSettings, position)) return v != osd::kPositionSides;
            if (r.offset == offsetof(OsdSettings, maxMessages)) return v != 4;
            if (r.offset == offsetof(OsdSettings, advantageTolerance)) return v != 1;
        }
        return v != 0;
    }

    static void adjustRow(const QuickRow& r, int delta)
    {
        if (r.kind == kKindSub) return;
        if (s_newerOnCard || s_olderLayoutOnCard || !rowEnabled(r))
        {
            sound(game::kSoundError);
            return;
        }
        int cur = readRowValue(r);
        int next = cur;
        if (r.kind == kKindToggle)
        {
            next = cur ? 0 : 1;
        }
        else
        {
            next = cur + delta;
            if (next < r.minVal) next = r.minVal;
            if (next > r.maxVal) next = r.maxVal;
        }
        if (next == cur) return;
        writeRowValue(r, static_cast<u8>(next));
        if (r.source == kSrcOsd && r.offset == offsetof(OsdSettings, preset))
        {
            applyPresetMask(static_cast<u8>(next));
            if (next >= main::kOsdPresetCustom1)
            {
                u32 slot = static_cast<u32>(next - main::kOsdPresetCustom1);
                bool empty = slot >= main::kOsdCustomSlotCount
                             || (s_osdPresets.customMask[slot] & main::kOsdCustomSavedBit) == 0u;
                s_statusNote = empty ? "Custom slot empty (save in Lab Menu > OSDs)." : NULL;
            }
            else if (!s_newerOnCard && !s_olderLayoutOnCard)
            {
                s_statusNote = NULL;
            }
        }
        else if (r.kind == kKindToggle)
        {
            s_data.osd.preset = main::kOsdPresetCustom;
            if (!s_newerOnCard && !s_olderLayoutOnCard)
            {
                s_statusNote = NULL;
            }
        }
        pushLive();
        s_dirty = true;
        sound(game::kSoundMove);
    }

    static ui::style::ModalAnim s_anim;

    void attach()
    {
        s_open = false;
        s_subView = false;
        s_dirty = false;
        s_phase = kIdle;
        s_jobStale = s_job.busy();
        s_cursor[0] = s_cursor[1] = 0;
        s_scroll[0] = s_scroll[1] = 0;
        memset(s_rep, 0, sizeof(s_rep));
        s_statusNote = NULL;
        ui::style::modalAnimReset(s_anim);
    }

    void detach()
    {
        flushChanges();
        finishPendingWrite();
        s_open = false;
        s_subView = false;
    }

    void open(int port, u32 controllerType)
    {
        loadFromCardAndLive();
        s_open = true;
        s_subView = false;
        s_dirty = false;
        s_type = controllerType;
        s_cursor[0] = s_cursor[1] = 0;
        s_scroll[0] = s_scroll[1] = 0;
        memset(s_rep, 0, sizeof(s_rep));
        ui::style::modalAnimOpen(s_anim);
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
        if (!s_open) return;
        s_type = in.type;
        const game::PadMap& map = game::padMap(in.type);
        int view = s_subView ? 1 : 0;
        int count = 0;
        const QuickRow* rows = activeRows(count);

        bool up = (in.held & map.up) || in.stickY > kStickDeadZone;
        bool down = (in.held & map.down) || in.stickY < -kStickDeadZone;
        bool left = (in.held & game::kPadDLeft) || in.stickX < -kStickDeadZone;
        bool right = (in.held & game::kPadDRight) || in.stickX > kStickDeadZone;
        if (repeat(s_rep[0], up && !down) && s_cursor[view] > 0)
        {
            s_cursor[view]--;
            keepVisible(view);
            sound(game::kSoundMove);
        }
        if (repeat(s_rep[1], down && !up) && s_cursor[view] + 1 < count)
        {
            s_cursor[view]++;
            keepVisible(view);
            sound(game::kSoundMove);
        }
        const QuickRow& curRow = rows[s_cursor[view]];
        if (repeat(s_rep[2], left && !right))
        {
            adjustRow(curRow, -1);
        }
        if (repeat(s_rep[3], right && !left))
        {
            adjustRow(curRow, 1);
        }
        if ((in.pressed & map.confirm) != 0)
        {
            if (curRow.kind == kKindSub)
            {
                s_subView = true;
                s_cursor[1] = 0;
                s_scroll[1] = 0;
                s_anim.hasCursor = 0;
                gfx::forgetWidths();
                sound(game::kSoundConfirm);
                return;
            }
            if (curRow.kind == kKindToggle)
            {
                adjustRow(curRow, 1);
            }
            else
            {
                int cur = readRowValue(curRow);
                int next = cur >= curRow.maxVal ? curRow.minVal : cur + 1;
                adjustRow(curRow, next - cur);
            }
            return;
        }
        if ((in.pressed & map.back) != 0)
        {
            if (s_subView)
            {
                s_subView = false;
                s_anim.hasCursor = 0;
                gfx::forgetWidths();
                sound(game::kSoundBack);
                return;
            }
            flushChanges();
            s_open = false;
            sound(game::kSoundMove);
            TM_LOG("osdquick: closed\n");
        }
    }

    void update()
    {
        int result = storage::kOk;
        int status = 0;
        if (s_jobStale)
        {
            if (s_job.poll(result, status)) s_jobStale = false;
            return;
        }
        if (s_phase == kIdle || !s_job.poll(result, status)) return;
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
        if (gfx::lineHeight(rowScale) <= 0.0f) return;

        float alpha = 1.0f;
        float dy = modalAnimStep(s_anim, alpha);

        static const ModalFrame kFrame = {
            56.0f, 584.0f, 34.0f, 446.0f, 71.0f, 379.0f, 407.0f, 326.0f, kLabModalBackdrop
        };
        const float inL43 = kFrame.left43 + kLabBorder;
        const float inR43 = kFrame.right43 - kLabBorder;
        const float inTop = kFrame.topY + dy + kLabBorder;
        const float inBot = kFrame.botY + dy - kLabBorder;
        const float bodyTop = kFrame.hdrBot + dy + kFibHair;
        const float descTop = kFrame.descTop + dy;
        const float footTop = kFrame.footTop + dy;
        drawModalFrame(kFrame, s_subView ? "All OSDs" : "Quick Setup", dy, alpha);

        float hdrMid = (inTop + kFrame.hdrBot + dy) * 0.5f;
        const char* sub = s_statusNote != NULL ? s_statusNote : "Saves to SD on exit";
        float subScale = scaleForCap(kTypeMeta);
        float subW = gfx::textWidth(sub, subScale);
        float subMax = w(inR43 - kFrame.ribbonRight43 - kFibPad);
        float subDrawW = subW > subMax ? subMax : subW;
        drawFitText(cx(inR43 - kFibPad) - subDrawW, textYForCap(hdrMid - kTypeMeta * 0.5f, subScale), sub,
                    s_statusNote != NULL ? kLabFocusGold : kLabMutedText, subScale, subMax);

        int view = s_subView ? 1 : 0;
        int rowCount = 0;
        const QuickRow* rows = activeRows(rowCount);
        int cursor = s_cursor[view];
        int scroll = s_scroll[view];

        const float rowH = 25.0f;
        const float rowPitch = rowH + kFibTight;
        const float rowTop = bodyTop + 20.0f;
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

        for (int r = 0; r < kVisibleRows && scroll + r < rowCount; r++)
        {
            float y = rowTop + r * rowPitch;
            drawSlantBar(hlLeft43, hlRight43, y, rowH, false, kLabRowSlateTop, kLabRowSlateBot, kLabRowSlateEdge);
        }
        if (cursor >= scroll && cursor < scroll + kVisibleRows)
        {
            float targetY = (kFrame.hdrBot + kFibHair + 20.0f) + (cursor - scroll) * rowPitch;
            float barY = modalAnimCursor(s_anim, targetY) + dy;
            const QuickRow& selRow = rows[cursor];
            float stripeR = selRow.kind == kKindSub ? (hlRight43 - 18.0f) : (pillLeft43 + 4.5f);
            drawGoldBar(hlLeft43, hlRight43, barY, rowH, 8.5f, false, stripeR);
        }

        for (int r = 0; r < kVisibleRows && scroll + r < rowCount; r++)
        {
            int idx = scroll + r;
            const QuickRow& row = rows[idx];
            float y = rowTop + r * rowPitch;
            bool sel = idx == cursor;
            bool on = rowEnabled(row);

            u8 v = readRowValue(row);
            bool isHi = quickRowHighlighted(row, v);
            if (row.kind != kKindSub)
            {
                drawValuePill(pillLeft43, pillRight43, y, rowH, isHi, on, sel);
                if (sel && on)
                {
                    drawValueArrows(pillLeft43, pillRight43, y, rowH, isHi ? kLabFocusGold : kLabGoldInk);
                }
            }

            float ty = textYForCap(y + capOff, rowScale);
            gfx::Rgba nameCol = sel ? (on ? kLabGoldInk : kLabGoldInkMuted) : (on ? kText : kTextDisabled);
            drawFitText(nameX, ty, row.name, nameCol, rowScale, nameMaxW);

            char buf[24];
            const char* valTxt = buf;
            if (row.kind == kKindSub)
            {
                int cnt = activeIndividualOsdCount();
                snprintf(buf, sizeof(buf), cnt > 0 ? "%d Active  >" : "All Off  >", cnt);
                gfx::Rgba valCol = sel ? kLabGoldInk : kLabMutedText;
                drawFitText(pillCenter, ty, valTxt, valCol, rowScale, valMaxW, true);
                continue;
            }
            if (row.kind == kKindToggle)
            {
                valTxt = v != 0 ? "On" : "Off";
            }
            else if (row.kind == kKindInt)
            {
                snprintf(buf, sizeof(buf), (row.source == kSrcOsd && row.offset == offsetof(OsdSettings, advantageTolerance)) ? "%uf" : "%u",
                         static_cast<unsigned>(v));
            }
            else
            {
                if (row.source == kSrcOsd && row.offset == offsetof(OsdSettings, preset)
                    && v >= main::kOsdPresetCustom1 && v < main::kOsdPresetCount)
                {
                    u32 slot = static_cast<u32>(v - main::kOsdPresetCustom1);
                    if ((s_osdPresets.customMask[slot] & main::kOsdCustomSavedBit) == 0u)
                    {
                        snprintf(buf, sizeof(buf), "Custom %u (Empty)", static_cast<unsigned>(slot + 1u));
                        valTxt = buf;
                    }
                    else
                    {
                        valTxt = row.labels[v];
                    }
                }
                else
                {
                    valTxt = (v <= row.maxVal && row.labels != NULL) ? row.labels[v] : "?";
                }
            }
            gfx::Rgba valCol = on ? ((sel && !isHi) ? kLabGoldInk : kLabWhiteText) : kTextDisabled;
            drawFitText(pillCenter, ty, valTxt, valCol, rowScale, valMaxW, true);
        }

        drawScrollBarAt(sbX43, rowTop, (kVisibleRows - 1) * rowPitch + rowH, rowCount, scroll, kVisibleRows,
                        kLabDivider, kLabTealTop, kLabTealBot);

        const char* help = (!s_subView && cursor >= 0 && cursor < kMainCount)
                               ? kMainHelp[cursor]
                               : "Toggle individual OSDs, Max OSDs, or Tolerance.";
        float hScale = scaleForCap(kTypeMeta);
        float hy = textYForCap(descTop + kFibHair + (footTop - descTop - kFibHair - kTypeMeta) * 0.5f, hScale);
        drawFitText(cx(inL43 + kFibPad), hy, help, kLabDescText, hScale, w(inR43 - inL43 - 2.0f * kFibPad));

        const char* okGlyph = s_type == gfPadType::WIIMOTE ? "2" : "A";
        const char* backGlyph = s_type == gfPadType::WIIMOTE ? "1" : "B";
        float fy = footTop + kFibHair + (inBot - (footTop + kFibHair)) * 0.5f;
        float fx = cx(inL43 + kFibPad);
        u8 flags = kBadgeRound | kBadgeTightGap;
        const char* okLabel = rows[cursor].kind == kKindSub ? "Open" : "Toggle / Cycle";
        fx = drawFooterBadge(fx, fy, 17.0f, okGlyph, okLabel, kBtnA, flags);
        const char* backLabel = s_subView ? "Back" : ((s_newerOnCard || s_olderLayoutOnCard) ? "Back" : "Back & Save");
        drawFooterBadge(fx, fy, 17.0f, backGlyph, backLabel, kBtnB, flags);
    }
}
}
}
