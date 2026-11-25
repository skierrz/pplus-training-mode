#pragma once

#include <types.h>

// Stand-in for the console's clock in the PC build: the time is whatever a
// test set (host/card.h, setClock).
extern "C" {
typedef struct OSCalendarTime {
    s32 sec;
    s32 min;
    s32 hour;
    s32 mday;
    s32 month; // 0-11
    s32 year;
    s32 wday;
    s32 yday;
    s32 msec;
    s32 usec;
} OSCalendarTime;

s64 OSGetTime(void);
u32 OSGetTick(void);
void OSTicksToCalendarTime(s64 time, OSCalendarTime* cal);
}

#define OS_MSEC_TO_TICKS(ms) (static_cast<s64>(ms))
#define OS_TICKS_TO_USEC(ticks) (static_cast<u32>(ticks))
