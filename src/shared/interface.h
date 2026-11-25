#pragma once

#include <types.h>

struct gfPadStatus;

// The contract between the stub (the plugin Syriinge loads) and the modules
// it loads for particular scenes (kind "match" for Training matches, with
// an optional "extra" module next to it, and kind "menu" for Training's
// character select). Both sides are built from this header; change kVersion whenever
// anything below changes layout or meaning.
namespace tm {
namespace iface {
    enum {
        kVersion = 5,
        kModuleMagic = 0x544D4D44, // "TMMD"
        // Module kinds, as tagged in the packed file's module table.
        kKindMatch = 0x4D544348, // "MTCH": loaded for match scenes
        kKindMenu = 0x4D454E55,  // "MENU": reserved for menu scenes
        // "XTRA": loaded after the match module for the same scene, into
        // another heap, and linked against it (it calls its functions and
        // reads its data directly): loaded only while the match module is,
        // unloaded first. It fills no callback slots; the match module calls
        // it through what it registers (main/extra.h).
        kKindExtra = 0x58545241,
        // Bytes in the stub's settings block (see Services::settings).
        kSettingsBlockSize = 768,
        // gfPadStatus records handed to Callbacks::pads: GameCube ports 1-4,
        // then Wii remotes 1-4, as gfPadSystem lays them out.
        kPadCount = 8
    };

    // Which of gfPadSystem's pad arrays a pads() call hands over.
    enum PadPhase {
        // The debug pads, from gfPadSystem::updateSystem once per main-loop
        // pass, just copied from the system pads. The menu pads and the
        // merged system, debug and menu pads are derived from them after
        // the callback returns.
        kPadsSystem = 0,
        // The game pads, from gfPadSystem::updateGame once per game step,
        // before they are merged and before the step's tasks run. On
        // Project+ the fighters do not read these (see Callbacks::fighterPad).
        kPadsGame = 1
    };

    // One fighter input object, handed to Callbacks::fighterInput once per
    // fighter (Ice Climbers: per climber) per simulated frame, after the
    // object's update() produced the frame's controls (the AI's or the pad's)
    // and before the fighter's controller reads them.
    struct FighterInput {
        void* input;  // the aiInput; its controls may be rewritten for the frame
        s32 entryId;  // aiInput+0x2C: ftManager entry id of the owning fighter (Nana: Popo's)
        u32 aiIndex;  // aiInput+0x42: index into aiMgr's per-slot tables (Nana: slot + 7)
    };

    // Provided by the stub; valid from attach() until detach() returns.
    struct Services {
        // OSReport-compatible; the caller adds the plugin tag.
        void (*log)(const char* format, ...);
        // The stub's settings block: kSettingsBlockSize bytes, 4-byte
        // aligned, zero at boot, kept by the stub across module loads. The
        // module owns its layout and must check the header it keeps there.
        u8* (*settings)();
    };

    // Filled by the loaded module in attach(). The stub calls each non-null slot
    // from the hook that owns it, and nulls them all before detach().
    struct Callbacks {
        // After each frame is drawn, before the copy to the framebuffer.
        // `renderCall` counts the stub's render-hook calls from 1.
        void (*render)(u32 renderCall);
        // `pads` points at kPadCount records the module may read and change;
        // `phase` is a PadPhase.
        void (*pads)(u32 phase, gfPadStatus* pads);
        // Once per player per game step, after updateGame (PadPhase
        // kPadsGame): `pad` is the game's own copy of port `port`'s pad, on
        // its way to being turned into that player's controls (buttons by
        // the controller settings, the stick). What the module changes in
        // it is what the fighter gets; no other reader sees the copy.
        void (*fighterPad)(u32 port, gfPadStatus* pad);
        // From the fighter-input hook in sora_melee (see FighterInput).
        void (*fighterInput)(const FighterInput* call);
        // From Fighter::processFixCamera in sora_melee, after the game's
        // per-frame setCursor / setNameCursor calls and before stOperatorInfo
        // updates the HUD cursors.
        void (*fighterFixCamera)(void* fighter);
    };

    struct Interface {
        u32 version; // kVersion of the stub
        u32 size;    // sizeof(Interface) as the stub was built
        Services services;
        Callbacks callbacks;
    };

    // What a module's _prolog returns. The stub checks magic, version and
    // kind before calling anything else in the module.
    struct ModuleInfo {
        u32 magic;   // kModuleMagic
        u32 version; // kVersion the module was built with
        u32 kind;    // kKind*, must match the packed file's table
        const char* buildVersion;
        // Fills iface->callbacks; false refuses the attach (the stub then unloads).
        bool (*attach)(Interface* iface);
        // Called after the stub has nulled the callbacks, before _epilog.
        void (*detach)();
    };
}
}
