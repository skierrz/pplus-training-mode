#pragma once

#include <gf/gf_file_io_handle.h>

// Stand-in (PC build). The mod calls these two only after it has compared
// their machine code with what it read in the game; a PC function is not
// that code, so here the mod takes its "unknown code" path: the card counts
// as inserted and writable, and a request against a locked card fails by
// itself (host/card.h).
class gfFileIO {
public:
    static bool checkSDInserted();
    static bool checkSDWriteProtected();
};
