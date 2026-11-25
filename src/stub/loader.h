#pragma once

#include <types.h>

// Loads the modules appended to the plugin's own file, each of a given kind
// into a given heap, for the life of one scene (load_policy.h decides
// which). Up to two are loaded at once; the second may be linked against
// the first, so it is loaded after it and unloaded before it.
namespace tm {
namespace loader {
    // Reads the plugin's own file once and checks every appended module.
    // Must run from _prolog, where Syriinge itself reads plugin files.
    void init();

    // Detaches, unlinks and frees the loaded modules, last loaded first.
    void unload(const char* when);

    // From the scene hooks, just before gfSceneManager calls the current
    // scene's start() and exit(). At start, each of the scene's modules is
    // read into a temporary buffer, linked into its permanent block and the
    // temporary buffer freed before the next module or the scene runs.
    void onSceneStart();
    void onSceneExit();

    // From the render hook: stops calling into a module that is still
    // loaded outside the scene it was loaded for (should never happen).
    void checkScene();

    // The heap the extra module is loaded in right now, or -1.
    s32 extraHeap();
}
}
