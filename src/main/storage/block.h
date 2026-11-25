#pragma once

#include <types.h>

#include "shared/block_layout.h"
#include "shared/interface.h"
#include "shared/version.h"

// What the SD card storage keeps in the stub's settings block, between the
// match module's settings and the menu session (the block's map is
// shared/block_layout.h): the state of the settings file for this session, and an
// import the menu module hands to the next match. The block is zero at boot.
namespace tm {
namespace storage {
    enum {
        kStateOffset = block::kStorageState,
        kStateSize = 16,
        kHandoffOffset = block::kStorageHandoff,
        kHandoffSize = 48,
        kStateMagic = 0x544D5353,                  // "TMSS"
        kHandoffMagic = 0x544D494D,                // "TMIM"
        kHandoffNameMax = 40
    };

    // The settings file this session (see settings_file.cpp).
    struct State {
        u32 magic;       // kStateMagic once the file was read this session
        u8 slot;         // 0/1: which of the two files holds `sequence`
        u8 force;        // 1: write at the next menu close even if unchanged (Reset Lab Settings)
        u8 newerOnCard;  // 1: newest file on the card is a newer format (do not write this session)
        u8 reserved[1];
        u32 sequence;    // of the newest file on the card (0: none)
        u32 baseline;    // checksum of the saved view of the settings the card holds
    };

    // A recording the menu module's browser chose: the match module loads it
    // once the match it started runs.
    struct Handoff {
        u32 magic; // kHandoffMagic while pending
        u32 size;  // the file's size from its header
        char name[kHandoffNameMax];
    };

    TM_STATIC_ASSERT(sizeof(State) == kStateSize, tm_storage_state_size);
    TM_STATIC_ASSERT(sizeof(Handoff) == kHandoffSize, tm_storage_handoff_size);
    TM_STATIC_ASSERT(kStateOffset + kStateSize <= block::kStorageStateEnd, tm_storage_state_in_its_range);
    TM_STATIC_ASSERT(kHandoffOffset + kHandoffSize <= block::kStorageHandoffEnd, tm_storage_handoff_in_its_range);

    inline State* blockState(u8* block)
    {
        return block != NULL ? reinterpret_cast<State*>(block + kStateOffset) : NULL;
    }

    inline Handoff* blockHandoff(u8* block)
    {
        return block != NULL ? reinterpret_cast<Handoff*>(block + kHandoffOffset) : NULL;
    }
}
}
