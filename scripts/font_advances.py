"""Writes tests/host/data/font_advances.h from a memory dump of the running game.

usage: font_advances.py DUMP [-o FILE]

DUMP is the prefix of a PREFIX.mem1 / PREFIX.mem2 pair (scripts/memdump.py),
or one file holding MEM1 alone. Any dump taken once the game is up will do:
the font is the resident one the mod prints with (src/main/gfx/text.cpp).

What is read: the font object at 0x80497E44 keeps a pointer to its resource
at +0x10 (an NW4R "RFNU" font) and to the resource's FINF block at +0x14.
FINF names the line height, the width table (CWDH blocks: three bytes per
glyph, the third is the advance) and the character map (CMAP blocks: direct,
table or scan). The game's ms::CharWriter::Print (0x8006FE50) moves the
cursor by that advance times the horizontal scale and nothing else; its
GetFontHeight (0x8006FCF8) is the font's height times the vertical scale.

The output holds only what `make check-host`'s text checks need: the advance
of each printable ASCII character and the font's height. No glyph image is
read or written.
"""
import os
import struct
import sys

sys.dont_write_bytecode = True

FONT_OBJECT = 0x80497E44
MEM1 = 0x80000000
MEM2 = 0x90000000
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tests", "host", "data",
                   "font_advances.h")


def reader(dump):
    if os.path.isfile(dump):
        mem1_path, mem2_path = dump, None
    else:
        mem1_path, mem2_path = dump + ".mem1", dump + ".mem2"
    with open(mem1_path, "rb") as f:
        mem1 = f.read()
    mem2 = b""
    if mem2_path and os.path.isfile(mem2_path):
        with open(mem2_path, "rb") as f:
            mem2 = f.read()

    def read(address, size):
        for base, image in ((MEM1, mem1), (MEM2, mem2)):
            if base <= address and address + size <= base + len(image):
                return image[address - base:address - base + size]
        sys.exit("font_advances: address %#x is not in the dump" % address)

    return read


def font(read):
    """(height, {character code: advance}) of the resident font."""
    resource, finf = struct.unpack(">II", read(FONT_OBJECT + 0x10, 8))
    if read(resource, 4) != b"RFNU":
        sys.exit("font_advances: no font resource at %#x (was the dump taken before the game was up?)" % resource)
    # FINF: type, line feed, the index used for a character with no glyph,
    # the default widths, encoding, then pointers to the glyph, width and map
    # blocks, and the height, width and ascent.
    (_type, _linefeed, alternate, d_left, d_glyph, d_advance, _encoding, _glyphs, widths_at, maps_at, height, _width,
     _ascent) = struct.unpack(">BbHbBbBIIIBBB", read(finf, 23))
    advances = {}
    at = widths_at
    while at:
        first, last, following = struct.unpack(">HHI", read(at, 8))
        for index in range(first, last + 1):
            advances[index] = struct.unpack(">bBb", read(at + 8 + 3 * (index - first), 3))[2]
        at = following

    def glyph_index(code):
        at = maps_at
        while at:
            first, last, method, _reserved, following = struct.unpack(">HHHHI", read(at, 12))
            if first <= code <= last:
                if method == 0:  # direct: an offset
                    return struct.unpack(">H", read(at + 12, 2))[0] + code - first
                if method == 1:  # table
                    return struct.unpack(">H", read(at + 12 + 2 * (code - first), 2))[0]
                if method == 2:  # scan: pairs of code and index
                    count = struct.unpack(">H", read(at + 12, 2))[0]
                    for k in range(count):
                        c, index = struct.unpack(">HH", read(at + 14 + 4 * k, 4))
                        if c == code:
                            return index
                    return 0xFFFF
            at = following
        return 0xFFFF

    out = {}
    for code in range(0x20, 0x7F):
        index = glyph_index(code)
        if index == 0xFFFF:
            sys.exit("font_advances: the font has no glyph for %r" % chr(code))
        out[code] = advances.get(index, d_advance)
    return height, advances.get(alternate, d_advance), out


def main(argv):
    out_path = OUT
    if "-o" in argv:
        at = argv.index("-o")
        out_path = argv[at + 1]
        argv = argv[:at] + argv[at + 2:]
    if len(argv) != 1:
        print(__doc__)
        return 2
    height, alternate, advances = font(reader(argv[0]))
    lines = [
        "// The game's resident font as the mod prints with it (src/main/gfx/text.cpp):",
        "// how far each printable ASCII character moves the cursor at scale 1, and",
        "// the height of a line at scale 1. Read by the PC tests' stand-in for the",
        "// game's character writer (tests/host/host/canvas.cpp).",
        "//",
        "// Written by scripts/font_advances.py from a memory dump of the running",
        "// game (Project+ 3.2.1 on RSBE01 rev 2); do not edit by hand. To write it",
        "// again:",
        "//     py -3 scripts/font_advances.py PREFIX-OF-A-DUMP",
        "// (the dump is the kind scripts/memdump.py makes; docs/testing.md, \"Text",
        "// that fits\").",
        "#pragma once",
        "",
        "namespace host {",
        "namespace font {",
        "    enum {",
        "        kFirst = 0x20,",
        "        kLast = 0x7E,",
        "        kHeight = %d," % height,
        "        // What a character the font has no glyph for moves the cursor by.",
        "        kAlternate = %d" % alternate,
        "    };",
        "    static const signed char kAdvance[kLast - kFirst + 1] = {",
    ]
    for row in range(0x20, 0x7F, 8):
        codes = [c for c in range(row, min(row + 8, 0x7F))]
        cells = ", ".join("%2d" % advances[c] for c in codes)
        shown = "".join(chr(c) for c in codes).replace("\\", "(backslash)")
        lines.append("        %s%s // %s" % (cells, "," if codes[-1] != 0x7E else " ", shown))
    lines += ["    };", "}", "}", ""]
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("font_advances: wrote %s (height %d, %d characters)" % (os.path.relpath(out_path), height, len(advances)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
