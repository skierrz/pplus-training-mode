#pragma once

#include <types.h>

#include "main/settings.h"

// Brawl-native quick OSD toggle screen opened from the Training Boot Menu
// (Y on GC/Classic, Z on Nunchuk, -+A on Sideways Wii Remote). Patches
// kAreaOsd in-place in settings-a/b.bin (preserving all other 15 areas with
// zero clobber) and updates the live Settings block immediately.
namespace tm {
namespace menu {
namespace osdQuick {
    struct Input {
        u32 held;
        u32 pressed;
        s8 stickX, stickY;
        u32 type; // gfPadType::PadType
    };

    // Populates `out` with clean v2 Essentials OSD defaults (matching osd_menu.cpp).
    void defaultOsdSettings(main::OsdSettings& out);

    // Pure host-testable in-place kAreaOsd patcher:
    // - When `inFile != NULL` (valid layout-2 settings file), copies every other
    //   area's exact AreaEntry & payload bytes verbatim (zero clobber of the
    //   other 15 areas!) and replaces/adds kAreaOsd with `osd`.
    // - When `inFile == NULL` (no settings file on SD yet), builds a clean
    //   layout-2 file holding kAreaOsd alone.
    // Writes the sealed file (sequence = `nextSeq`, valid checksum) into
    // `outFile` and sets `outSize`. Returns false if `outCap` is too small or
    // `inFile` is malformed.
    bool patchOsdAreaInFile(const u8* inFile, u32 inSize, const main::OsdSettings& osd, u8* outFile, u32 outCap,
                            u32 nextSeq, u32& outSize);

    void attach();
    void detach();
    void open(int port, u32 controllerType);
    bool isOpen();
    void setControllerType(u32 controllerType);
    void step(int port, const Input& in);
    void update();
    void draw();
}
}
}
