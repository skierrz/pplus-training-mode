#pragma once

#include <types.h>

// Recorded inputs: 6 slots per side (HMN, CPU), up to 3600 frames each,
// indexed by the game step since the positions were restored. One block for
// all twelve and the run log (runLog below), allocated when the match starts
// in a heap that is empty then and freed when the match module detaches.
//
// A frame is what the fighter acts on at the end of its controller update:
// the held buttons and main stick of its input object (aiInput +0xC, +0x10,
// +0x14) and P+'s analog variables, filled from the controller in that
// update and read by its shield and C-stick code (instance work floats
// 34-37: C-stick x, y, analog L, R; int 77: a new L/R press). 8 bytes: the
// buttons (every bit the game uses is below 0x10000), one byte per float
// indexing a per-slot table of the exact values seen, and the press state.
// Replaying the same float bits keeps playback exact (a stick quantised to
// 1/127 already moves a walking fighter differently). A table holds 256
// values; past that the nearest is stored and the frame counted lossy.
namespace tm {
namespace record {
    enum {
        kSides = 2,
        kSlotCount = 6,
        kMaxFrames = 3600,
        kPaletteSize = 256,
        kNoFrame = 0xFFFF
    };

    struct Input {
        u32 buttons;
        float x, y;   // main stick
        float cx, cy; // Project+ C-stick (relative)
        float l, r;   // Project+ analog L and R
        u8 press;     // Project+ new L/R press state
    };

    struct Packed {
        u16 buttons;
        u8 x, y, cx, cy;
        u8 lr;    // index of the (l, r) pair
        u8 press;
    };

    enum Table { kTableX, kTableY, kTableCx, kTableCy, kTables };

    struct Slot {
        u16 start;   // first frame with inputs (kNoFrame when empty)
        u16 end;     // one past the last frame with inputs (0 when empty)
        u16 count[kTables];
        u16 lrCount;
        u16 reserved;
        u32 lossy;   // frames stored approximately
        u32 table[kTables][kPaletteSize]; // float bits
        u32 lr[kPaletteSize][2];
        Packed frames[kMaxFrames];
    };

    // Takes the block from the heap if that heap holds nothing; false (and
    // logged) if it is not there, not empty or too small.
    // `quiet`: no log line when the heap is not free yet.
    bool storageAllocate(bool quiet = false);
    // Frees the block and logs whether the heap is back where it was.
    void storageFree();
    bool storageReady();
    u32 storageBytes();
    const void* storageAddress();

    Slot* slotAt(int side, int index); // NULL without storage
    bool slotEmpty(const Slot& s);
    void slotClear(Slot& s);
    // Stores frame `t`; the slot then ends at t + 1 (later frames dropped).
    void slotWrite(Slot& s, int t, const Input& in);
    // False outside start..end.
#if defined(TM_MODULE_EXTRA) && defined(__MWERKS__)
    bool x_slotRead(const Slot& s, int t, Input& out);
#ifndef TM_EXTRA_THUNK_IMPL
    static inline bool slotRead(const Slot& s, int t, Input& out) { return x_slotRead(s, t, out); }
#else
    bool slotRead(const Slot& s, int t, Input& out);
#endif
#else
    bool slotRead(const Slot& s, int t, Input& out);
#endif
    void slotCopy(Slot& to, const Slot& from);
    // Drops the first `frames` frames: frame `frames` becomes frame 0.
    void slotDropFront(Slot& s, int frames);
    // Alter Inputs: replaces frame `t` (start..end - 1) with `in`. A value
    // new to a full table is stored as the table's nearest value (the frame
    // counts as lossy) and false is returned; no other frame changes.
    bool slotEdit(Slot& s, int t, const Input& in);

    // The run log: both sides' inputs of every frame of the current run,
    // packed into one Slot per side (8 bytes/frame plus exact float-bit
    // palettes; Frame Decrement replays it). NULL without storage.
    Slot* runLogSlot(int side);
    void runLogClear(int side);
    void runLogWrite(int side, int t, const Input& in);
    bool runLogRead(int side, int t, Input& out);
}
}
