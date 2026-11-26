#pragma once

#include <types.h>

// The SD card on the PC: files a test holds in memory, behind the game's
// own interfaces as the mod uses them (the stand-ins gf/gf_file_io_handle.h
// and revolution/FA.h). The mod's file code (src/main/storage/file_io.cpp)
// runs unchanged on top: its paths, its size check before a read, its
// polling, its results.
//
// What it copies from the game, as file_io.cpp and docs/files.md state it:
// a write stores the length rounded up to 32 bytes; a write that cannot
// open its file ends with status 1, a short write with status 3; a read is
// only ever asked for bytes the file holds (the game has no answer to a
// longer one: here the test fails).
//
// File names below are names inside the mod's folder (<build folder>tm/).
namespace host {
namespace card {
    // Called by the runner before every test: an empty, writable card;
    // requests finish at the first poll.
    void reset();

    // Polls a request stays unfinished for.
    void setDelay(int polls);
    // The write-protect switch: folder, write and delete requests fail.
    void setLocked(bool locked);
    // No room: a write stores half of what was sent and ends "short".
    void setFull(bool full);
    // Fault injection: the Nth read from now (1 = next read, 0 = off) hits a
    // short FAFread, so readSDFile frees the destination buffer and returns
    // status 2 (0x8001CD50..0x8001CDD8).
    void setShortRead(int onRead);
    // Fault injection: the Nth read from now fails with `status`; when
    // `freeBuf` is true, readSDFile also frees the destination buffer first.
    void setReadStatus(int onRead, int status, bool freeBuf = false);
    // The date and time the console's clock gives.
    void setClock(int year, int month, int day, int hour, int minute, int second);

    bool has(const char* name);
    u32 size(const char* name);
    // The file's bytes (to read, or to damage); NULL if it is not there.
    u8* data(const char* name);
    void put(const char* name, const void* bytes, u32 size);
    void remove(const char* name);
    // Files at arbitrary FAT paths (no "sd:" prefix), e.g. the plugin file.
    void putRaw(const char* path, const void* bytes, u32 size);
    void removeRaw(const char* path);
    int fileCount();

    // Requests the mod made since the test began.
    int writes();
    int reads();
    int deletes();
    bool folderMade();
}
}
