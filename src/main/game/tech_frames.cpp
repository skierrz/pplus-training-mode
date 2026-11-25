#include "main/game/tech_frames.h"

namespace tm {
namespace game {
    // P+'s first distinguishable frame of a tech per fighter kind (ftKind):
    // the first frame (1 = the tech's first) on which Passive, PassiveStandF
    // and PassiveStandB can be told apart. -1 means none derived (neither tech
    // invisibility nor Tech Sound applies).
    // From scripts/tech_frames.py on P+ 3.2.1's pf/fighter: the three techs
    // count as distinct when their bones diverge by an average of 16% of the
    // fighter's height. Techs play one animation frame per game frame
    // (measured: Mario, Fox, Lucario, Mewtwo).
    static const s8 kDistinguishable[kTechDistinguishableCount] = {
         8, // 0x00 Mario
        10, // 0x01 Donkey Kong
         8, // 0x02 Link
         8, // 0x03 Samus
         6, // 0x04 Yoshi
         3, // 0x05 Kirby
         4, // 0x06 Fox
         7, // 0x07 Pikachu
         8, // 0x08 Luigi
        10, // 0x09 Captain Falcon
         5, // 0x0A Ness
         4, // 0x0B Bowser
         6, // 0x0C Peach
         3, // 0x0D Zelda
         4, // 0x0E Sheik
         8, // 0x0F Popo
         8, // 0x10 Nana
         7, // 0x11 Marth
         3, // 0x12 Mr. Game & Watch
         5, // 0x13 Falco
         3, // 0x14 Ganondorf
         8, // 0x15 Wario
        10, // 0x16 Meta Knight
        10, // 0x17 Pit
         9, // 0x18 Zero Suit Samus
         7, // 0x19 Olimar
         7, // 0x1A Lucas
         3, // 0x1B Diddy Kong
        -1, // 0x1C Pokemon Trainer: not a fighter of its own here
         5, // 0x1D Charizard
         2, // 0x1E Squirtle
         2, // 0x1F Ivysaur
         7, // 0x20 King Dedede
         7, // 0x21 Lucario
         9, // 0x22 Ike
         9, // 0x23 R.O.B.
        -1, // 0x24 (none): not a fighter of its own here
         4, // 0x25 Jigglypuff
         8, // 0x26 Mewtwo
         7, // 0x27 Roy
        -1, // 0x28 Dr. Mario: not a fighter of its own here
         8, // 0x29 Toon Link
        -1, // 0x2A (none): not a fighter of its own here
        -1, // 0x2B (none): not a fighter of its own here
         4, // 0x2C Wolf
         5, // 0x2D Knuckles
        10, // 0x2E Snake
         5, // 0x2F Sonic
         3, // 0x30 Giga Bowser
        -1, // 0x31 Wario-Man: not a fighter of its own here
    };

    int techDistinguishable(int kind)
    {
        return kind >= 0 && kind < kTechDistinguishableCount ? kDistinguishable[kind] : kTechDistinguishableMissing;
    }
}
}
