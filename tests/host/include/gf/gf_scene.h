#pragma once

// Stand-in (PC build) for Brawl's gfSceneManager.
class gfScene;

class gfSceneManager {
public:
    void* m_currentSequence;
    int processStep;
    int unk1;

    static gfSceneManager* getInstance();
};
