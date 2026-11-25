#pragma once

#include <types.h>

// Stand-in for the game's FAT library in the PC build: the three calls the
// mod lists and sizes files with, answered from the card of host/card.h.
// The records keep the fields the mod reads.
extern "C" {
typedef struct FAStat {
    int filesize;
    s16 accessDate;
    s16 modifiedTime;
    s16 modifiedDate;
    s16 createdTime;
    s16 createdDate;
    u16 unk;
    char flags;
} FAStat;

typedef struct FAEntryInfo {
    char _0x0[0x224];
    s16 modifiedTime;
    s16 modifiedDate;
    int size;
    char _flag;
    char shortname[0xd];
    char name[0x38];
    char unk[0x1e2];
} FAEntryInfo;

int FAFstat(const char* filepath, FAStat* st);
int FAFsfirst(const char* pattern, int filter, FAEntryInfo* ret);
int FAFsnext(FAEntryInfo* ret);
}
