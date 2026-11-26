#include "main/storage/settings_file.h"

#include <memory.h>
#include <stddef.h>
#ifdef __MWERKS__
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#endif

#include "main/extra.h"
#include "main/settings.h"
#include "main/storage/areas.h"
#include "main/storage/block.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/storage/notice.h"
#include "main/ui/lab_menu.h"
#include "shared/log.h"

namespace tm {
namespace storage {
    using main::Settings;

    enum {
        // Worst-case size across all 16 feature areas (checked at build time
        // in areas.cpp against kSettingsFileCap).
        kFileCap = kSettingsFileCap,
        kWaitMs = 3000
    };
    static const char* const kNames[2] = {"settings-a.bin", "settings-b.bin"};

    // Fields kept for the session only: the card never restores them, so the
    // next boot starts with their defaults. Each would surprise there: the
    // game held by frame advance, a damage locked, a slowed game, the HUD,
    // models or stage hidden, Training's own CPU replaced by the lab's,
    // recording modes for inputs that are gone (recordings do not outlive a
    // match; Export keeps them).
    struct Range {
        u16 offset;
        u16 size;
    };
#define TM_SESSION(f) {offsetof(Settings, f), sizeof(((Settings*)0)->f)}
    static const Range kSessionOnly[] = {
        TM_SESSION(general.playerPercent),
        TM_SESSION(general.lockPlayerPercent),
        TM_SESSION(general.frameAdvance),
        TM_SESSION(general.gameSpeed),
        TM_SESSION(display.debugOverlay),
        TM_SESSION(display.hud),
        TM_SESSION(display.models),
        TM_SESSION(display.stageModel),
        TM_SESSION(display.effects),
        TM_SESSION(cpu.percent),
        TM_SESSION(cpu.lockPercent),
        TM_SESSION(cpu.options.control),
        TM_SESSION(comboTrainer.enabled),
        // Recording: everything from HMN Mode on (Random Percent is kept).
        {offsetof(Settings, recording.hmnMode),
         offsetof(main::RecordingSettings, reserved) - offsetof(main::RecordingSettings, hmnMode)},
    };
#undef TM_SESSION
    enum { kSessionCount = sizeof(kSessionOnly) / sizeof(kSessionOnly[0]) };

    static State* s_state = NULL;
    static Job s_job;
    // Single 32-byte aligned file buffer used for builds, writes, and holding
    // the newest loaded file (allocated in Heaps::Network on console).
#ifdef __MWERKS__
    static u8* s_file = NULL;
#else
    static u8 s_fileStatic[kFileCap];
    static u8* s_file = s_fileStatic;
#endif

    static void ensureFileBuf()
    {
#ifdef __MWERKS__
        if (s_file != NULL)
        {
            return;
        }
        s_file = static_cast<u8*>(game::allocNetworkZero(static_cast<u32>(kFileCap)));
        if (s_file == NULL)
        {
            TM_LOG("storage: settings file buffer alloc failed (%u B in Network)\n", static_cast<unsigned>(kFileCap));
        }
#endif
    }

    enum Phase { kIdle, kFolder, kWrite };
    static int s_phase = kIdle;
    static bool s_again = false; // closed again while writing
    static bool s_jobStale = false; // timed-out request still draining
    static u32 s_pendingView = 0;
    static u32 s_pendingSequence = 0;
    static u8 s_pendingSlot = 0;
    static u32 s_pendingSize = 0;

    // Pending v1 -> v2 upgrade modal state (held in s_file until confirmed).
    static bool s_v1Pending = false;
    static int s_v1Affected = 0;
    static u32 s_v1UnchangedMask = 0;
    static u32 s_v1FileSize = 0;
    static u8 s_v1SessionBuf[64];
    enum {
        kV1SessionBytes =
            sizeof(((Settings*)0)->general.playerPercent) + sizeof(((Settings*)0)->general.lockPlayerPercent)
            + sizeof(((Settings*)0)->general.frameAdvance) + sizeof(((Settings*)0)->general.gameSpeed)
            + sizeof(((Settings*)0)->display.debugOverlay) + sizeof(((Settings*)0)->display.hud)
            + sizeof(((Settings*)0)->display.models) + sizeof(((Settings*)0)->display.stageModel)
            + sizeof(((Settings*)0)->display.effects) + sizeof(((Settings*)0)->cpu.percent)
            + sizeof(((Settings*)0)->cpu.lockPercent) + sizeof(((Settings*)0)->cpu.options.control)
            + sizeof(((Settings*)0)->comboTrainer.enabled)
            + (offsetof(main::RecordingSettings, reserved) - offsetof(main::RecordingSettings, hmnMode))
    };
    TM_STATIC_ASSERT(kV1SessionBytes <= sizeof(s_v1SessionBuf), tm_v1_session_buf_fits);

#ifdef __MWERKS__
#pragma dont_inline on
#endif
    static void copySessionFields(Settings& s, u8* buf, bool toBuf)
    {
        u8* p = reinterpret_cast<u8*>(&s);
        u32 pos = 0;
        for (int i = 0; i < kSessionCount; i++)
        {
            u32 n = kSessionOnly[i].size;
            if (pos + n <= sizeof(s_v1SessionBuf))
            {
                memcpy(toBuf ? (buf + pos) : (p + kSessionOnly[i].offset),
                       toBuf ? (p + kSessionOnly[i].offset) : (buf + pos), n);
                pos += n;
            }
        }
    }

    static void startSave();

    // The settings as saved, with active scenario overrides projected back to
    // the pre-preset snapshot and session-only fields zeroed.
    static void savedView(Settings& out)
    {
        memcpy(&out, &main::settings(), sizeof(Settings));
        main::extraScenarioSavedView(out);
        u8* p = reinterpret_cast<u8*>(&out);
        for (int i = 0; i < kSessionCount; i++)
        {
            memset(p + kSessionOnly[i].offset, 0, kSessionOnly[i].size);
        }
    }

    // Builds the file in s_file; returns its size (0 if it does not fit) and
    // the checksum of its areas, which is what "changed" compares.
    static u32 build(u32& view)
    {
        if (s_file == NULL)
        {
            return 0;
        }
        Settings saved;
        savedView(saved);
        memset(s_file, 0, kFileCap);
        FileHeader* h = reinterpret_cast<FileHeader*>(s_file);
        SettingsHead* head = reinterpret_cast<SettingsHead*>(s_file + sizeof(FileHeader));
        u32 at = sizeof(FileHeader) + sizeof(SettingsHead);
        u16 count = 0;
        u32 n = writeAreas(s_file + at, kFileCap - at, at, reinterpret_cast<const u8*>(&saved), kAllAreas, count);
        if (n == 0)
        {
            return 0;
        }
        view = checksum(s_file + at, n);
        head->areaCount = count;
        head->layout = main::kSettingsVersion;
        h->magic = kSettingsMagic;
        h->format = kSettingsFormat;
        h->headerSize = sizeof(FileHeader);
        h->size = at + n;
        return at + n;
    }

    static u32 currentView()
    {
        u32 view = 0;
        build(view);
        return view;
    }

    // Blocking read of one of the two files via a heap buffer: a Result.
    // Copies into s_file only when this slot is valid and newer than any
    // previous usable slot, avoiding a second static buffer.
    static int readFile(int slot, u32& sequence, bool hasBest, u32 bestSeq)
    {
        int status = 0;
        sequence = 0;
        if (s_file == NULL)
        {
            return kNoMemory;
        }
        if (s_jobStale || s_job.busy())
        {
            return kMissing;
        }
        u8* buf = allocReadBuffer(kFileCap);
        if (buf == NULL)
        {
            return kNoMemory;
        }
        int freeR = kOk;
        int freeStatus = 0;
        int r = kMissing;
        if (!s_job.read(kNames[slot], buf, sizeof(FileHeader)))
        {
            goto out;
        }
        r = s_job.wait(kWaitMs, status);
        if (r != kOk)
        {
            freeR = r;
            freeStatus = status;
            if (r == kBusy)
            {
                s_jobStale = true;
                r = kMissing;
            }
            goto out;
        }
        {
            const FileHeader* h = reinterpret_cast<const FileHeader*>(buf);
            r = checkHeader(buf, sizeof(FileHeader), kSettingsMagic, kSettingsFormat, false);
            if (r != kOk && r != kNewer)
            {
                goto out;
            }
            u32 minSize = r == kNewer ? sizeof(FileHeader) : (sizeof(FileHeader) + sizeof(SettingsHead));
            u32 size = h->size;
            if (size > kFileCap || size < minSize || !s_job.read(kNames[slot], buf, size))
            {
                r = kCorrupt;
                goto out;
            }
            r = s_job.wait(kWaitMs, status);
            if (r != kOk)
            {
                freeR = r;
                freeStatus = status;
                if (r == kBusy)
                {
                    s_jobStale = true;
                }
                if (r == kMissing || r == kBusy)
                {
                    r = kCorrupt;
                }
                goto out;
            }
            r = checkHeader(buf, size, kSettingsMagic, kSettingsFormat, true);
            if (r == kNewer)
            {
                sequence = reinterpret_cast<const FileHeader*>(buf)->sequence;
            }
            else if (r == kOk)
            {
                const SettingsHead* head = reinterpret_cast<const SettingsHead*>(buf + sizeof(FileHeader));
                if (!validAreas(buf, size, sizeof(FileHeader) + sizeof(SettingsHead), head->areaCount))
                {
                    r = kCorrupt;
                }
                else
                {
                    sequence = reinterpret_cast<const FileHeader*>(buf)->sequence;
                    if (!hasBest || sequenceNewer(sequence, bestSeq))
                    {
                        memcpy(s_file, buf, size);
                    }
                }
            }
        }
    out:
        freeReadBuffer(buf, freeR, freeStatus);
        return r;
    }
#ifdef __MWERKS__
#pragma dont_inline reset
#endif

    static void load()
    {
        if (s_file == NULL)
        {
            return;
        }
        int results[2];
        u32 seqs[2];
        int best = -1;
        for (int slot = 0; slot < 2; slot++)
        {
            results[slot] = readFile(slot, seqs[slot], best >= 0, best >= 0 ? seqs[best] : 0);
            bool usable = results[slot] == kOk || results[slot] == kNewer;
            if (usable && (best < 0 || sequenceNewer(seqs[slot], seqs[best])))
            {
                best = slot;
            }
        }
        TM_LOG("storage: settings files: a %s (%u), b %s (%u)\n", resultText(results[0]), seqs[0],
               resultText(results[1]), seqs[1]);
        // The next write goes to the other file than the newest usable one,
        // so that one stays until the new one is complete.
        // Without one, a damaged a.bin is replaced first, else b.bin.
        bool onlyBDamaged = results[1] == kCorrupt && results[0] != kCorrupt;
        s_state->slot = static_cast<u8>(best >= 0 ? best : (onlyBDamaged ? 0 : 1));
        s_state->sequence = best >= 0 ? seqs[best] : 0;
        bool damaged = results[0] == kCorrupt || results[0] == kOlder || results[1] == kCorrupt
                       || results[1] == kOlder;
        if (best < 0)
        {
            if (damaged)
            {
                notice(kNoticeError, "Lab settings file damaged: defaults used");
            }
            return;
        }
        if (results[best] == kNewer)
        {
            s_state->newerOnCard = 1;
            notice(kNoticeWarn, "Lab settings are from a newer version: not used");
            return;
        }
        // Defaults first (the block was just reset or holds this session's
        // values; the file's areas replace them, the session-only fields stay).
        Settings& s = main::settings();
        Settings before;
        memcpy(&before, &s, sizeof(Settings));
        const FileHeader* h = reinterpret_cast<const FileHeader*>(s_file);
        const SettingsHead* head = reinterpret_cast<const SettingsHead*>(s_file + sizeof(FileHeader));
        if (head->layout > main::kSettingsVersion)
        {
            s_state->newerOnCard = 1;
            TM_LOG("storage: settings layout %u newer than %u: not used\n", head->layout, main::kSettingsVersion);
            notice(kNoticeWarn, "Lab settings are from a newer version: not used");
            return;
        }
        if (head->layout < main::kSettingsVersion)
        {
            // Keep the valid v1 save intact in s_file and compute which areas
            // changed generation or used size vs current v2 so the modal can
            // offer Reset All, Reset Affected, or Keep My Settings.
            copySessionFields(before, s_v1SessionBuf, true);
            s_v1FileSize = h->size;
            s_v1UnchangedMask = 0;
            s_v1Affected = 0;

            TM_STATIC_ASSERT((kFileCap & 3u) == 0u, tm_settings_file_cap_word_aligned);
            u32 refWords[kFileCap / 4];
            u8* refBuf = reinterpret_cast<u8*>(refWords);
            u32 tableOff = sizeof(FileHeader) + sizeof(SettingsHead);
            u16 refCount = 0;
            if (writeAreas(refBuf, kFileCap, 0, reinterpret_cast<const u8*>(&s), kAllAreas, refCount) > 0)
            {
                const AreaEntry* refTable = reinterpret_cast<const AreaEntry*>(refBuf);
                const AreaEntry* oldTable = reinterpret_cast<const AreaEntry*>(s_file + tableOff);
                for (u16 r = 0; r < refCount; r++)
                {
                    bool same = false;
                    for (u16 o = 0; o < head->areaCount; o++)
                    {
                        if (oldTable[o].id == refTable[r].id)
                        {
                            same = (oldTable[o].generation == refTable[r].generation
                                    && oldTable[o].used == refTable[r].used);
                            break;
                        }
                    }
                    if (same)
                    {
                        s_v1UnchangedMask |= areaBit(refTable[r].id);
                    }
                    else
                    {
                        s_v1Affected++;
                    }
                }
            }
            s_v1Pending = true;
            TM_LOG("storage: settings layout %u older than %u (%d areas affected): upgrade modal pending\n",
                   head->layout, main::kSettingsVersion, s_v1Affected);
            return;
        }
        AreaStats stats;
        if (!readAreas(s_file, h->size, sizeof(FileHeader) + sizeof(SettingsHead), head->areaCount,
                       reinterpret_cast<u8*>(&s), kAllAreas, stats))
        {
            memcpy(&s, &before, sizeof(Settings));
            notice(kNoticeError, "Lab settings file damaged: defaults used");
            return;
        }
        u8* p = reinterpret_cast<u8*>(&s);
        const u8* b = reinterpret_cast<const u8*>(&before);
        for (int i = 0; i < kSessionCount; i++)
        {
            memcpy(p + kSessionOnly[i].offset, b + kSessionOnly[i].offset, kSessionOnly[i].size);
        }
        ui::clampSettings();
        main::sanitizeSettings(s);
        ui::retireDroppedValues();
        TM_LOG("storage: settings loaded from %s (save %u, layout %u): %u areas, %u with new fields, %u at defaults, "
               "%u unknown\n",
               kNames[best], h->sequence, head->layout, stats.copied, stats.grown, stats.reset, stats.skipped);
        bool quickOsdOnly = head->areaCount == 1 && stats.copied == 1 && stats.grown == 0
                            && reinterpret_cast<const AreaEntry*>(s_file + sizeof(FileHeader) + sizeof(SettingsHead))
                                       ->id
                                   == kAreaOsd;
        if (quickOsdOnly)
        {
            s_state->force = 1;
        }
        else if (stats.grown + stats.reset > 0)
        {
            notice(kNoticeWarn, "Lab settings from an older version: new ones at defaults");
        }
    }

    void settingsAttach(u8* block)
    {
        s_state = blockState(block);
        s_phase = kIdle;
        s_again = false;
        s_jobStale = s_job.busy();
        s_v1Pending = false;
        s_v1Affected = 0;
        s_v1UnchangedMask = 0;
        s_v1FileSize = 0;
        if (s_state == NULL)
        {
            return;
        }
        ensureFileBuf();
        if (s_state->magic == kStateMagic)
        {
            return;
        }
        if (s_file == NULL)
        {
            return;
        }
        memset(s_state, 0, sizeof(State));
        s_state->magic = kStateMagic;
        s_state->slot = 1;
        TM_LOG("storage: card %s, %s\n", cardInserted() ? "inserted" : "not inserted", cardLocked() ? "locked" : "writable");
        load();
        if (!s_v1Pending)
        {
            s_state->baseline = currentView();
        }
    }

    bool settingsUpgradePending()
    {
        return s_v1Pending;
    }

    int settingsUpgradeAffectedCount()
    {
        return s_v1Affected;
    }

    void settingsUpgradeApply(int choice)
    {
        if (!s_v1Pending || s_state == NULL || s_file == NULL)
        {
            s_v1Pending = false;
            return;
        }
        if (choice < kUpgradeResetAll || choice >= kUpgradeChoiceCount)
        {
            choice = kUpgradeResetAll;
        }
        Settings& s = main::settings();
        main::resetSettings();
        const SettingsHead* head = reinterpret_cast<const SettingsHead*>(s_file + sizeof(FileHeader));
        if (choice == kUpgradeResetAffected || choice == kUpgradeKeepMine)
        {
            u32 mask = choice == kUpgradeResetAffected ? s_v1UnchangedMask : kAllAreas;
            AreaStats stats;
            if (!readAreas(s_file, s_v1FileSize, sizeof(FileHeader) + sizeof(SettingsHead), head->areaCount,
                           reinterpret_cast<u8*>(&s), mask, stats))
            {
                main::resetSettings();
            }
        }
        copySessionFields(s, s_v1SessionBuf, false);
        ui::clampSettings();
        main::sanitizeSettings(s);
        ui::retireDroppedValues();

        s_v1Pending = false;
        s_v1Affected = 0;
        s_v1UnchangedMask = 0;
        s_v1FileSize = 0;

        TM_LOG("storage: v1 -> v2 upgrade choice %d applied (from save %u)\n", choice, s_state->sequence);
        s_state->force = 1;
        s_state->baseline = currentView();
        notice(kNoticeOk, choice == kUpgradeResetAll ? "Settings reset to v2 defaults"
                                                     : (choice == kUpgradeResetAffected
                                                            ? "Updated changed settings to v2"
                                                            : "Kept compatible settings (v2)"));
        startSave();
    }

    static void startSave()
    {
        if (s_file == NULL)
        {
            return;
        }
        if (s_state->newerOnCard)
        {
            TM_LOG("storage: settings not saved: newer format on the card\n");
            return;
        }
        u32 view = 0;
        u32 size = build(view);
        if (size == 0)
        {
            // Not expected (areas.cpp checks the largest file against the
            // cap at build time), but never a silent skip.
            TM_LOG("storage: settings not saved: the file does not fit in %d bytes\n", static_cast<int>(kFileCap));
            return;
        }
        if (!s_state->force && view == s_state->baseline)
        {
            return;
        }
        int w = writable();
        if (w != kOk)
        {
            notice(kNoticeError, "Lab settings not saved: %s", resultText(w));
            return;
        }
        FileHeader* h = reinterpret_cast<FileHeader*>(s_file);
        h->sequence = nextSequence(s_state->sequence);
        h->checksum = checksum(s_file + kChecksumFrom, size - kChecksumFrom);
        s_pendingView = view;
        s_pendingSequence = h->sequence;
        s_pendingSlot = static_cast<u8>(s_state->slot ^ 1);
        s_pendingSize = size;
        if (s_jobStale || s_job.busy())
        {
            s_again = true;
            notice(kNoticeWarn, "Lab settings not saved: %s", resultText(kBusy));
            return;
        }
        // The folder first (it exists after the first save; creating it again is harmless).
        if (!s_job.makeFolder())
        {
            s_phase = kIdle;
            notice(kNoticeWarn, "Lab settings not saved: %s", resultText(kBusy));
            return;
        }
        s_phase = kFolder;
    }

    void settingsMenuClosed()
    {
        if (s_state == NULL)
        {
            return;
        }
        if (s_phase != kIdle)
        {
            s_again = true;
            return;
        }
        startSave();
    }

    void settingsReset()
    {
        if (s_state != NULL)
        {
            s_state->force = 1;
        }
    }

    void settingsUpdate()
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
            if (s_again)
            {
                s_again = false;
                startSave();
            }
            return;
        }
        if (s_phase == kIdle || !s_job.poll(result, status))
        {
            return;
        }
        if (s_phase == kFolder)
        {
            if (!s_job.write(kNames[s_pendingSlot], s_file, s_pendingSize))
            {
                s_phase = kIdle;
                notice(kNoticeWarn, "Lab settings not saved: %s", resultText(kBusy));
                return;
            }
            s_phase = kWrite;
            return;
        }
        s_phase = kIdle;
        if (result == kOk)
        {
            s_state->slot = s_pendingSlot;
            s_state->sequence = s_pendingSequence;
            s_state->baseline = s_pendingView;
            s_state->force = 0;
            TM_LOG("storage: settings saved to %s (save %u, %u bytes)\n", kNames[s_pendingSlot], s_pendingSequence,
                   s_pendingSize);
        }
        else
        {
            TM_LOG("storage: settings save to %s failed: status %d\n", kNames[s_pendingSlot], status);
            notice(kNoticeError, "Lab settings not saved: %s", resultText(result));
        }
        if (s_again)
        {
            s_again = false;
            startSave();
        }
    }

    void settingsDetach()
    {
        if (s_v1Pending && s_state != NULL)
        {
            // Match ended before the player resolved the v1 modal: prompt again next match.
            s_state->magic = 0;
        }
        s_v1Pending = false;
        s_v1Affected = 0;
        s_v1UnchangedMask = 0;
        s_v1FileSize = 0;
        bool dmaBusy = false;
        if (s_phase != kIdle)
        {
            // Finish what was started: the folder, then the write.
            int status = 0;
            int r = s_job.wait(kWaitMs, status);
            if (s_phase == kFolder && r != kBusy && s_file != NULL)
            {
                s_phase = kWrite;
                r = s_job.write(kNames[s_pendingSlot], s_file, s_pendingSize) ? s_job.wait(kWaitMs, status) : kBusy;
            }
            if (r == kOk && s_state != NULL)
            {
                s_state->slot = s_pendingSlot;
                s_state->sequence = s_pendingSequence;
                s_state->baseline = s_pendingView;
                s_state->force = 0;
            }
            dmaBusy = (r == kBusy);
            if (dmaBusy)
            {
                s_jobStale = true;
            }
            TM_LOG("storage: settings write finished at detach: %s (status %d)\n", resultText(r), status);
            s_phase = kIdle;
        }
#ifdef __MWERKS__
        if (s_file != NULL)
        {
            if (!dmaBusy)
            {
                free(s_file);
            }
            s_file = NULL;
        }
#else
        (void)dmaBusy;
#endif
    }
}
}
