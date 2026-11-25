// The memory image itself (host/mem.h) and the PC build's stand-in for the
// mod's read helpers (tests/host/include/shared/game/mem.h), compared with
// the mod's own file.
#include "host/mem.h"
#include "host/test.h"
#include "shared/game/mem.h"

// The mod's own helpers, under another name. (Its <types.h> is already in.)
namespace the_mods_own {
#include "../../../src/shared/game/mem.h"
}

namespace real = the_mods_own::tm::game;
namespace standin = tm::game;

TEST(memory, the_stand_in_helpers_agree_with_the_mods_own)
{
    static const u32 kEdges[] = {0x00000000u, 0x00001000u, 0x7FFFFFFFu, 0x80000000u, 0x80000004u, 0x817FFFFFu, 0x81800000u,
                                 0x8FFFFFFFu, 0x90000000u, 0x93FFFFFFu, 0x94000000u, 0xC0000000u, 0xFFFFFFFFu};
    for (u32 i = 0; i < sizeof(kEdges) / sizeof(kEdges[0]); i++)
    {
        const void* p = reinterpret_cast<const void*>(kEdges[i]);
        if (real::isRam(p) != standin::isRam(p))
        {
            host::test::fail(__FILE__, __LINE__, "isRam(%08lx): the mod's own says %d, the stand-in %d", kEdges[i],
                             real::isRam(p), standin::isRam(p));
        }
    }
    // A word of the image reads the same through both.
    host::mem::setWord(0x80001234u, 0xCAFEF00Du);
    const void* base = reinterpret_cast<const void*>(0x80001000u);
    CHECK_EQ(0xCAFEF00Du, real::wordAt(base, 0x234));
    CHECK_EQ(0xCAFEF00Du, standin::wordAt(base, 0x234));
    CHECK(real::sameString("Wait1", "Wait1") && standin::sameString("Wait1", "Wait1"));
    CHECK(!real::sameString("Wait1", "Wait") && !standin::sameString("Wait1", "Wait"));

    // The two differences the stand-in is there for: the arena counts as
    // RAM, and a byte of the image is read where it is.
    void* object = host::mem::alloc(16);
    CHECK(standin::isRam(object));
    CHECK(!real::isRam(object));
    CHECK_EQ(0xCA, standin::byteAt(base, 0x234));
    CHECK_EQ(0x0D, standin::byteAt(base, 0x237));
    u8 bytes[4] = {1, 2, 3, 4};
    memcpy(object, bytes, 4);
    CHECK_EQ(1, standin::byteAt(object, 0));
    CHECK_EQ(4, standin::byteAt(object, 3));
}

// A big-endian word 0x11223344 at an address holds the bytes 11 22 33 44 at
// that address and the three after it, and the 16-bit values 0x1122 and
// 0x3344.
TEST(memory, bytes_and_halves_of_a_word_are_where_the_console_has_them)
{
    u32 a = 0x90000100u;
    host::mem::setWord(a, 0x11223344u);
    CHECK_EQ(0x11, host::mem::byte(a));
    CHECK_EQ(0x22, host::mem::byte(a + 1));
    CHECK_EQ(0x33, host::mem::byte(a + 2));
    CHECK_EQ(0x44, host::mem::byte(a + 3));
    CHECK_EQ(0x1122, host::mem::half(a));
    CHECK_EQ(0x3344, host::mem::half(a + 2));
    host::mem::setByte(a + 1, 0xAB);
    host::mem::setHalf(a + 2, 0xCDEF);
    CHECK_EQ(0x11ABCDEFu, host::mem::word(a));
    host::mem::setReal(a + 4, 1.875f);
    CHECK_EQ(0x3FF00000u, host::mem::word(a + 4));
    CHECK_EQ(1.875f, host::mem::real(a + 4));

    // The image is zero at the start of a test, at both ends of both
    // ranges, and can be written there.
    static const u32 kEnds[] = {0x80000000u, 0x817FFFFCu, 0x90000000u, 0x93FFFFFCu};
    for (int i = 0; i < 4; i++)
    {
        CHECK_EQ(0, host::mem::word(kEnds[i]));
        host::mem::setWord(kEnds[i], 0xFFFFFFFFu);
    }
    host::mem::clear();
    for (int i = 0; i < 4; i++)
    {
        CHECK_EQ(0, host::mem::word(kEnds[i]));
    }
    CHECK_EQ(0, host::mem::word(a));
}

// A dump is loaded with every word turned into the PC's order: the disc's
// id at the start of memory reads as the console has it.
TEST_DUMP(memory, a_dump_reads_as_the_console_has_it)
{
    CHECK_EQ(0x52534245u, host::mem::word(0x80000000u)); // "RSBE"
    CHECK_EQ('R', host::mem::byte(0x80000000u));
    CHECK_EQ('E', host::mem::byte(0x80000003u));
    // The bus clock in the OS globals, the value the mod's file code uses
    // for its waits: a plausible speed.
    u32 bus = host::mem::word(0x800000F8u);
    CHECK(bus > 100000000u && bus < 500000000u);
    NOTE("disc RSBE, bus clock %lu Hz", bus);
}
