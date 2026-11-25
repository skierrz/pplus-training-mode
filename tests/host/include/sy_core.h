#pragma once

#include <types.h>

// Stand-in for Syriinge's header in the PC build: MOD_PATCH_DIR (the build's
// folder on the SD card, set up by host/card.h as a 0x18-byte char array in
// the arena so both sizeof(MOD_PATCH_DIR) and game::isRam(MOD_PATCH_DIR)
// match the console) and CoreApi::syInlineHook (for stub/hook.cpp tests).
typedef char HostModPatchDir[0x18];
extern HostModPatchDir* host_mod_patch_dir_ptr;
#define MOD_PATCH_DIR (*host_mod_patch_dir_ptr)

class CoreApi {
public:
    virtual ~CoreApi() {}
    virtual void syInlineHook(u32 address, const void* replacement) = 0;
};
