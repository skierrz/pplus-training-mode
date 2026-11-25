#include "host/fakes.h"

#include <gf/gf_application.h>
#include <gf/gf_file_io_manager.h>
#include <gf/gf_heap_manager.h>
#include <gf/gf_memory_pool.h>
#include <gf/gf_pad_system.h>
#include <gf/gf_scene.h>
#include <gm/gm_global.h>
#include <if/if_mngr.h>
#include <ip/ip_padconfig.h>

#include "host/canvas.h"
#include "host/card.h"
#include "host/log.h"
#include "host/mem.h"
#include "host/script.h"
#include "host/test.h"
#include "main/cpu/view.h"
#include "main/game/fighters.h"
#include "main/game/motion.h"
#include "main/record/record.h"
#include "main/services.h"
#include "main/ui/osd.h"
#include "menu/services.h"
#include "shared/game/heap.h"
#include "shared/game/mem.h"
#include "shared/game/scene.h"
#include "shared/interface.h"

GameGlobal* g_GameGlobal = NULL;
IfMngr* g_IfMngr = NULL;
gfPadSystem* g_gfPadSystem = NULL;
gfApplication* g_gfApplication = NULL;
gfFileIOManager* g_gfFileIOManager = NULL;
static ipPadConfig* s_padConfig = NULL;

namespace host {
namespace fakes {
    static FakeFighter s_fighters[tm::game::kMaxTracked];
    static tm::game::Tracked s_tracked[tm::game::kMaxTracked];
    static int s_fighterCount = 0;
    static s32 s_trackerFrame = 0;
    static u8* s_block = NULL;
    static tm::game::MatchState s_match;
    static Recording s_recording;

    enum { kHeaps = 0x48, kMaxBlocks = 32, kHeapRoom = 0x00400000 };
    enum { kGuard = 256, kGuardByte = 0xA5 };
    struct Block {
        void* at;
        u32 bytes;
        int heap;
        bool live;
    };
    static Block s_blocks[kMaxBlocks];
    static u32 s_heapRoom[kHeaps];
    static u32* s_heapHandles = NULL;

    static int s_osdCount = 0;
    static int s_osdPlayer = -1;
    static int s_osdId = -1;
    static char s_osdText[128];

    enum { kMaxCacheOps = 64 };
    static CacheOp s_cacheOps[kMaxCacheOps];
    static int s_cacheOpCount = 0;
    static bool s_padConnected[tm::iface::kPadCount];
    static u32 s_padType[tm::iface::kPadCount];
    static bool s_trainingSelect = false;
    static gfSceneManager* s_sceneManager = NULL;
    static u8* s_sequence = NULL;
    static gfScene* s_scene = NULL;

    void reset()
    {
        memset(s_fighters, 0, sizeof(s_fighters));
        memset(s_tracked, 0, sizeof(s_tracked));
        s_fighterCount = 0;
        s_trackerFrame = 0;
        s_block = NULL;
        s_osdCount = 0;
        s_osdPlayer = -1;
        s_osdId = -1;
        s_osdText[0] = '\0';
        s_cacheOpCount = 0;
        Script::reset();
        card::reset();
        canvas::reset();
        s_match.running = true;
        s_match.training = true;
        s_match.paused = false;
        memset(&s_recording, 0, sizeof(s_recording));
        s_recording.who.hmn = 0;
        s_recording.who.cpu = 1;
        memset(s_blocks, 0, sizeof(s_blocks));
        s_heapHandles = static_cast<u32*>(mem::alloc(kHeaps * sizeof(u32), 4));
        for (int i = 0; i < kHeaps; i++)
        {
            s_heapRoom[i] = kHeapRoom;
            s_heapHandles[i] = static_cast<u32>(i);
        }
        g_GameGlobal = mem::make<GameGlobal>();
        g_GameGlobal->m_modeMelee = mem::make<gmGlobalModeMelee>();
        g_GameGlobal->m_selCharData = mem::make<gmSelCharData>();
        g_GameGlobal->m_selStageData = mem::alloc(0x40, 8);
        g_GameGlobal->m_setRule = mem::make<gmSetRule>();
        g_gfPadSystem = mem::make<gfPadSystem>();
        g_gfApplication = mem::make<gfApplication>();
        g_gfFileIOManager = mem::make<gfFileIOManager>();
        s_padConfig = mem::make<ipPadConfig>();
        for (int i = 0; i < 4; i++)
        {
            s_padConfig->raw[0xB5 + i] = static_cast<u8>(i);
        }
        s_trainingSelect = false;
        s_sceneManager = mem::make<gfSceneManager>();
        s_sequence = static_cast<u8*>(mem::alloc(0x40, 8));
        s_scene = reinterpret_cast<gfScene*>(mem::alloc(0x40, 8));
        s_sceneManager->m_currentSequence = s_sequence;
        s_sceneManager->processStep = 1;
        *reinterpret_cast<u32*>(s_sequence + 0x8) = 4; // kStateCharacterSelect
        for (int i = 0; i < tm::iface::kPadCount; i++)
        {
            s_padConnected[i] = (i == 0);
            s_padType[i] = i < 4 ? static_cast<u32>(gfPadType::GCC) : static_cast<u32>(gfPadType::WIIMOTE);
        }
    }

    FakeFighter& addFighter()
    {
        if (s_fighterCount >= tm::game::kMaxTracked)
        {
            printf("host: more fake fighters than tracker slots\n");
            exit(3);
        }
        int slot = s_fighterCount++;
        FakeFighter& f = s_fighters[slot];
        f.fighter = mem::make<Fighter>();
        f.accesser = mem::make<soModuleAccesser>();
        f.modules = mem::make<soModuleEnumeration>();
        f.motion = mem::make<soMotionModule>();
        f.situation = mem::make<soSituationModule>();
        f.stop = mem::make<soStopModule>();
        f.statusModule = mem::make<soStatusModule>();
        f.work = mem::make<soWorkManageModule>();
        f.fighter->m_moduleAccesser = f.accesser;
        f.accesser->m_enumerationStart = f.modules;
        f.modules->m_motionModule = f.motion;
        f.modules->m_situationModule = f.situation;
        f.modules->m_stopModule = f.stop;
        f.modules->m_statusModule = f.statusModule;
        f.modules->m_workManageModule = f.work;
        f.motion->m_rate = 1.0f;
        f.situation->m_kind = Situation_Ground;
        f.motionKind = -1;
        tm::game::Tracked& t = s_tracked[slot];
        t.used = true;
        t.playerNo = static_cast<s8>(slot);
        t.entryId = slot;
        t.fighter = f.fighter;
        t.lastDealt.other = -1;
        t.lastDealt.frame = tm::game::kNoFrame;
        t.lastReceived.other = -1;
        t.lastReceived.frame = tm::game::kNoFrame;
        f.tracked = &t;
        return f;
    }

    void setTrackerFrame(s32 frame)
    {
        s_trackerFrame = frame;
    }

    u8* block()
    {
        if (s_block == NULL)
        {
            s_block = static_cast<u8*>(mem::alloc(tm::iface::kSettingsBlockSize, 32));
        }
        return s_block;
    }

    tm::game::MatchState& match()
    {
        return s_match;
    }

    Recording& recording()
    {
        return s_recording;
    }

    void setHeapRoom(int heap, u32 bytes)
    {
        s_heapRoom[heap] = bytes;
    }

    int heapBlocks(int heap)
    {
        int n = 0;
        for (int i = 0; i < kMaxBlocks; i++)
        {
            n += s_blocks[i].live && s_blocks[i].heap == heap ? 1 : 0;
        }
        return n;
    }

    void freeByGame(void* block)
    {
        for (int i = 0; i < kMaxBlocks; i++)
        {
            if (s_blocks[i].at == block && block != NULL && s_blocks[i].live)
            {
                host_game_free(block);
                return;
            }
        }
        test::fail(__FILE__, __LINE__,
                   "readSDFile short read called gfHeapManager::free on a non-heap buffer %p", block);
    }

    static u32 heapUsed(int heap)
    {
        u32 n = 0;
        for (int i = 0; i < kMaxBlocks; i++)
        {
            n += s_blocks[i].live && s_blocks[i].heap == heap ? s_blocks[i].bytes : 0;
        }
        return n;
    }

    // gmGlobalModeMelee +0x98 and gmSelCharData +0xB8: 0x5C bytes a player,
    // +0 the character, +1 the state, +5 and +6 the costume; the stage is
    // the 16 bits at gmGlobalModeMelee +0x1A.
    void setPlayer(int player, u8 kind, u8 state, u8 colorFile, u8 color)
    {
        u8* sets[2] = {g_GameGlobal->m_modeMelee->raw + 0x98, g_GameGlobal->m_selCharData->raw + 0xB8};
        for (int i = 0; i < 2; i++)
        {
            u8* p = sets[i] + 0x5C * player;
            p[0] = kind;
            p[1] = state;
            p[5] = colorFile;
            p[6] = color;
        }
    }

    void setStage(u16 stage)
    {
        memcpy(g_GameGlobal->m_modeMelee->raw + 0x1A, &stage, sizeof(stage));
    }

    void setHazardOff(bool off)
    {
        u8& flags = g_GameGlobal->m_modeMelee->raw[0x29];
        if (off)
        {
            flags |= 0x20;
        }
        else
        {
            flags &= static_cast<u8>(~0x20);
        }
    }

    int osdCount()
    {
        return s_osdCount;
    }
    const char* osdText()
    {
        return s_osdText;
    }
    int osdPlayer()
    {
        return s_osdPlayer;
    }
    int osdId()
    {
        return s_osdId;
    }

    static FakeFighter* byFighter(const Fighter* fighter)
    {
        for (int i = 0; i < s_fighterCount; i++)
        {
            if (s_fighters[i].fighter == fighter)
            {
                return &s_fighters[i];
            }
        }
        return NULL;
    }

    static FakeFighter* byModules(const soModuleEnumeration* m)
    {
        for (int i = 0; i < s_fighterCount; i++)
        {
            if (s_fighters[i].modules == m)
            {
                return &s_fighters[i];
            }
        }
        return NULL;
    }

    static void logLine(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        log::write(format, args);
        va_end(args);
    }

    static u8* settingsBlock()
    {
        return block();
    }
}
}

// The game's free: the block leaves its heap's count (its memory is the
// arena's, which goes when the next test begins). A block freed twice in
// one test fails it. A pointer this test never got is let pass: the mod's
// static state outlives a test, and one that stopped early can leave a
// block of its own behind for the next one's clean-up.
void host_game_free(void* block)
{
    for (int i = 0; i < host::fakes::kMaxBlocks; i++)
    {
        host::fakes::Block& b = host::fakes::s_blocks[i];
        if (b.at == block && block != NULL)
        {
            if (!b.live)
            {
                host::test::fail(__FILE__, __LINE__, "a block of %lu bytes from game heap %d was freed twice", b.bytes, b.heap);
            }
            for (unsigned long g = 0; b.live && g < host::fakes::kGuard; g++)
            {
                if (static_cast<unsigned char*>(b.at)[b.bytes + g] != host::fakes::kGuardByte)
                {
                    host::test::fail(__FILE__, __LINE__, "a block of %lu bytes from game heap %d was written %lu bytes past its end",
                                     b.bytes, b.heap, g + 1);
                    break;
                }
            }
            b.live = false;
            return;
        }
    }
}

// A heap's handle sits in the arena so game::isRam(getHeap(h)) matches the console.
void* gfHeapManager::getHeap(Heaps::HeapType heapType)
{
    using namespace host::fakes;
    int id = static_cast<int>(heapType);
    if (s_heapHandles == NULL || id <= 0 || id >= kHeaps)
    {
        return NULL;
    }
    return &s_heapHandles[id];
}

void* gfMemoryPool::alloc(void* heapAddr, size_t size, int align)
{
    using namespace host::fakes;
    if (s_heapHandles == NULL || heapAddr < s_heapHandles || heapAddr >= s_heapHandles + kHeaps)
    {
        return NULL;
    }
    int heap = static_cast<int>(*static_cast<const u32*>(heapAddr));
    if (heap <= 0 || heap >= kHeaps || size > s_heapRoom[heap] - heapUsed(heap))
    {
        return NULL;
    }
    for (int i = 0; i < kMaxBlocks; i++)
    {
        if (!s_blocks[i].live)
        {
            // kGuard bytes after the block, checked when it is freed: a
            // write past its end fails the test.
            s_blocks[i].at = host::mem::alloc(static_cast<u32>(size) + kGuard, static_cast<u32>(align));
            if (s_blocks[i].at != NULL)
            {
                memset(static_cast<u8*>(s_blocks[i].at) + size, kGuardByte, kGuard);
            }
            s_blocks[i].bytes = static_cast<u32>(size);
            s_blocks[i].heap = heap;
            s_blocks[i].live = true;
            return s_blocks[i].at;
        }
    }
    printf("host: more than %d blocks from game heaps at once\n", static_cast<int>(kMaxBlocks));
    exit(3);
}

namespace tm {
namespace main {
    // src/main/module.cpp: the stub's services while attached.
    const iface::Services& services()
    {
        static const iface::Services kServices = {host::fakes::logLine, host::fakes::settingsBlock};
        return kServices;
    }
}

namespace menu {
    // src/menu/module.cpp: the same services for the menu module.
    const iface::Services& services()
    {
        return main::services();
    }
}

namespace cpu {
    // src/main/cpu/view.cpp.
    int fighterKind(int)
    {
        return -1;
    }
}

namespace game {
    // src/main/game/fighters.cpp. fighterModules is the same code; the three
    // readers there take the values from the game's work and status modules.
    const soModuleEnumeration* fighterModules(Fighter* fighter)
    {
        if (!isRam(fighter))
        {
            return NULL;
        }
        soModuleAccesser* accesser = fighter->m_moduleAccesser;
        if (!isRam(accesser) || !isRam(accesser->m_enumerationStart))
        {
            return NULL;
        }
        return accesser->m_enumerationStart;
    }

    int fighterStatus(const soModuleEnumeration* m)
    {
        host::fakes::FakeFighter* f = host::fakes::byModules(m);
        return f != NULL ? f->status : -1;
    }

    int fighterHitstun(const soModuleEnumeration* m)
    {
        host::fakes::FakeFighter* f = host::fakes::byModules(m);
        return f != NULL && f->hitstun > 0 ? f->hitstun : 0;
    }

    int fighterShieldStun(const soModuleEnumeration* m, int status)
    {
        host::fakes::FakeFighter* f = host::fakes::byModules(m);
        return f != NULL && status == kStatusGuardDamage && f->shieldStun > 0 ? f->shieldStun : 0;
    }

    // src/main/game/fighters.cpp: no match fighters here.
    bool findFighter(bool, FighterRef&)
    {
        return false;
    }

    // src/main/game/motion.cpp.
    bool readMotion(Fighter* fighter, MotionInfo& out)
    {
        host::fakes::FakeFighter* f = host::fakes::byFighter(fighter);
        if (f == NULL)
        {
            return false;
        }
        out.kind = f->motionKind;
        out.frame = static_cast<int>(f->motion->m_frame);
        out.length = f->motionLength;
        out.name = NULL;
        return true;
    }

    // src/main/game/match.cpp.
    MatchState matchState()
    {
        return host::fakes::s_match;
    }

    // src/shared/game/heap.cpp reads a pool's header and free list; here a
    // heap is one free block of what its room leaves.
    bool heapUsage(u32 id, HeapUsage& out)
    {
        using namespace host::fakes;
        memset(&out, 0, sizeof(out));
        if (id == 0 || id >= kHeaps)
        {
            return false;
        }
        out.used = heapUsed(static_cast<int>(id));
        out.allocated = static_cast<u32>(heapBlocks(static_cast<int>(id)));
        out.freeBytes = s_heapRoom[id] - out.used;
        out.largest = out.freeBytes;
        out.freeBlocks = 1;
        return true;
    }

    const char* heapName(u32)
    {
        return "heap";
    }

    // src/main/game/tracker.cpp.
    s32 trackerFrame()
    {
        return host::fakes::s_trackerFrame;
    }

    const Tracked* tracked(int i)
    {
        return i >= 0 && i < kMaxTracked && host::fakes::s_tracked[i].used ? &host::fakes::s_tracked[i] : NULL;
    }

    // src/shared/game/scene.cpp.
    gfScene* currentScene()
    {
        return host::fakes::s_trainingSelect ? host::fakes::s_scene : NULL;
    }

    bool sceneIs(const gfScene* scene, const char* name)
    {
        return host::fakes::s_trainingSelect && scene == host::fakes::s_scene && name != NULL
               && strcmp(name, "scSelctCharacter") == 0;
    }

    bool inTraining()
    {
        return host::fakes::s_trainingSelect;
    }

    bool trainingGameMode()
    {
        return host::fakes::s_match.training;
    }
}

namespace record {
    // src/main/record/start_state.cpp and record.cpp.
    bool liteGet(LitePlacement out[2], Players& who)
    {
        const host::fakes::Recording& r = host::fakes::s_recording;
        if (!r.positions)
        {
            return false;
        }
        out[0] = r.place[0];
        out[1] = r.place[1];
        who = r.who;
        return true;
    }

    bool positionsSaved()
    {
        return host::fakes::s_recording.positions;
    }

    void importStart(const LitePlacement place[2])
    {
        host::fakes::Recording& r = host::fakes::s_recording;
        r.imported[0] = place[0];
        r.imported[1] = place[1];
        r.imports++;
    }

    bool storageGaveUp()
    {
        return false;
    }

    bool cpuRowsAvailable()
    {
        return host::fakes::s_recording.rows;
    }

    bool loopRowsAvailable()
    {
        return host::fakes::s_recording.rows;
    }

    bool mirrorRowAvailable()
    {
        return host::fakes::s_recording.rows;
    }

    bool modeRowsAvailable()
    {
        return host::fakes::s_recording.rows;
    }

    bool slotHasInputs(int, int)
    {
        return host::fakes::s_recording.rows;
    }
}

namespace osd {
    // src/main/ui/osd.cpp: the message is kept for the test, not drawn.
    Message* post(int player, int id, gfx::Rgba, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(host::fakes::s_osdText, sizeof(host::fakes::s_osdText), format, args);
        va_end(args);
        host::fakes::s_osdCount++;
        host::fakes::s_osdPlayer = player;
        host::fakes::s_osdId = id;
        return NULL;
    }
}
}

namespace host {
namespace fakes {
    void clearCacheOps()
    {
        s_cacheOpCount = 0;
    }

    int cacheOpCount()
    {
        return s_cacheOpCount;
    }

    const CacheOp* cacheOps()
    {
        return s_cacheOps;
    }

    static void recordCacheOp(bool icache, void* at, u32 bytes)
    {
        if (s_cacheOpCount < kMaxCacheOps)
        {
            s_cacheOps[s_cacheOpCount].icache = icache;
            s_cacheOps[s_cacheOpCount].addr = reinterpret_cast<u32>(at);
            s_cacheOps[s_cacheOpCount].bytes = bytes;
            s_cacheOpCount++;
        }
    }

    void setMenuPort(u32 port)
    {
        *reinterpret_cast<u32*>(g_GameGlobal->m_setRule->raw + 0x24) = port;
    }

    void setPadConnected(int port, bool connected)
    {
        if (port >= 0 && port < tm::iface::kPadCount)
        {
            s_padConnected[port] = connected;
        }
    }

    void setPadType(int port, u32 controllerType)
    {
        if (port >= 0 && port < tm::iface::kPadCount)
        {
            s_padType[port] = controllerType;
        }
    }

    void setPlayerPort(int playerNo, u8 port)
    {
        if (s_padConfig != NULL && playerNo >= 0 && playerNo < 4)
        {
            s_padConfig->raw[0xB5 + playerNo] = port;
        }
    }

    void setTrainingSelectRunning(bool running)
    {
        s_trainingSelect = running;
        if (running && s_sceneManager != NULL && s_sequence != NULL)
        {
            *reinterpret_cast<u32*>(s_sequence + 0x8) = 4; // kStateCharacterSelect
            s_sceneManager->processStep = 1;             // kProcessRunning
            s_sceneManager->unk1 = 0;
        }
    }
}
}

gfSceneManager* gfSceneManager::getInstance()
{
    return host::fakes::s_trainingSelect ? host::fakes::s_sceneManager : NULL;
}

ipPadConfig* ipPadConfig::getInstance()
{
    return s_padConfig;
}

void gfPadSystem::getSysPadStatus(int padNum, gfPadStatus* dest)
{
    memset(dest, 0, sizeof(*dest));
    bool ok = padNum >= 0 && padNum < tm::iface::kPadCount && host::fakes::s_padConnected[padNum];
    dest->m_error = static_cast<s8>(ok ? gfPadError::NONE : gfPadError::NO_CONTROLLER);
    dest->m_controllerType = (padNum >= 0 && padNum < tm::iface::kPadCount)
                                 ? static_cast<gfPadType::PadType>(host::fakes::s_padType[padNum])
                                 : gfPadType::GCC;
}

void DCFlushRange(void* at, u32 bytes)
{
    host::fakes::recordCacheOp(false, at, bytes);
}

void ICInvalidateRange(void* at, u32 bytes)
{
    host::fakes::recordCacheOp(true, at, bytes);
}

void OSReport(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    host::log::write(format, args);
    va_end(args);
}
