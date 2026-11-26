#pragma once

// Put in front of every file of the PC build (tests/host/build.py passes it
// to the compiler as a forced include). It holds the three things the mod's
// sources need from the PC's compiler before their first line; the sources
// themselves are compiled as they are.
//
// 1. The C library has a struct named `tm` (<time.h>, which other standard
//    headers bring along) and the mod's namespace is `tm`. The library's gets
//    another name: every standard header the mod or the test layer uses is
//    included here once, under that name, and never again.
#define tm host_c_library_tm
#include <math.h>
#include <memory.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#undef tm

// 2. Spellings of the console's compiler: `__attribute__((aligned(32)))` on a
//    buffer (the game's file requests want it; nothing tested here does).
#define __attribute__(x)

// 3. `free` in the mod's sources is the game's: it returns a block to the
//    game heap it came from (gfMemoryPool::alloc, stand-in in
//    gf/gf_memory_pool.h). The test layer itself never calls the C
//    library's free.
void host_game_free(void* block);
bool host_game_block_live(const void* block);
#define free host_game_free

// The sources keep game addresses in pointers and in 32-bit integers
// (reinterpret_cast<u32>(pointer)), and the memory image is mapped at the
// console's own addresses (host/mem.h): the PC build must be 32-bit.
typedef char host_build_must_be_32_bit[sizeof(void*) == 4 ? 1 : -1];
