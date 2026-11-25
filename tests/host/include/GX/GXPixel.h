#pragma once

// Stand-in: src/main/gfx/text.cpp switches the depth test off before it
// prints. Nothing is drawn on the PC.
enum { GX_ALWAYS = 7 };

inline void GXSetZMode(bool, int, bool)
{
}
