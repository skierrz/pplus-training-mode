#pragma once

#include <ft/fighter.h>
#include <types.h>

#include "main/game/match.h"
#include "main/game/tracker.h"
#include "main/record/start_state.h"

// What the tested sources call in sources that are not in the PC build (they
// call the game), answered from values a test sets. Every function defined
// in fakes.cpp for the mod's namespaces is one of these; each says there
// which source holds the real one.
namespace host {
namespace fakes {
    // Called by the runner before every test.
    void reset();

    // A fighter made of host objects: the Fighter, its module accesser and
    // its modules (so/so_module_accesser.h), all in the arena, plus what the
    // mod's own readers would read from the game for it
    // (src/main/game/fighters.cpp, motion.cpp, tracker.cpp).
    struct FakeFighter {
        Fighter* fighter;
        soModuleAccesser* accesser;
        soModuleEnumeration* modules;
        soMotionModule* motion;
        soSituationModule* situation;
        soStopModule* stop;
        soStatusModule* statusModule;
        soWorkManageModule* work;
        int status;       // game::fighterStatus
        int hitstun;      // game::fighterHitstun
        int shieldStun;   // game::fighterShieldStun (in GuardDamage)
        int motionKind;   // game::readMotion: the subaction
        int motionLength; // and its animation's frame count
        tm::game::Tracked* tracked; // its tracker slot (game::tracked)
    };

    // A new fake fighter in the next free tracker slot: standing on the
    // ground, animation rate 1, nothing else set.
    FakeFighter& addFighter();
    // The tracker's frame number (game::trackerFrame) and freeze state (game::frozen).
    void setTrackerFrame(s32 frame);
    void setFrozen(bool frozen);

    // The stub's settings block (iface::kSettingsBlockSize bytes, zero at
    // the start of a test), as main::services().settings() hands it out.
    u8* block();

    // The match as game::matchState() reports it (src/main/game/match.cpp);
    // a running Training match, not paused, at the start of a test.
    tm::game::MatchState& match();

    // The game's heaps (the stand-ins gf/gf_heap_manager.h and
    // gf/gf_memory_pool.h, and game::heapUsage of src/shared/game/heap.cpp):
    // each has room for 4 MiB unless a test says otherwise, and counts the
    // blocks taken from it and given back.
    void setHeapRoom(int heap, u32 bytes);
    int heapBlocks(int heap);
    // Simulates Brawl's gfHeapManager::free on a buffer inside readSDFile:
    // fails the test if `block` is not a live block from a game heap.
    void freeByGame(void* block);

    // The match setup the game keeps (gm/gm_global.h): the player's
    // character, its state (0 human, 1 CPU) and costume bytes, and the
    // stage.
    void setPlayer(int player, u8 kind, u8 state, u8 colorFile, u8 color);
    void setStage(u16 stage);
    void setHazardOff(bool off);

    // The recording's start state as src/main/record/ would give it
    // (record::liteGet, positionsSaved) and take it (record::importStart).
    struct Recording {
        bool positions; // positions are saved
        bool rows;      // the Recording page's rows that depend on a mode are open (record::*RowsAvailable)
        tm::record::LitePlacement place[2];
        tm::record::Players who;
        int imports;    // importStart calls
        tm::record::LitePlacement imported[2];
    };
    Recording& recording();

    // On-screen messages posted through osd::post since the test began.
    int osdCount();
    const char* osdText();   // the last one's text, "" if none
    int osdPlayer();
    int osdId();

    // DCFlushRange / ICInvalidateRange calls recorded since reset or clear.
    struct CacheOp {
        bool icache; // false = DCFlushRange, true = ICInvalidateRange
        u32 addr;
        u32 bytes;
    };
    void clearCacheOps();
    int cacheOpCount();
    const CacheOp* cacheOps();

    // gmSetRule+0x24 (menu controlling port) and gfPadSystem's per-port
    // connection status and controller type (ports 0-3 GameCube, 4-7 Wii).
    void setMenuPort(u32 port);
    void setPadConnected(int port, bool connected);
    void setPadType(int port, u32 controllerType);
    void setPlayerPort(int playerNo, u8 port);
    // Toggle a running Training character select for training::startMatch().
    void setTrainingSelectRunning(bool running);
}
}
