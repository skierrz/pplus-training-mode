#pragma once

#include <sr/sr_common.h>
#include <types.h>

// Stand-in for the game's file request handle in the PC build: one request
// at a time against the SD card of host/card.h, a set of files a test holds
// in memory. A request finishes a set number of isReady() calls after it
// was made, with the status the card decides (0, or the game's own codes
// for a file that cannot be opened and a short write).
class gfFileIOHandle {
public:
    gfFileIOHandle() : m_request(NULL)
    {
    }
    bool read(const char* filepath, Heaps::HeapType heap, int param);
    void* getBuffer() const;
    int getSize() const;
    bool isReady() const;
    int getReturnStatus() const;
    void release();
    bool createDirRequest(const char* filepath);
    bool readRequestNoSync(const char* filepath, void* addr, int length, int offset);
    bool del(const char* filepath);
    bool writeRequest(const char* filepath, void* addr, int length, int offset);

    void* m_request;
};
