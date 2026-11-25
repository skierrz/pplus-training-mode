#pragma once

class gfScene;

namespace tm {
namespace game {
    // The current scene object, or NULL while there is none.
    gfScene* currentScene();

    // Names of the current scene ("scSelctCharacter", "scMelee", ...) and
    // sequence ("sqVsMelee", "sqTraining", ...); "?" while there is none.
    const char* sceneName();
    const char* sequenceName();

    // True if `scene` is a readable scene object named `name`.
    bool sceneIs(const gfScene* scene, const char* name);

    // True while the current sequence is Training (its menus and matches).
    bool inTraining();

    // True if the game mode the menus set for the next or current match
    // (gmGlobalModeMelee) is Training.
    bool trainingGameMode();
}
}
