#pragma once

#include <types.h>

// The savestate's memory snapshot: which memory makes up the match state
// (the region set), copying it into a slot and back, and the checks that a
// slot still belongs to the running match (docs/internals.md, "The
// savestate's region set").
namespace tm {
namespace savestate {
    enum Result {
        kOk = 0,
        kErrNoRoom,      // the spare heaps are not empty (more fighters?)
        kErrTooBig,      // the set does not fit the slot
        kErrWalk,        // a heap's block chain is not what we expect
        kErrEmpty,       // nothing saved
        kErrMatch,       // the slot was saved in another match
        kErrLayout,      // heaps or modules moved since the save
        kErrResources,   // files were loaded or unloaded since the save
        kErrChecksum,    // the slot's bytes changed since the save
        kResultCount
    };

    struct SlotInfo {
        bool valid;
        u32 frame;     // the match's frame counter at the save
        u32 loopPass;  // our loop-top count at the save (for the age)
        u32 bytes;     // match memory in the slot
        u32 segments;
        u32 capacity;
        u32 setBytesV3; // what the set without fighter free space would be
        u32 saveTicks;  // OSGetTick() durations of the last save and load
        u32 loadTicks;
        u32 checkTicks; // of the last load's checksum pass
#ifdef TM_PERF
        u32 saveMaxIrqOffTicks; // peak single OSDisableInterrupts..OSRestoreInterrupts span
        u32 loadMaxIrqOffTicks;
#endif
    };

    // Two slots share the buffers: the quick state (D-pad, Save State) and
    // the recording's start state (Save Positions); capture() places them.
    enum { kSlotQuick = 0, kSlotRecording = 1, kSlotCount = 2 };

    // Takes the slot buffers if they are not held yet (only from heaps with
    // no block in them) and copies the region set into slot `which`.
    Result capture(int which, u32 loopPass);
    // Checks the slot against the running match and copies it back;
    // `beforeCopy` runs once every check has passed, before memory changes.
    Result restore(int which, void (*beforeCopy)());
    void invalidate(int which);
    // True if the last capture of the recording's state dropped the quick state.
    bool droppedOther();
    // Frees the slot buffers (match end, or the fighters' heaps are needed).
    void release();
    bool holdingBuffers();

    const SlotInfo& slotInfo(int which);

    // Buffers of ours inside heaps of the set (e.g. recorded inputs): never
    // copied, so a load leaves them as they are. Slot 0 is setOwnExclusion().
    enum { kMaxOwnExclusions = 4 };
    void setOwnExclusion(u32 addr, u32 len);
    void setOwnExclusionAt(u32 index, u32 addr, u32 len);
    const char* resultText(Result r);

    // Last thing after a stall of ours at the top of the main loop (a save,
    // a load, a park): the loop runs one game step, not a catch-up. Nothing
    // slow may follow it (the pad thread would count another sample): two
    // steps with no frame drawn between leave the fighters' models (hurtbox
    // and bone positions) slightly different from one step per frame.
    void forgetStall();

    // For the trace: allocated blocks and bytes of every heap in the set,
    // and the fighters' instance heaps byte for byte, each folded into a hash.
    u32 usageHash();
    u32 fighterHeapHash();

    // Checks the heap table holds the heaps the rules name; false disables
    // the feature for this match.
    bool layoutKnown();

#ifdef TM_PERF
    // Times fixed work (a counted loop, copies, checksums, a cache flush, a
    // log line) inside the slot buffers and logs it; needs the buffers held
    // (a state saved). Copies are in place, so the slots keep their bytes.
    void perfCalibrate();
    // Damages the quick state's slot the way a stray write would: `how` 1
    // makes its first record's length run past the slot, 2 flips a bit of
    // its first data word (one the load's check samples).
    void perfDamageSlot(u32 how);
#endif
}
}
