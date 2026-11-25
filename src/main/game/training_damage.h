#pragma once

#include "main/game/match.h"

// Brawl's Training sets every CPU's damage to its pause menu's "CPU Damage"
// each time that pause closes, which threw away the lab's CPU Percent and
// any damage dealt on every visit to the lab menu through that pause. Here
// the CPU keeps its damage across the pause unless the player changed CPU
// Damage during it (then Training's own write is what was asked for).
namespace tm {
namespace game {
    // Once per game step (the game pads), with the match state.
    void trainingDamageStep(const MatchState& m);
    // Puts the game's calls back (module detach).
    void trainingDamageRestore();
}
}
