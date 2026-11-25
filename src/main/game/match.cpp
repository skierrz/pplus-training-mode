#include "main/game/match.h"

#include <gf/gf_scene.h>
#include <gm/gm_global.h>
#include <if/if_mngr.h>
#include <types.h>

#include "shared/game/mem.h"
#include "shared/game/scene.h"

namespace tm {
namespace game {
    // Read in Training matches on 3.2.1 (two pause/resume cycles compared
    // byte for byte in emulator memory):
    //  - scMelee+0x54, the stOperatorRuleMelee: byte +0xDF goes 0 -> 1 when
    //    the match starts and stays 1.
    //  - scMelee+0x68, the info operator, in Training a
    //    StOperatorInfoTraining (0x160 bytes): byte +0x128 is 1 exactly while
    //    the Training menu (Start) is open and the game is paused.
    enum {
        kSceneRuleOperator = 0x54,
        kSceneInfoOperator = 0x68,
        kRuleStarted = 0xDF,
        kTrainingInfoMenuOpen = 0x128,
        // g_IfMngr+0x78 -> IfMinigameTraining (the pointer P+'s Training AI
        // code loads too), whose byte +0x17 is the pause menu's cursor row
        // (read while stepping the cursor in the emulator).
        kIfMinigameTraining = 0x78,
        kTrainingMenuCursor = 0x17
    };

    // Training's operator, as Finish drives it (RSBE01 rev 2, P+ 3.2.1, read
    // in a memory dump; measured on Mario vs Fox CPU, Final Destination):
    // docs/internals.md, "How Training's Finish ends a match".
    enum {
        kIfCloseTrainingMenu = 0x800DCE04,
        kIfCloseWord0 = 0x80630078, // lwz r3,0x78(r3)
        kIfCloseWord1 = 0x480286D0  // b 0x801054D8 (IfMinigameTraining's closeMenu)
    };
    enum {
        kInfoVtable = 0x3C,
        kInfoTrainingVtable = 0x80B321A8, // StOperatorInfoTraining's (its RTTI, in the same memory dump)
        kInfoCondition = 0x44,
        kInfoEnabled = 0x11B,
        kInfoWait = 0x12C,
        kInfoStateWord = 0x140,
        kInfoState = 0x144,
        kConditionWait = 0x18,
        kStateWords = 0x80B32158,
        kStateRunning = 376,
        kStateWait = 383,
        kStateWaitWord = 0x38
    };

    MatchState matchState()
    {
        MatchState state = {false, false, false};
        gfScene* scene = currentScene();
        if (!sceneIs(scene, "scMelee"))
        {
            return state;
        }
        const void* rule = reinterpret_cast<const void*>(wordAt(scene, kSceneRuleOperator));
        if (!isRam(rule) || byteAt(rule, kRuleStarted) == 0)
        {
            return state;
        }
        state.running = true;
        state.training = trainingGameMode();
        if (state.training)
        {
            const void* info = reinterpret_cast<const void*>(wordAt(scene, kSceneInfoOperator));
            state.paused = isRam(info) && byteAt(info, kTrainingInfoMenuOpen) != 0;
        }
        return state;
    }

    EndResult endTrainingMatch()
    {
        gfScene* scene = currentScene();
        if (!sceneIs(scene, "scMelee") || !trainingGameMode())
        {
            return kEndRefused;
        }
        u8* info = reinterpret_cast<u8*>(wordAt(scene, kSceneInfoOperator));
        if (!isRam(info) || wordAt(info, kInfoVtable) != kInfoTrainingVtable || byteAt(info, kInfoEnabled) == 0
            || wordAt(info, kInfoState) != kStateRunning)
        {
            return kEndRefused;
        }
        const void* condition = reinterpret_cast<const void*>(wordAt(info, kInfoCondition));
        if (!isRam(condition))
        {
            return kEndRefused;
        }
        EndResult how = kEndByWait;
        if (byteAt(info, kTrainingInfoMenuOpen) != 0)
        {
            // Training's pause is open (the lab menu was opened over it):
            // closed as processBegin closes it on Finish.
            IfMngr* manager = g_IfMngr;
            const u32* code = reinterpret_cast<const u32*>(kIfCloseTrainingMenu);
            if (!isRam(manager) || !isRam(reinterpret_cast<const void*>(wordAt(manager, kIfMinigameTraining)))
                || code[0] != kIfCloseWord0 || code[1] != kIfCloseWord1)
            {
                return kEndRefused;
            }
            reinterpret_cast<void (*)(IfMngr*)>(kIfCloseTrainingMenu)(manager);
            how = kEndFromPause;
        }
        // What processBegin does after Finish (0x80962BA8).
        *reinterpret_cast<u32*>(info + kInfoWait) = byteAt(condition, kConditionWait);
        *reinterpret_cast<u32*>(info + kInfoStateWord) =
            wordAt(reinterpret_cast<const void*>(kStateWords), kStateWaitWord);
        *reinterpret_cast<u32*>(info + kInfoState) = kStateWait;
        return how;
    }

#pragma force_active on
    bool trainingEnding()
    {
        gfScene* scene = currentScene();
        const void* info = sceneIs(scene, "scMelee") ? reinterpret_cast<const void*>(wordAt(scene, kSceneInfoOperator))
                                                     : NULL;
        return isRam(info) && wordAt(info, kInfoVtable) == kInfoTrainingVtable && wordAt(info, kInfoState) == kStateWait;
    }
#pragma force_active reset

    int trainingMenuCursor()
    {
        IfMngr* manager = g_IfMngr;
        if (!isRam(manager))
        {
            return -1;
        }
        const void* menu = reinterpret_cast<const void*>(wordAt(manager, kIfMinigameTraining));
        return isRam(menu) ? byteAt(menu, kTrainingMenuCursor) : -1;
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
}
}
