#include "menu/bests_file.h"

#include <memory.h>

#include "main/storage/file_io.h"
#include "main/storage/format.h"
#include "shared/log.h"

namespace tm {
namespace menu {
namespace bests {
    using drills::Session;
    using storage::FileHeader;

    enum {
        kMagic = 0x544D4246, // "TMBF"
        kFormat = 1,
        kRevision = 1,
        kHeadBytes = sizeof(FileHeader) + 4,
        // Room for 128 slots: more drills only add slots, the format stays.
        kCap = 320,
        kWaitMs = 3000
    };

    // After the FileHeader: the slot count, rules revision, then one u16 per drill id.
    struct Head {
        u16 slotCount;
        u16 revision; // 0 in older files; raised when a drill's scoring rules change
    };

    static const char* const kNames[2] = {"bests-a.bin", "bests-b.bin"};
    static storage::Job s_job;
    // One file read or written; a second for the newest file while both are read.
    static u8 s_file[kCap] __attribute__((aligned(32)));
    static u8 s_newest[kCap] __attribute__((aligned(32)));

    enum Phase { kIdle, kFolder, kWrite };
    static int s_phase = kIdle;
    static Session* s_session = NULL;
    static u8 s_pendingSlot = 0;
    static u32 s_pendingSequence = 0;
    static u32 s_pendingSum = 0;
    static u32 s_pendingSize = 0;

    static u32 sumOf(const u16* best)
    {
        return storage::checksum(best, drills::kBestSlots * sizeof(u16));
    }

    // Blocking read of one of the two files into s_file via a heap buffer: a storage::Result.
    static int readFile(int slot, u32& sequence)
    {
        int status = 0;
        sequence = 0;
        u8* buf = storage::allocReadBuffer(kCap);
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
        r = storage::checkHeader(buf, sizeof(FileHeader), kMagic, kFormat, false);
        if (r != storage::kOk)
        {
            if (r == storage::kNewer)
            {
                sequence = h->sequence;
            }
            storage::freeReadBuffer(buf, storage::kOk, 0);
            return r;
        }
        u32 size = h->size;
        if (size > kCap)
        {
            storage::freeReadBuffer(buf, storage::kOk, 0);
            return storage::kTooBig;
        }
        if (size < kHeadBytes)
        {
            storage::freeReadBuffer(buf, storage::kOk, 0);
            return storage::kCorrupt;
        }
        if (!s_job.read(kNames[slot], buf, size))
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
        r = storage::checkHeader(buf, size, kMagic, kFormat, true);
        const Head* head = reinterpret_cast<const Head*>(buf + sizeof(FileHeader));
        if (r == storage::kOk && kHeadBytes + head->slotCount * sizeof(u16) > size)
        {
            r = storage::kCorrupt;
        }
        if (r == storage::kOk)
        {
            sequence = reinterpret_cast<const FileHeader*>(buf)->sequence;
            memcpy(s_file, buf, size);
        }
        storage::freeReadBuffer(buf, storage::kOk, 0);
        return r;
    }

    void load(Session& s)
    {
        int results[2];
        u32 seqs[2];
        int newest = -1;
        for (int slot = 0; slot < 2; slot++)
        {
            results[slot] = readFile(slot, seqs[slot]);
            bool usable = results[slot] == storage::kOk || results[slot] == storage::kNewer;
            if (usable && (newest < 0 || storage::sequenceNewer(seqs[slot], seqs[newest])))
            {
                newest = slot;
                if (results[slot] == storage::kOk)
                {
                    memcpy(s_newest, s_file, kCap);
                }
            }
        }
        TM_LOG("bests: files: a %s (%u), b %s (%u)\n", storage::resultText(results[0]), seqs[0],
               storage::resultText(results[1]), seqs[1]);
        // As the settings: the next write goes to the other file than the
        // newest usable one; without one, a damaged a.bin is replaced first.
        bool onlyBDamaged = results[1] == storage::kCorrupt && results[0] != storage::kCorrupt;
        s.cardSlot = static_cast<u8>(newest >= 0 ? newest : (onlyBDamaged ? 0 : 1));
        s.cardSequence = newest >= 0 ? seqs[newest] : 0;
        s.cardNewer = newest >= 0 && results[newest] == storage::kNewer ? 1 : 0;
        // What the card holds, as the session lays it out ("changed" compares it).
        u16 card[drills::kBestSlots];
        memset(card, 0, sizeof(card));
        if (newest >= 0 && results[newest] == storage::kOk)
        {
            const Head* head = reinterpret_cast<const Head*>(s_newest + sizeof(FileHeader));
            const u16* best = reinterpret_cast<const u16*>(s_newest + kHeadBytes);
            u32 n = head->slotCount < drills::kBestSlots ? head->slotCount : drills::kBestSlots;
            memcpy(card, best, n * sizeof(u16));
        }
        s.cardSum = sumOf(card);
        int kept = 0;
        for (int id = 1; id < drills::kBestSlots; id++)
        {
            s.best[id] = drills::betterBest(id, s.best[id], card[id]);
            kept += card[id] != 0 ? 1 : 0;
        }
        if (newest >= 0 && results[newest] == storage::kOk)
        {
            TM_LOG("bests: loaded from %s (save %u): %d drills with a best\n", kNames[newest], seqs[newest], kept);
        }
    }

    static void finished(bool ok, int status)
    {
        if (ok && s_session != NULL)
        {
            s_session->cardSlot = s_pendingSlot;
            s_session->cardSequence = s_pendingSequence;
            s_session->cardSum = s_pendingSum;
            TM_LOG("bests: saved to %s (save %u, %u bytes)\n", kNames[s_pendingSlot], s_pendingSequence,
                   s_pendingSize);
        }
        else
        {
            TM_LOG("bests: save to %s failed: status %d\n", kNames[s_pendingSlot], status);
        }
        s_phase = kIdle;
    }

    void save(Session& s)
    {
        s_session = &s;
        u32 sum = sumOf(s.best);
        if (s_phase != kIdle || sum == s.cardSum)
        {
            return;
        }
        if (s.cardNewer != 0)
        {
            TM_LOG("bests: not saved: newer format on the card\n");
            return;
        }
        // A locked card keeps the bests for the session; nothing to tell
        // the player on the character select.
        int w = storage::writable();
        if (w != storage::kOk)
        {
            TM_LOG("bests: not saved: %s\n", storage::resultText(w));
            return;
        }
        memset(s_file, 0, sizeof(s_file));
        FileHeader* h = reinterpret_cast<FileHeader*>(s_file);
        Head* head = reinterpret_cast<Head*>(s_file + sizeof(FileHeader));
        u32 size = kHeadBytes + sizeof(s.best);
        head->slotCount = drills::kBestSlots;
        head->revision = kRevision;
        memcpy(s_file + kHeadBytes, s.best, sizeof(s.best));
        h->magic = kMagic;
        h->format = kFormat;
        h->headerSize = sizeof(FileHeader);
        h->size = size;
        h->sequence = storage::nextSequence(s.cardSequence);
        h->checksum = storage::checksum(s_file + storage::kChecksumFrom, size - storage::kChecksumFrom);
        s_pendingSlot = static_cast<u8>(s.cardSlot ^ 1);
        s_pendingSequence = h->sequence;
        s_pendingSum = sum;
        s_pendingSize = size;
        // The folder first (it exists after the first save; creating it again is harmless).
        s_phase = kFolder;
        if (!s_job.makeFolder())
        {
            finished(false, 0);
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
            if (!s_job.write(kNames[s_pendingSlot], s_file, s_pendingSize))
            {
                finished(false, 0);
            }
            return;
        }
        finished(result == storage::kOk, status);
    }

    void detach()
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
            r = s_job.write(kNames[s_pendingSlot], s_file, s_pendingSize) ? s_job.wait(kWaitMs, status)
                                                                           : storage::kBusy;
        }
        finished(r == storage::kOk, status);
        s_session = NULL;
    }
}
}
}
