#pragma once

#include <types.h>

#include "main/settings.h"

// Brawl-native CSS Y: Quick Setup screen opened from the Boot Menu (Y on
// GC/Classic, Z on Nunchuk, -+A on Sideways Wii Remote). Shows 7 high-signal
// rows + an All OSDs sub-view, patches {kAreaGeneral, kAreaDisplay, kAreaOsd}
// (plus existing kAreaCpu techTrackerOsd) in settings-a/b.bin with zero clobber
// of other areas, supports reading Custom 1..3 OSD presets via kAreaOsdPresets,
// and updates the live Settings block when initialized.
namespace tm {
namespace menu {
namespace osdQuick {
    struct Input {
        u32 held;
        u32 pressed;
        s8 stickX, stickY;
        u32 type; // gfPadType::PadType
    };

    struct QuickSetupData {
        main::GeneralSettings general;
        main::DisplaySettings display;
        main::OsdSettings osd;
        s8 techTrackerOsd; // 0/1 when present, -1 to leave kAreaCpu untouched
    };

    // Populates defaults matching in-match Lab pages (general / display / osd).
    void defaultOsdSettings(main::OsdSettings& out);
    void defaultQuickSetupData(QuickSetupData& out);

    // Pure host-testable in-place Quick Setup patcher:
    // - When `inFile != NULL` (valid layout-2 settings file), copies every other
    //   area verbatim and updates/inserts kAreaGeneral, kAreaDisplay, kAreaOsd
    //   (plus updating existing kAreaCpu techTrackerOsd if present and >= 0).
    // - When `inFile == NULL`, writes a valid 3-area partial Quick Setup file
    //   ({kAreaGeneral, kAreaDisplay, kAreaOsd}) accepted silently by
    //   settings_file::isQuickSetupPartialFile().
    bool patchQuickSetupAreasInFile(const u8* inFile, u32 inSize, const QuickSetupData& data, u8* outFile,
                                    u32 outCap, u32 nextSeq, u32& outSize);

    // Single-area kAreaOsd patcher (kept for callers/tests updating OSDs alone).
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
