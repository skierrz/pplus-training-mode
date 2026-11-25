#include "shared/game/scene.h"

#include <gf/gf_scene.h>
#include <gm/gm_global.h>

#include "shared/game/mem.h"

namespace tm {
namespace game {
    // gmGlobalModeMelee+0x8: the game mode in the top six bits; 0xD is Training.
    enum {
        kModeMeleeGameMode = 0x8,
        kGameModeTraining = 0xD
    };

    static const char* nameOr(const char* const* name)
    {
        return isRam(name) && isRam(*name) ? *name : "?";
    }

    gfScene* currentScene()
    {
        gfSceneManager* manager = gfSceneManager::getInstance();
        if (!isRam(manager))
        {
            return NULL;
        }
        gfScene* scene = manager->m_currentScene;
        return isRam(scene) ? scene : NULL;
    }

    const char* sceneName()
    {
        gfScene* scene = currentScene();
        return scene != NULL ? nameOr(&scene->m_sceneName) : "?";
    }

    const char* sequenceName()
    {
        gfSceneManager* manager = gfSceneManager::getInstance();
        if (!isRam(manager))
        {
            return "?";
        }
        gfSequence* sequence = manager->m_currentSequence;
        return isRam(sequence) ? nameOr(&sequence->m_sequenceName) : "?";
    }

    bool sceneIs(const gfScene* scene, const char* name)
    {
        return isRam(scene) && isRam(scene->m_sceneName) && sameString(scene->m_sceneName, name);
    }

    bool sequenceIs(const char* name)
    {
        return sameString(sequenceName(), name);
    }

    bool inTraining()
    {
        return sequenceIs("sqTraining");
    }

    bool trainingGameMode()
    {
        if (!isRam(g_GameGlobal) || !isRam(g_GameGlobal->m_modeMelee))
        {
            return false;
        }
        return (byteAt(g_GameGlobal->m_modeMelee, kModeMeleeGameMode) >> 2) == kGameModeTraining;
    }
}
}
