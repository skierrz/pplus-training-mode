#pragma once

#include <types.h>

// Brawl's stOperatorRuleTraining (0x80962c74, called every step at
// 0x80962b90) reassigns each CPU's gmPlayerInitData.m_controllerNo back to
// its connected preferred port at 0x80962d0c..0x80962d48 before copying
// m_controllerNo - 1 into aiInput at 0x80962f54..0x80962f84. Branching
// 0x80962d0c -> 0x80962e7c while either CPU Options > Controlled By or
// Recording owns the CPU's controller port keeps that assignment intact.
namespace tm {
namespace game {
    enum ControllerPatchOwner {
        kControllerOwnerControlledBy = 1u << 0,
        kControllerOwnerRecord = 1u << 1
    };

    // Idempotent: patches 0x80962d0c iff the owner mask is nonzero and the
    // game mode is Training; restores when the mask is 0 (without the
    // Training gate, only if the site holds our skip branch).
    void trainingControllerSuppress(u32 owner, bool on);
    // Current owner mask (0 when neither Controlled By nor Record owns it).
    u32 trainingControllerOwners();
    // Clears the owner mask and restores 0x80962d0c (module detach).
    void trainingControllerRestore();
}
}
