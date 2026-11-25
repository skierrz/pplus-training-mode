#pragma once

class Fighter;

// P+-native Hitbox Color Modes (Property and Priority) rendered via P+ 3.2.1's
// resident soCollision::getShape (0x80740A3C) and kBubble (0x80541FA0), with
// strict GX camera/state restoration before returning to debug_draw.cpp.
namespace tm {
namespace extra {
    void hitboxColorsAttach();
    void hitboxColorsDetach();
    bool drawColoredHitboxes(Fighter* fighter);
}
}
