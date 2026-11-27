#include "menu/browser.h"

#include <cstdio>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <memory.h>

#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/storage/block.h"
#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "main/ui/style.h"
#include "menu/game/training.h"
#include "menu/names.h"
#include "menu/services.h"
#include "menu/sound.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/game/pad_map.h"
#include "shared/log.h"

namespace tm {
namespace menu {
namespace browser {
    using namespace ui::style;
    using storage::Job;

    enum {
        kStickDeadZone = 40,
        kMaxEntries = 70, // seven pages of 10
        kPageRows = kVisibleRows,
        kWaitMs = 5000,
        // A whole file is checked in one block of the Network heap, leaving
        // kTempReserve (512 KiB) free so costume decompression and any
        // transient SD buffers coexist cleanly.
        kTempHeap = Heaps::Network,
        kTempMax = 0x30000,
        kTempReserve = 0x80000,
        // Text slots after the training menu's.
        kSlotPage = 20,
        kSlotHelp,
        kSlotRows
    };

    enum Status { kUnread, kReady, kNewer, kOlder, kDamaged };
    enum Phase { kIdle, kHeaders, kVerify, kDelete };

    struct Entry {
        char file[storage::kNameMax];
        u32 size; // from the listing
        u8 status;
        storage::RecordingInfo info;
        u32 fileSize; // from the header
    };

    struct Repeat {
        u16 held;
        u16 next;
    };

    static Entry s_entries[kMaxEntries];
    static int s_count = 0;
    static bool s_more = false; // the folder holds more than kMaxEntries
    static bool s_open = false;
    static int s_port = 0;
    static u32 s_controllerType = gfPadType::GCC;
    static int s_row = 0;
    static bool s_confirm = false; // delete asked
    static bool s_waitRelease = false;
    static Repeat s_repeat[2];
    static const char* s_message = NULL;
    static char s_messageText[64];
    static int s_phase = kIdle;
    static int s_next = 0; // header to read next
    static int s_busyRow = -1;
    static Job s_job;
    static bool s_jobStale = false;
    static u8* s_head = NULL;
    static u8* s_temp = NULL;

    static float rowScale()
    {
        return scaleForCap(kRowCap);
    }

    static void say(const char* format, const char* detail)
    {
        snprintf(s_messageText, sizeof(s_messageText), format, detail);
        s_message = s_messageText;
    }

    // ---- the list ----

    static void listed(const char* name, u32 size, void*)
    {
        if (s_count >= kMaxEntries)
        {
            s_more = true;
            return;
        }
        Entry& e = s_entries[s_count];
        u32 n = 0;
        while (name[n] != '\0' && n + 1 < sizeof(e.file))
        {
            e.file[n] = name[n];
            n++;
        }
        if (name[n] != '\0')
        {
            return; // too long to be one of ours
        }
        e.file[n] = '\0';
        e.size = size;
        e.status = kUnread;
        e.fileSize = 0;
        memset(&e.info, 0, sizeof(e.info));
        s_count++;
    }

    static int compareNames(const char* a, const char* b)
    {
        while (*a != '\0' && *a == *b)
        {
            a++;
            b++;
        }
        return static_cast<int>(static_cast<u8>(*a)) - static_cast<int>(static_cast<u8>(*b));
    }

    // Newest first: the names start with the date and time.
    static void sortEntries()
    {
        for (int i = 1; i < s_count; i++)
        {
            Entry e = s_entries[i];
            int j = i - 1;
            while (j >= 0 && compareNames(s_entries[j].file, e.file) < 0)
            {
                s_entries[j + 1] = s_entries[j];
                j--;
            }
            s_entries[j + 1] = e;
        }
    }

    static void refresh()
    {
        s_count = 0;
        s_more = false;
        int n = storage::listFiles(storage::kRecordingSuffix, listed, NULL);
        sortEntries();
        s_next = 0;
        s_phase = s_count > 0 ? kHeaders : kIdle;
        if (s_row >= s_count)
        {
            s_row = s_count > 0 ? s_count - 1 : 0;
        }
        if (n < 0)
        {
            say("Could not load: %s", storage::resultText(storage::cardInserted() ? storage::kMissing : storage::kNoCard));
        }
        TM_LOG("browser: %d recording(s) listed (folder %s)%s\n", s_count, n < 0 ? "unreadable" : "read",
               s_more ? ", more not shown" : "");
    }

    static void readNextHeader()
    {
        while (s_next < s_count && s_entries[s_next].status != kUnread)
        {
            s_next++;
        }
        if (s_next >= s_count)
        {
            s_phase = kIdle;
            int n[kDamaged + 1] = {0, 0, 0, 0, 0};
            for (int i = 0; i < s_count; i++)
            {
                n[s_entries[i].status]++;
            }
            TM_LOG("browser: headers read: %d ready, %d newer, %d older, %d damaged\n", n[kReady], n[kNewer],
                   n[kOlder], n[kDamaged]);
            return;
        }
        Entry& e = s_entries[s_next];
        storage::freeReadBuffer(s_head, storage::kOk, 0);
        if (e.size < storage::kRecordingListBytes)
        {
            e.status = kDamaged;
            s_next++;
            return;
        }
        s_head = storage::allocReadBuffer(storage::kRecordingListBytes);
        if (s_head == NULL || !s_job.read(e.file, s_head, storage::kRecordingListBytes))
        {
            storage::freeReadBuffer(s_head, storage::kOk, 0);
            e.status = kDamaged;
            s_next++;
        }
    }

    static void headerRead(int result, int status)
    {
        Entry& e = s_entries[s_next];
        int r = result == storage::kOk && s_head != NULL
                    ? storage::checkHeader(s_head, storage::kRecordingListBytes, storage::kRecordingMagic,
                                           storage::kRecordingFormat, false)
                    : storage::kCorrupt;
        const storage::FileHeader* h = r == storage::kOk ? reinterpret_cast<const storage::FileHeader*>(s_head) : NULL;
        e.status = r == storage::kOk ? kReady : (r == storage::kNewer ? kNewer : (r == storage::kOlder ? kOlder : kDamaged));
        if (e.status == kReady && (h->size > e.size || h->size < storage::kRecordingMinBytes))
        {
            e.status = kDamaged; // cut short
        }
        if (e.status == kReady)
        {
            memcpy(&e.info, s_head + sizeof(storage::FileHeader), sizeof(e.info));
            e.fileSize = h->size;
        }
        storage::freeReadBuffer(s_head, result, status);
        s_next++;
    }

    // ---- loading ----

    static void freeTemp()
    {
        if (s_temp != NULL)
        {
            free(s_temp);
            s_temp = NULL;
        }
    }

    static u8* allocTemp(u32 bytes)
    {
        game::HeapUsage u;
        u.largest = 0;
        if (bytes > kTempMax || !game::heapUsage(kTempHeap, u) || u.largest < bytes + kTempReserve)
        {
            return NULL;
        }
        void* p = gfMemoryPool::alloc(gfHeapManager::getHeap(static_cast<Heaps::HeapType>(kTempHeap)), bytes, 32);
        return game::isRam(p) ? static_cast<u8*>(p) : NULL;
    }

    static void startVerify(int row)
    {
        Entry& e = s_entries[row];
        if (e.fileSize > kTempMax)
        {
            say("Too big to load: %s", e.file);
            return;
        }
        s_temp = allocTemp(storage::padded(e.fileSize));
        if (s_temp == NULL)
        {
            say("Could not load: %s", storage::resultText(storage::kNoMemory));
            return;
        }
        if (!s_job.read(e.file, s_temp, e.fileSize))
        {
            freeTemp();
            say("Could not load: %s", storage::resultText(storage::kBusy));
            return;
        }
        s_busyRow = row;
        s_phase = kVerify;
        s_message = "Loading...";
    }

    static bool startMatch(const Entry& e, bool& starting)
    {
        storage::Handoff* h = storage::blockHandoff(services().settings());
        if (h == NULL)
        {
            say("Could not load: %s", "no settings block");
            return false;
        }
        training::MatchSetup setup;
        setup.p1Kind = e.info.hmnKind;
        setup.p1Icon = e.info.hmnIcon;
        setup.cpuKind = e.info.cpuKind;
        setup.cpuIcon = e.info.cpuIcon;
        setup.stage = e.info.stage;
        // The character select's own bytes when the export found them, else
        // the match's.
        training::Looks looks;
        bool sel = e.info.hasSelect != 0;
        looks.p1ColorFile = sel ? e.info.hmnSelect[0] : e.info.hmnColorFile;
        looks.p1Color = sel ? e.info.hmnSelect[1] : e.info.hmnColor;
        looks.cpuColorFile = sel ? e.info.cpuSelect[0] : e.info.cpuColorFile;
        looks.cpuColor = sel ? e.info.cpuSelect[1] : e.info.cpuColor;
        looks.altStage = e.info.altStage;
        looks.hazardOff = e.info.hazardOff != 0 ? 1 : 0;
        memset(h, 0, sizeof(*h));
        for (u32 i = 0; i + 1 < sizeof(h->name) && e.file[i] != '\0'; i++)
        {
            h->name[i] = e.file[i];
        }
        h->size = e.fileSize;
        h->magic = storage::kHandoffMagic;
        const char* why = training::startMatch(setup, s_port, &looks);
        if (why != NULL)
        {
            h->magic = 0;
            say("Cannot start the match: %s", why);
            TM_LOG("browser: %s not started: %s\n", e.file, why);
            return false;
        }
        TM_LOG("browser: starting %s (kinds %u/%u, colours %u.%u/%u.%u, stage %u, hazOff %u)\n", e.file,
               setup.p1Kind, setup.cpuKind, looks.p1ColorFile, looks.p1Color, looks.cpuColorFile, looks.cpuColor,
               setup.stage, looks.hazardOff);
        starting = true;
        s_open = false;
        return true;
    }

    static bool s_startNow = false;

    static void verified(int result, int status)
    {
        Entry& e = s_entries[s_busyRow];
        int r = result == storage::kOk && s_temp != NULL
                    ? storage::checkHeader(s_temp, e.fileSize, storage::kRecordingMagic, storage::kRecordingFormat, true)
                    : storage::kCorrupt;
        storage::freeReadBuffer(s_temp, result, status);
        s_phase = s_next < s_count ? kHeaders : kIdle;
        if (r != storage::kOk)
        {
            e.status = kDamaged;
            say("Cannot load %s: the file is damaged.", e.file);
            TM_LOG("browser: %s failed its check: %s\n", e.file, storage::resultText(r));
            sound(game::kSoundError);
            return;
        }
        s_message = NULL;
        s_startNow = true;
    }

    // A: a confirm if the recording's load begins, an error if it cannot
    // (nothing listed, the card busy, a file this version cannot load).
    static void select()
    {
        if (s_count == 0 || s_phase == kVerify || s_phase == kDelete)
        {
            sound(game::kSoundError);
            return;
        }
        const Entry& e = s_entries[s_row];
        switch (e.status)
        {
        case kUnread:
            s_message = "Still reading the card...";
            sound(game::kSoundError);
            return;
        case kNewer:
            s_message = "Made by a newer version: update the mod to load it.";
            break;
        case kOlder:
            s_message = "Made by an older version that cannot load it.";
            break;
        case kDamaged:
            s_message = "This file is damaged and cannot be loaded.";
            break;
        default:
            break;
        }
        if (e.status == kReady && s_phase == kHeaders && s_job.busy())
        {
            s_message = "Still reading the card...";
        }
        else if (e.status == kReady)
        {
            startVerify(s_row);
        }
        if (s_message != NULL)
        {
            TM_LOG("browser: %s: %s\n", e.file, s_message);
        }
        sound(s_phase == kVerify ? game::kSoundConfirm : game::kSoundError);
    }

    // X: a confirm if the question is asked, an error if nothing can be
    // deleted now.
    static void askDelete()
    {
        if (s_count == 0 || s_phase == kVerify || s_phase == kDelete || s_job.busy())
        {
            sound(game::kSoundError);
            return;
        }
        int w = storage::writable();
        if (w != storage::kOk)
        {
            say("Cannot delete: %s.", storage::resultText(w));
            TM_LOG("browser: %s\n", s_message);
            sound(game::kSoundError);
            return;
        }
        s_confirm = true;
        s_message = NULL;
        sound(game::kSoundConfirm);
    }

    static void doDelete()
    {
        s_confirm = false;
        if (!s_job.remove(s_entries[s_row].file))
        {
            say("Cannot delete: %s.", "the card is busy");
            sound(game::kSoundError);
            return;
        }
        s_busyRow = s_row;
        s_phase = kDelete;
        sound(game::kSoundConfirm);
    }

    static void deleted(int result)
    {
        char name[storage::kNameMax];
        memcpy(name, s_entries[s_busyRow].file, sizeof(name));
        if (result != storage::kOk)
        {
            say("Could not delete %s.", name);
            TM_LOG("browser: delete %s failed\n", name);
            sound(game::kSoundError);
            s_phase = s_next < s_count ? kHeaders : kIdle;
            return;
        }
        for (int i = s_busyRow; i + 1 < s_count; i++)
        {
            s_entries[i] = s_entries[i + 1];
        }
        s_count--;
        if (s_next > s_busyRow)
        {
            s_next--;
        }
        if (s_row >= s_count && s_row > 0)
        {
            s_row--;
        }
        say("Deleted %s.", name);
        TM_LOG("browser: deleted %s\n", name);
        s_phase = s_next < s_count ? kHeaders : kIdle;
    }

    // ---- input ----

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

    static void moveTo(int row)
    {
        if (s_count == 0)
        {
            return;
        }
        row = row < 0 ? 0 : (row >= s_count ? s_count - 1 : row);
        if (row != s_row)
        {
            s_row = row;
            s_message = NULL;
            s_confirm = false;
            gfx::forgetWidths();
            TM_LOG("browser: row %d %s\n", s_row, s_entries[s_row].file);
            sound(game::kSoundMove);
        }
    }

    void setControllerType(u32 controllerType)
    {
        if (controllerType != s_controllerType)
        {
            s_controllerType = controllerType;
            gfx::forgetWidths();
        }
    }

    void step(int port, const Input& in, bool& starting)
    {
        s_port = port;
        setControllerType(in.controllerType);
        const game::PadMap& map = game::padMap(in.controllerType);
        if (s_waitRelease)
        {
            bool still = in.held != 0 || in.stickX > kStickDeadZone || in.stickX < -kStickDeadZone
                         || in.stickY > kStickDeadZone || in.stickY < -kStickDeadZone;
            if (still)
            {
                return;
            }
            s_waitRelease = false;
        }
        if (s_startNow)
        {
            s_startNow = false;
            startMatch(s_entries[s_busyRow], starting);
            return;
        }
        if (s_confirm)
        {
            if (in.pressed & map.confirm)
            {
                doDelete();
            }
            else if (in.pressed & (map.back | map.alt))
            {
                s_confirm = false;
                sound(game::kSoundBack);
            }
            return;
        }
        bool up = (in.held & map.up) || in.stickY > kStickDeadZone;
        bool down = (in.held & map.down) || in.stickY < -kStickDeadZone;
        if (repeat(s_repeat[0], up && !down))
        {
            moveTo(s_row - 1);
        }
        if (repeat(s_repeat[1], down && !up))
        {
            moveTo(s_row + 1);
        }
        if ((in.pressed & map.pagePrev) && !(in.held & map.pageNext))
        {
            moveTo((s_row / kPageRows - 1) * kPageRows);
        }
        if ((in.pressed & map.pageNext) && !(in.held & map.pagePrev))
        {
            int next = (s_row / kPageRows + 1) * kPageRows;
            if (next < s_count)
            {
                moveTo(next);
            }
        }
        if (in.pressed & map.back)
        {
            if (s_phase != kVerify && s_phase != kDelete)
            {
                s_open = false;
                TM_LOG("browser: closed\n");
                sound(game::kSoundBack);
            }
            return;
        }
        if (in.pressed & map.alt)
        {
            askDelete();
        }
        else if (in.pressed & map.confirm)
        {
            select();
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
        if (s_phase == kIdle)
        {
            return;
        }
        if (!s_job.busy())
        {
            if (s_phase == kHeaders)
            {
                readNextHeader();
            }
            return;
        }
        if (!s_job.poll(result, status))
        {
            return;
        }
        if (s_phase == kHeaders)
        {
            headerRead(result, status);
        }
        else if (s_phase == kVerify)
        {
            verified(result, status);
        }
        else if (s_phase == kDelete)
        {
            deleted(result);
        }
    }

    // ---- drawing ----

    static void lengthText(char* out, u32 size, u32 frames)
    {
        snprintf(out, size, "%u.%u s", frames / 60, frames % 60 * 10 / 60);
    }

    static void slotsText(char* out, u32 size, u8 mask)
    {
        u32 at = 0;
        for (int i = 0; i < 6 && at + 3 < size; i++)
        {
            if (mask & (1 << i))
            {
                out[at++] = static_cast<char>('1' + i);
                out[at++] = ' ';
            }
        }
        if (at == 0)
        {
            out[at++] = '-';
        }
        out[at] = '\0';
    }

    static void title(char* out, u32 size, const Entry& e)
    {
        if (e.status != kReady)
        {
            snprintf(out, size, "%s", e.file);
            return;
        }
        snprintf(out, size, "%s vs %s", characterName(e.info.hmnKind), characterName(e.info.cpuKind));
    }

    static const float kBrowserBottom = 452.0f;
    static const float kBrowserFootTop = 413.0f; // 449 - kFibBandH (34) - kFibHair (2)

    static void descLineAt(float capTop, const char* text, gfx::Rgba color)
    {
        float scale = scaleForCap(kTypeMeta);
        drawFitText(cx(kFooterX), textYForCap(capTop, scale), text, color, scale, w(kFooterMaxW));
    }

    static void descLine(int line, const char* text, gfx::Rgba color)
    {
        descLineAt(kFooterCapTop + line * kFooterPitch, text, color);
    }

    static void drawDescription()
    {
        static const gfx::Rgba kError = 0xFF8C8CFF;
        char line[80];
        if (s_confirm)
        {
            // High-contrast delete confirmation alert banner inside the description strip.
            float bx = cx(kPanelLeft + kLabBorder + kFibGap);
            float bw = w(kPanelRight - kPanelLeft - 2.0f * (kLabBorder + kFibGap));
            float by = kRowsBandBottom + kFibTight;
            float bh = kBrowserFootTop - kRowsBandBottom - 2.0f * kFibTight;
            gfx::fillRect(bx, by, bw, kFibHair, kLabBtnBTop);
            gfx::fillRect(bx, by + bh - kFibHair, bw, kFibHair, kLabBtnBTop);
            gfx::fillRect(bx, by + kFibHair, w(kFibHair), bh - 2.0f * kFibHair, kLabBtnBTop);
            gfx::fillRect(bx + bw - w(kFibHair), by + kFibHair, w(kFibHair), bh - 2.0f * kFibHair, kLabBtnBTop);
            gfx::fillGradV(bx + w(kFibHair), by + kFibHair, bw - w(2.0f * kFibHair), bh - 2.0f * kFibHair,
                           0x341016F0u, 0x20080DF0u);
            float linePitch = kFibPad + 2.0f;
            float line0Top = by + (bh - (linePitch + kTypeMeta)) * 0.5f;
            snprintf(line, sizeof(line), "Delete %s?", s_entries[s_row].file);
            descLineAt(line0Top, line, kLabFocusGold);
            descLineAt(line0Top + linePitch, "This cannot be undone.", kText);
            return;
        }
        if (s_phase == kDelete)
        {
            descLine(0, "Deleting...", kText);
            return;
        }
        if (s_message != NULL)
        {
            descLine(0, s_message, s_phase == kVerify ? kText : kError);
            return;
        }
        if (s_count == 0)
        {
            descLine(0, "No recordings found on the SD card.", kText);
            descLine(1, "Export one in a Training match: lab menu,", kLabDescText);
            descLine(2, "Recording > Export.", kLabDescText);
            return;
        }
        const Entry& e = s_entries[s_row];
        switch (e.status)
        {
        case kUnread:
            descLine(0, "Reading...", kText);
            return;
        case kNewer:
            descLine(0, "Made by a newer version of the mod.", kError);
            break;
        case kOlder:
            descLine(0, "Made by an older version that cannot load it.", kError);
            break;
        case kDamaged:
            descLine(0, "This file is damaged.", kError);
            break;
        default:
        {
            const storage::RecordingInfo& i = e.info;
            snprintf(line, sizeof(line), "%s (costume %u) vs %s (costume %u)", characterName(i.hmnKind),
                     i.hmnColor, characterName(i.cpuKind), i.cpuColor);
            descLine(0, line, kText);
            char length[16];
            lengthText(length, sizeof(length), i.frames);
            snprintf(line, sizeof(line), "%s, %u-%02u-%02u %02u:%02u, %s", stageName(i.stage), i.year, i.month,
                     i.day, i.hour, i.minute, length);
            descLine(1, line, kLabDescText);
            char hmn[16];
            char cpu[16];
            slotsText(hmn, sizeof(hmn), i.hmnSlots);
            slotsText(cpu, sizeof(cpu), i.cpuSlots);
            snprintf(line, sizeof(line), "Slots: HMN %s  CPU %s", hmn, cpu);
            descLine(2, line, kLabDescText);
            break;
        }
        }
        snprintf(line, sizeof(line), "File %s", e.file);
        descLine(3, line, kLabMutedText);
    }

    static void drawBrowserPanel()
    {
        static const ModalFrame kFrame = {
            kPanelLeft, kPanelRight, kPanelTop, kBrowserBottom,
            kTitleBandBottom - kFibHair, kRowsBandBottom - kFibHair, kBrowserFootTop,
            278.0f, 0x070B12B8u
        };
        drawModalFrame(kFrame, "Recordings");
    }

    void draw()
    {
        float scale = rowScale();
        if (gfx::lineHeight(scale) <= 0.0f)
        {
            return;
        }
        drawBrowserPanel();
        const float b = kLabBorder;
        const float inL43 = kPanelLeft + b;
        const float inR43 = kPanelRight - b;
        const float inTop = kPanelTop + b;
        const float inBot = kBrowserBottom - b;
        const float hdrBot = kTitleBandBottom - kFibHair;
        const float hdrMid = (inTop + hdrBot) * 0.5f;

        int first = s_row / kPageRows * kPageRows;
        const float hlLeft43 = inL43 + kFibGap;
        const float hlRight43 = kScrollLeft - kFibTight;
        for (int r = 0; r < kPageRows && first + r < s_count; r++)
        {
            float y = kRowTop + r * kRowPitch;
            if (first + r == s_row)
            {
                drawGoldBar(hlLeft43, hlRight43, y, kRowBoxH, 8.0f);
            }
            else
            {
                drawSlantBar(hlLeft43, hlRight43, y, kRowBoxH, false, kLabRowSlateTop, kLabRowSlateBot,
                             kLabRowSlateEdge);
            }
        }
        drawScrollBar(s_count, first > s_count - kVisibleRows ? (s_count > kVisibleRows ? s_count - kVisibleRows : 0)
                                                               : first);

        // Glossy shoulder pills + page readout in the header's right half.
        bool wiiPage = (s_controllerType == gfPadType::WIIMOTE || s_controllerType == gfPadType::NUNCHUK);
        const char* prevG = wiiPage ? "-" : "L";
        const char* nextG = wiiPage ? "+" : "R";
        float pScale = scaleForCap(kTypeMeta);
        float pillH = 18.0f;
        float pillW = gw(22.0f);
        float pillY = hdrMid - pillH * 0.5f;
        float pTextY = textYForCap(hdrMid - kTypeMeta * 0.5f, pScale);
        float lPillX = cx(412.0f);
        float rPillX = cx(inR43 - kFibPad - 22.0f);
        drawGlossyPillBadge(lPillX, pillY, pillW, pillH, kLabBtnLightTop, kLabBtnLightBot);
        drawGlossyPillBadge(rPillX, pillY, pillW, pillH, kLabBtnLightTop, kLabBtnLightBot);
        gfx::text(lPillX + (pillW - gfx::textWidth(prevG, pScale)) * 0.5f, pTextY, prevG, kLabBtnDarkText, pScale);
        gfx::text(rPillX + (pillW - gfx::textWidth(nextG, pScale)) * 0.5f, pTextY, nextG, kLabBtnDarkText, pScale);

        char page[20];
        int pages = (s_count + kPageRows - 1) / kPageRows;
        snprintf(page, sizeof(page), "Page %d/%d", s_count > 0 ? s_row / kPageRows + 1 : 1, pages > 0 ? pages : 1);
        float pgW = gfx::textWidth(page, pScale);
        float slotCenter = (lPillX + pillW + rPillX) * 0.5f;
        gfx::text(slotCenter - pgW * 0.5f, pTextY, page, kLabDescText, pScale);

        float capOffset = (kRowBoxH - kRowCap) * 0.5f;
        char text[48];
        for (int r = 0; r < kPageRows && first + r < s_count; r++)
        {
            bool selected = first + r == s_row;
            const Entry& e = s_entries[first + r];
            float y = textYForCap(kRowTop + r * kRowPitch + capOffset, scale);
            title(text, sizeof(text), e);
            gfx::Rgba nameColor = selected ? (e.status == kReady ? kLabGoldInk : kLabGoldInkMuted)
                                           : (e.status == kReady ? kText : kTextDisabled);
            gfx::Rgba rightColor = selected ? kLabGoldInkMuted : kLabMutedText;
            char name[sizeof(text)];
            snprintf(name, sizeof(name), "%s", text);
            const char* right = text;
            if (e.status == kReady)
            {
                // The date tells exports of the same pair apart.
                char length[16];
                lengthText(length, sizeof(length), e.info.frames);
                snprintf(text, sizeof(text), "%02u-%02u %02u:%02u   %s", e.info.month, e.info.day, e.info.hour,
                         e.info.minute, length);
            }
            else
            {
                right = e.status == kUnread ? "" : (e.status == kNewer ? "Newer version"
                                                                       : (e.status == kOlder ? "Older version" : "Damaged"));
            }
            gfx::textRight(kSlotRows + r, cx(kRowBoxRight) - 4.0f, y, right, rightColor, scale);
            // Keep the matchup name clear of the cursor triangle and date/duration column.
            float nameX = cx(hlLeft43 + 26.0f);
            float room = cx(kRowBoxRight) - 4.0f - gfx::textWidth(right, scale) - 9.0f - nameX;
            drawFitText(nameX, y, name, nameColor, scale, room > 0.0f ? room : 1.0f);
        }
        drawDescription();

        // Glossy Brawl footer control badges matching drill_list.cpp & osd_quick.cpp.
        float fy = kBrowserFootTop + kFibHair + (inBot - (kBrowserFootTop + kFibHair)) * 0.5f;
        float fx = cx(inL43 + kFibPad);
        const char* okG = s_controllerType == gfPadType::WIIMOTE ? "2" : "A";
        const char* backG = s_controllerType == gfPadType::WIIMOTE ? "1" : "B";
        if (s_confirm)
        {
            fx = drawFooterBadge(fx, fy, 18.0f, okG, "Delete", kBtnB, kBadgeRound);
            drawFooterBadge(fx, fy, 18.0f, backG, "Cancel", kBtnLight, kBadgeRound);
        }
        else
        {
            const char* delG = s_controllerType == gfPadType::WIIMOTE ? "A"
                                                                      : (s_controllerType == gfPadType::NUNCHUK ? "C" : "X");
            const char* pgG = wiiPage ? "-/+" : "L/R";
            fx = drawFooterBadge(fx, fy, 18.0f, okG, "Load", kBtnA, kBadgeRound);
            fx = drawFooterBadge(fx, fy, 18.0f, delG, "Delete", kBtnLight, kBadgeRound);
            fx = drawFooterBadge(fx, fy, 18.0f, backG, "Back", kBtnB, kBadgeRound);
            drawFooterBadge(fx, fy, 18.0f, pgG, "Page", kBtnLight, 0);
        }
    }

    // ---- open / close ----

    void open(int port, u32 controllerType)
    {
        s_open = true;
        s_port = port;
        s_controllerType = controllerType;
        s_confirm = false;
        s_message = NULL;
        s_waitRelease = true;
        s_repeat[0].held = s_repeat[1].held = 0;
        s_row = 0;
        gfx::forgetWidths();
        if (s_phase == kIdle || s_phase == kHeaders)
        {
            if (s_job.busy())
            {
                int status = 0;
                int r = s_job.wait(kWaitMs, status);
                storage::freeReadBuffer(s_head, r, status);
                s_jobStale = r == storage::kBusy;
            }
            else
            {
                s_jobStale = false;
                storage::freeReadBuffer(s_head, storage::kOk, 0);
            }
            refresh();
        }
        TM_LOG("browser: opened for port %d\n", port + 1);
    }

    bool isOpen()
    {
        return s_open;
    }

    void attach()
    {
        s_open = false;
        s_controllerType = gfPadType::GCC;
        s_count = 0;
        s_phase = kIdle;
        s_jobStale = s_job.busy();
        s_head = NULL;
        s_temp = NULL;
        s_startNow = false;
    }

    void detach()
    {
        if (s_job.busy())
        {
            int status = 0;
            int r = s_job.wait(kWaitMs, status);
            if (!s_jobStale)
            {
                if (s_phase == kHeaders)
                {
                    storage::freeReadBuffer(s_head, r, status);
                }
                else if (s_phase == kVerify)
                {
                    storage::freeReadBuffer(s_temp, r, status);
                }
            }
            s_jobStale = r == storage::kBusy;
        }
        else
        {
            s_jobStale = false;
        }
        storage::freeReadBuffer(s_head, storage::kOk, 0);
        freeTemp();
        s_open = false;
        s_phase = kIdle;
    }
}
}
}
