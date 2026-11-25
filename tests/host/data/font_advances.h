// The game's resident font as the mod prints with it (src/main/gfx/text.cpp):
// how far each printable ASCII character moves the cursor at scale 1, and
// the height of a line at scale 1. Read by the PC tests' stand-in for the
// game's character writer (tests/host/host/canvas.cpp).
//
// Written by scripts/font_advances.py from a memory dump of the running
// game (Project+ 3.2.1 on RSBE01 rev 2); do not edit by hand. To write it
// again:
//     py -3 scripts/font_advances.py PREFIX-OF-A-DUMP
// (the dump is the kind scripts/memdump.py makes; docs/testing.md, "Text
// that fits").
#pragma once

namespace host {
namespace font {
    enum {
        kFirst = 0x20,
        kLast = 0x7E,
        kHeight = 24,
        // What a character the font has no glyph for moves the cursor by.
        kAlternate = 19
    };
    static const signed char kAdvance[kLast - kFirst + 1] = {
         7,  8, 10, 13, 13, 19, 14,  7, //  !"#$%&'
         8,  8, 10, 13,  7, 13,  7,  8, // ()*+,-./
        13, 13, 13, 13, 13, 13, 13, 13, // 01234567
        13, 13,  8,  8, 13, 13, 13, 13, // 89:;<=>?
        17, 14, 14, 14, 15, 13, 12, 15, // @ABCDEFG
        15,  6,  9, 13, 12, 18, 15, 15, // HIJKLMNO
        13, 15, 14, 13, 12, 15, 13, 18, // PQRSTUVW
        13, 12, 13,  8,  8,  8, 13, 10, // XYZ[(backslash)]^_
         8, 11, 12, 11, 12, 12,  7, 12, // `abcdefg
        12,  6,  6, 11,  6, 17, 12, 12, // hijklmno
        12, 12,  8, 11,  7, 12, 11, 15, // pqrstuvw
        10, 11, 10,  8,  7,  8, 10  // xyz{|}~
    };

    // Brawl's chunky 32x32 resident display font (0x80497DA4 -> 0x81009580),
    // dumped directly from live P+ 3.2.1 MEM1 RFNU CWDH/CMAP. -1 denotes the
    // 16 ASCII characters absent from its CMAP (which fall back to kAdvance).
    enum {
        kDisplayHeight = 32,
        kDisplayAlternate = 27
    };
    static const signed char kDisplayAdvance[kLast - kFirst + 1] = {
        11, 14, 20, -1, -1, 30, 28, 14, //  !"#$%&'
        -1, -1, -1, 24, 11, 18, 11, 20, // ()*+,-./
        31, 19, 30, 31, 30, 31, 31, 28, // 01234567
        31, 31, 11, 11, -1, 28, -1, 29, // 89:;<=>?
        31, 31, 30, 31, 30, 25, 26, 31, // @ABCDEFG
        30, 15, 30, 29, 26, 31, 29, 31, // HIJKLMNO
        29, 31, 30, 31, 24, 31, 31, 31, // PQRSTUVW
        30, 30, 27, -1, -1, -1, -1, -1, // XYZ[(backslash)]^_
        -1, 27, 27, 27, 27, 27, 23, 27, // `abcdefg
        28, 15, 20, 28, 15, 31, 28, 27, // hijklmno
        28, 27, 27, 28, 19, 27, 27, 31, // pqrstuvw
        29, 28, 28, -1, -1, -1, 19      // xyz{|}~
    };
}
}
