#include "menu/game/training.h"

#include <gf/gf_pad_system.h>
#include <gf/gf_scene.h>
#include <gm/gm_global.h>

#include "shared/game/mem.h"
#include "shared/game/pad_bits.h"
#include "shared/game/scene.h"
#include "shared/interface.h"

namespace tm {
namespace menu {
namespace training {
    using game::isRam;

    // sqTraining's state machine (its object +0x8), as read from sqTraining::setNext
    // (0x806F1510) on 3.2.1 and observed over Training round trips:
    // 3 opens the character select and moves to 4; 4 runs when it is left
    // (exit code 1: on to the stage select, state 5, then 6); 6 runs when the
    // stage select is left: with exit code 1 (or 3, a Stage Builder stage) it
    // calls sqTraining::setupTraining (0x806F1948), which builds the match
    // from the selection data, and goes through the memory change (state 1,
    // +0xC = 7) into the match (8).
    enum {
        kSeqState = 0x8,
        kStateCharacterSelect = 4,
        kStateStageSelectLeft = 6,
        kExitToNext = 1,
        kProcessRunning = 1,
        kProcessExit = 2
    };

    // gmSelCharData: the character select's players (gmPlayerInitData, 0x5C
    // each) from +0xB8, written when Start is pressed, not at exit (observed:
    // values written before the exit were what the match used). +0x7 and
    // the words at +0x8 + 4n (n's icon in bits 8-15) went from 0 / 0x2800 to
    // 1 / the chosen icons then; reopened, the character select puts the
    // tokens back on those icons (observed).
    enum {
        kSelHasChoice = 0x7,
        kSelIcons = 0x8,
        kSelPlayers = 0xB8,
        kPlayerSize = 0x5C,
        kPlayerKind = 0x0,
        kPlayerState = 0x1, // 0 human, 1 CPU, 3 none
        kPlayerStocks = 0x4,
        kPlayerColorFile = 0x5,
        kPlayerColor = 0x6,
        kPlayerController = 0x7, // port + 1 for a human, 0 for the CPU
        kStateHuman = 0,
        kStateCpu = 1,
        // What the character select wrote for both players in Training.
        kStocks = 4,
        // gmSelStageData: stage kind at +0x22, m_subStageKind at +0x24, and
        // Project+'s isHazardOff byte at +0x25 (0 hazards ON, 1 hazards OFF;
        // P+ Random.asm:972-1031 writes +0x25 at 0x806C9030 and copies it into
        // gmGlobalModeMelee+0x29 bit 0x20 in sqTraining::setupTraining at
        // 0x806F1A0C; P+ StageFiles.asm:1281-1287, 1538 defaults explicit stage
        // picks to 0).
        kStageKind = 0x22,
        kSubStageKind = 0x24,
        kStageHazardOff = 0x25,
        // gmSetRule+0x24: the port Training's menus follow (word).
        kRulePort = 0x24
    };

    // Project+: the alternate-stage button word (0 = the normal stage) and
    // My Music's flag word (1 = leave the stage select for My Music, which
    // its hook in sqTraining::setNext acts on in state 6).
    static u16* const kAltStageButtons = reinterpret_cast<u16*>(0x800B9EA2);
    static const u32* const kMyMusicFlag = reinterpret_cast<const u32*>(0x80002810);

    struct Word {
        u32 address;
        u32 value;
    };
    static const Word kStartPath[] = {
        {0x806F1550, 0x3BC00001}, // li r30,1
        {0x806F1554, 0x39C00007}, // li r14,7
        {0x806F167C, 0x80110284}, // lwz r0,0x284(r17)    state 6: the exit code
        {0x806F1680, 0x2C000002}, // cmpwi r0,2
        {0x806F16C0, 0x93CF0008}, // stw r30,0x8(r15)     next state 1
        {0x806F16C4, 0x7DE37B78}, // mr r3,r15
        {0x806F16C8, 0x91CF000C}, // stw r14,0xC(r15)     then 7, the match
        {0x806F16CC, 0x9ACF0010}, // stb r22,0x10(r15)
        {0x806F16D0, 0x48000279}, // bl setupTraining
        {0x807038C4, 0x806F1628}, // jump table, state 4
        {0x807038CC, 0x806F167C}  // jump table, state 6
    };

    static u8* at(void* base, u32 offset)
    {
        return reinterpret_cast<u8*>(base) + offset;
    }

    static u32& word(void* base, u32 offset)
    {
        return *reinterpret_cast<u32*>(at(base, offset));
    }

    bool widescreen()
    {
        GameGlobal* global = g_GameGlobal;
        if (!isRam(global) || !isRam(global->m_record))
        {
            return false;
        }
        return global->m_record->m_menuData.m_isWidescreen;
    }

    int controllingPort()
    {
        GameGlobal* global = g_GameGlobal;
        gfPadSystem* pads = g_gfPadSystem;
        if (!isRam(global) || !isRam(global->m_setRule) || !isRam(pads))
        {
            return -1;
        }
        u32 port = word(global->m_setRule, kRulePort);
        gfPadStatus status;
        if (port < iface::kPadCount)
        {
            pads->getSysPadStatus(static_cast<int>(port), &status);
            if (game::padConnected(static_cast<int>(port), status))
            {
                return static_cast<int>(port);
            }
        }
        for (int i = 0; i < iface::kPadCount; i++)
        {
            pads->getSysPadStatus(i, &status);
            if (game::padConnected(i, status))
            {
                return i;
            }
        }
        return -1;
    }

    static gfSceneManager* manager()
    {
        gfSceneManager* m = gfSceneManager::getInstance();
        return isRam(m) ? m : NULL;
    }

    bool selectRunning()
    {
        gfSceneManager* m = manager();
        if (m == NULL || !isRam(m->m_currentSequence) || !game::inTraining()
            || !game::sceneIs(game::currentScene(), "scSelctCharacter"))
        {
            return false;
        }
        return word(m->m_currentSequence, kSeqState) == kStateCharacterSelect && m->processStep == kProcessRunning;
    }

    int exitCode()
    {
        gfSceneManager* m = manager();
        return m != NULL ? m->unk1 : -1;
    }

    const char* checkStartPath()
    {
        for (u32 i = 0; i < sizeof(kStartPath) / sizeof(kStartPath[0]); i++)
        {
            if (*reinterpret_cast<const u32*>(kStartPath[i].address) != kStartPath[i].value)
            {
                return "sqTraining is not the expected code";
            }
        }
        return NULL;
    }

    static void writePlayer(u8* p, u8 kind, u8 state, u8 controller)
    {
        p[kPlayerKind] = kind;
        p[kPlayerState] = state;
        p[kPlayerStocks] = kStocks;
        p[kPlayerColorFile] = 0;
        p[kPlayerColor] = 0;
        p[kPlayerController] = controller;
    }

    static void writeStage(void* selStageData, u16 stage, u16 altStage, bool hazardOff = false)
    {
        u8* data = reinterpret_cast<u8*>(selStageData);
        *reinterpret_cast<u16*>(data + kStageKind) = stage;
        data[kSubStageKind] = 0;
        data[kStageHazardOff] = hazardOff ? 1 : 0;
        *kAltStageButtons = altStage;
    }

    const char* startMatch(const MatchSetup& setup, int port, const Looks* looks)
    {
        const char* why = checkStartPath();
        if (why != NULL)
        {
            return why;
        }
        if (!selectRunning())
        {
            return "the character select is not running";
        }
        if (*kMyMusicFlag == 1)
        {
            return "My Music is pending";
        }
        GameGlobal* global = g_GameGlobal;
        if (!isRam(global) || !isRam(global->m_selCharData) || !isRam(global->m_selStageData))
        {
            return "no selection data";
        }
        if (port < 0 || port >= iface::kPadCount)
        {
            port = 0;
        }
        u8* sel = reinterpret_cast<u8*>(global->m_selCharData);
        writePlayer(sel + kSelPlayers, setup.p1Kind, kStateHuman, static_cast<u8>(port + 1));
        writePlayer(sel + kSelPlayers + kPlayerSize, setup.cpuKind, kStateCpu, 0);
        sel[kSelHasChoice] = 1;
        word(sel, kSelIcons) = static_cast<u32>(setup.p1Icon) << 8;
        word(sel, kSelIcons + 4) = static_cast<u32>(setup.cpuIcon) << 8;
        if (looks != NULL)
        {
            sel[kSelPlayers + kPlayerColorFile] = looks->p1ColorFile;
            sel[kSelPlayers + kPlayerColor] = looks->p1Color;
            sel[kSelPlayers + kPlayerSize + kPlayerColorFile] = looks->cpuColorFile;
            sel[kSelPlayers + kPlayerSize + kPlayerColor] = looks->cpuColor;
        }

        writeStage(global->m_selStageData, setup.stage, looks != NULL ? looks->altStage : 0,
                   looks != NULL && looks->hazardOff != 0);

        // As if the stage select had just been left with a stage: the scene
        // manager runs the character select's exit() at its next step and
        // sqTraining::setNext takes it from state 6.
        gfSceneManager* m = manager();
        word(m->m_currentSequence, kSeqState) = kStateStageSelectLeft;
        m->unk1 = kExitToNext;
        m->processStep = kProcessExit;
        return NULL;
    }

    static const Word kExitSoloPath[] = {
        {0x806F154C, 0x3B40000B}, // li r26,11            exit sqTraining to Solo menu
        {0x806F1628, 0x80110284}, // lwz r0,0x284(r17)    state 4: the exit code
        {0x806F1644, 0x934F0008}, // stw r26,0x8(r15)     exit code 0 -> state 11
        {0x807038C4, 0x806F1628}  // jump table, state 4
    };

    const char* exitToSolo()
    {
        for (u32 i = 0; i < sizeof(kExitSoloPath) / sizeof(kExitSoloPath[0]); i++)
        {
            if (*reinterpret_cast<const u32*>(kExitSoloPath[i].address) != kExitSoloPath[i].value)
            {
                return "sqTraining is not the expected code";
            }
        }
        if (!selectRunning())
        {
            return "the character select is not running";
        }
        gfSceneManager* m = manager();
        m->unk1 = 0;
        m->processStep = kProcessExit;
        return NULL;
    }

    const char* skipStageSelect(u16 stage)
    {
        const char* why = checkStartPath();
        if (why != NULL)
        {
            return why;
        }
        gfSceneManager* m = manager();
        if (m == NULL || !isRam(m->m_currentSequence) || !game::inTraining()
            || !game::sceneIs(game::currentScene(), "scSelctCharacter")
            || word(m->m_currentSequence, kSeqState) != kStateCharacterSelect || m->processStep != kProcessExit
            || m->unk1 != kExitToNext)
        {
            return "the character select is not leaving for the stage select";
        }
        if (*kMyMusicFlag == 1)
        {
            return "My Music is pending";
        }
        GameGlobal* global = g_GameGlobal;
        if (!isRam(global) || !isRam(global->m_selStageData))
        {
            return "no selection data";
        }
        writeStage(global->m_selStageData, stage, 0);
        // Same state-6 jump as startMatch: scSelChar::exit() runs next, then
        // sqTraining::setNext takes state 6 into setupTraining and the match,
        // keeping every player setting muSelCharTask::setToGlobal just wrote.
        word(m->m_currentSequence, kSeqState) = kStateStageSelectLeft;
        return NULL;
    }
}
}
}
