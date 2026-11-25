#pragma once

// Stand-in for the game headers' <types.h> in the PC build (tests/host/).
// The same names and the same sizes as on the console, where `long` is 32
// bits too.
typedef unsigned long long u64;
typedef signed long long s64;

typedef unsigned long u32;
typedef signed long s32;

typedef unsigned short u16;
typedef signed short s16;

typedef unsigned char u8;
typedef signed char s8;

typedef float f32;
typedef double f64;

typedef char host_u32_is_32_bits[sizeof(u32) == 4 ? 1 : -1];
typedef char host_u64_is_64_bits[sizeof(u64) == 8 ? 1 : -1];
