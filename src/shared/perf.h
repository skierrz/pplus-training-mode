#pragma once

#include <types.h>

// Time measurements, built only with `make PERF=1` (docs/testing.md,
// "Measuring time"). Each module keeps a block of sections in its own data;
// a section adds up the emulated console's timebase ticks (OSGetTick) of
// every call, keeps the longest call, and the most it took in one frame.
// Some sections count things instead of ticks (rectangles, characters, log
// lines). The block's address and section names are logged once; the test
// script (scripts/perf_tests.py) zeroes the block, plays, and reads it.
// In a normal build every macro here compiles to nothing.
#ifdef TM_PERF
#include <OS/OSTime.h>

namespace tm {
namespace perf {
    // The stub's sections: its hooks, whole (record and the CPU driver wrap
    // the modules' callbacks, so only the stub sees them all), and the frame.
    enum StubSection {
        kStubFrame,       // render hook to render hook: the frame's length
        kStubBusy,        // system pads hook (top of the main-loop pass) to the end of the render hook
        kStubRender,      // the render hook's body, the module's render callback included
        kStubPadsSystem,  // the system pads hook
        kStubPadsGame,    // the game pads hook
        kStubFighterPad,  // the fighter pad hook (once per player per step)
        kStubFighterInput, // the fighter input hook (once per fighter per frame)
        // Where the pass's time goes between the hooks (the wait for the
        // video field is in one of them).
        kStubTopToGame,    // system pads hook to the first game pads hook of the pass
        kStubGameToRender, // last game pads hook to the render hook
        kStubRenderToTop,  // end of the render hook to the next system pads hook
        kStubSections
    };

    // The match module's sections (the extra module's work is timed where
    // the match module calls it: main/extra.cpp).
    enum MainSection {
        kTrackerHandOver, // the trackers' frame at the render call, its listeners included
        kTrackerStep,     // the same at the pads, while stepping
        kAdvantage,
        kFrameData,       // extra: frame data service
        kTechniques,
        kCharacterRng,    // extra
        kSavestateRender,
        kLabRender,       // lab::onRender whole
        kRender3d,        // display::render3d whole (hitboxes, collisions, DI)
        kDiDraw,          // extra
        kHeatmapDraw,     // extra
        kGrabVizDraw,     // extra
        kOsdDraw,
        kInfoDraw,
        kLedgeHud,
        kMenuDraw,
        kWatermark,
        kDebugOverlay,
        kRecordHud,
        kExtraRender,
        kMatchHud,
        kStorage,
        kPadsDamage,
        kPadsSavestate,
        kPadsLab,
        kPadsRecord,      // record's pads wrapper whole (the savestate and the lab inside it)
        kInputCpu,        // the CPU driver's fighter input
        kInputSavestate,
        kInputLab,
        kInputRecord,     // record's fighter input wrapper whole
        kFighterPadLab,
        kSaveCapture,     // a savestate save: snapshot::capture whole
        kSaveFingerprint,
        kSaveWaitGpu,
        kSaveWrite,
        kSaveCheck,
        kSaveMeasureV3,
        kLoadRestore,     // a savestate load: snapshot::restore whole
        kLoadFingerprint,
        kLoadCheck,
        kLoadWaitGpu,
        kLoadCopy,
        kLoadFlush,
        // Counts, not ticks.
        kCountRects,      // gfx::prepare calls (one per shape or text run)
        kCountTexts,      // gfx::text calls
        kCountChars,      // characters printed
        kCountWidths,     // gfx::textWidth calls (a text printed off screen to measure it)
        kCountLogs,       // TM_LOG lines
        kMainSections
    };

    struct Section {
        u32 calls;
        u32 ticks;      // or units, for a count
        u32 worstCall;
        u32 worstFrame;
        u32 frameTicks; // this frame so far
    };

    enum {
        kMagic = 0x50455246, // "PERF"
        kMaxSections = 64,
        // Block::control bits, set by the test script.
        kControlBypass = 1,    // the stub calls no module callback (the game alone)
        kControlCalibrate = 2, // the match module times fixed work once, then clears the bit
        // The match module reads addresses no console maps (NULL + 0x10,
        // 0x7E000010, past MEM1, past MEM2) once, then clears the bit: what
        // the emulator does with an invalid read, on purpose.
        kControlInvalidRead = 4,
        // The match and menu modules draw and check savestates as they did
        // before the cuts these measurements led to (no GX state kept, no
        // width or glyph caches, the whole-slot checksum): old and new from
        // one build.
        kControlLegacy = 8,
        // The match module damages the quick savestate's slot once, then
        // clears the bit: the low byte of `control` >> 8 says how (1: a
        // record's length, 2: a sampled data word).
        kControlDamageSlot = 16
    };

    struct Block {
        u32 magic;
        u32 frames;  // frameEnd calls since the block was zeroed
        u32 control;
        u32 count;   // sections
        // Stub only: ticks the render hook spends doing nothing each frame,
        // set by the test script to find how much time a frame has left.
        u32 burn;
        Section s[kMaxSections];
    };

#ifdef TM_MODULE_STUB
    extern const char* const kStubNames[kStubSections];
#endif
#ifdef TM_MODULE_MAIN
    extern const char* const kMainNames[kMainSections];
#endif

    void attach();
    void detach();
    Block* blockPtr();
    bool legacy();
#ifndef TM_MODULE_MAIN
    // This module's block.
    Block& block();
#endif

    void add(u32 section, u32 ticks);

    // Folds the frame's totals into each section's worst frame.
    void frameEnd();

    // Logs the block's address and the section names (`names`, count entries).
    void announce(const char* who, const char* const* names, u32 count);

    class Scope {
    public:
        Scope(u32 section);
        ~Scope();

    private:
        u32 m_section;
        u32 m_start;
    };
}
}

#define TM_PERF_SCOPE(section) ::tm::perf::Scope tmPerfScope_(::tm::perf::section)
#define TM_PERF_LEGACY() ::tm::perf::legacy()
#define TM_PERF_COUNT(section, n) ::tm::perf::add(::tm::perf::section, (n))
#else
#define TM_PERF_SCOPE(section)
#define TM_PERF_COUNT(section, n)
#define TM_PERF_LEGACY() false
#endif
