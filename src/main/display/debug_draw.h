#pragma once

#include <types.h>

class Fighter;

// Hitboxes, hurtboxes, ECBs and stage collision drawn by Project+'s own
// debug-draw routines (its Gecko codes, already in RAM), called from the
// render hook for the chosen fighters only. Project+'s flag bytes are never
// written. See debug_draw.cpp.
namespace tm {
namespace display {
    // Checks the routines' code once (per attach) and logs the result.
    void debugDrawCheck();
    bool boxesAvailable();
    bool collisionAvailable();

    // Brackets the draws of one frame: main camera, Project+'s hitbox colour
    // words kept to be put back.
    void debugDrawBegin();
    void debugDrawEnd();

    // Hurtboxes, hitboxes and shields of one fighter.
    void drawBoxes(Fighter* fighter);
    // The fighter's ECB.
    void drawEcb(Fighter* fighter);
    // Every stage collision line in use.
    void drawStageCollision();
}
}
