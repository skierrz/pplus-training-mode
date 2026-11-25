#pragma once

#include <types.h>

// Stand-in for OpenRVL's <OS/OSLink.h> in the PC build (tests/host/).
// Matches lib/BrawlHeaders/OpenRVL/include/revolution/OS/OSLink.h.
#ifdef __cplusplus
extern "C" {
#endif

#define OS_MODULE_VERSION 3
#define OS_SECTIONINFO_EXEC 0x1
#define OS_SECTIONINFO_OFFSET(offset) ((offset) & ~OS_SECTIONINFO_EXEC)

typedef enum {
    R_DOLPHIN_NOP = 201,
    R_DOLPHIN_SECTION = 202,
    R_DOLPHIN_END = 203,
    R_DOLPHIN_MRKREF = 204
} OSDolphinRelocType;

typedef u32 OSModuleID;
typedef struct OSModuleInfo OSModuleInfo;

typedef struct OSModuleLink {
    OSModuleInfo* next;
    OSModuleInfo* prev;
} OSModuleLink;

struct OSModuleInfo {
    OSModuleID id;
    OSModuleLink link;
    u32 numSections;
    u32 sectionInfoOffset;
    u32 nameOffset;
    u32 nameSize;
    u32 version;
};

typedef struct OSModuleHeader {
    OSModuleInfo info;
    u32 bssSize;
    u32 relOffset;
    u32 impOffset;
    u32 impSize;
    u8 prologSection;
    u8 epilogSection;
    u8 unresolvedSection;
    u8 bssSection;
    u32 prolog;
    u32 epilog;
    u32 unresolved;
    u32 align;
    u32 bssAlign;
    u32 fixSize;
} OSModuleHeader;

typedef struct OSSectionInfo {
    u32 offset;
    u32 size;
} OSSectionInfo;

typedef struct OSImportInfo {
    OSModuleID id;
    u32 offset;
} OSImportInfo;

typedef struct OSRel {
    u16 offset;
    u8 type;
    u8 section;
    u32 addend;
} OSRel;

int OSLinkFixed(OSModuleInfo* newModule, void* bss);
int OSUnlink(OSModuleInfo* oldModule);

#ifdef __cplusplus
}
#endif
