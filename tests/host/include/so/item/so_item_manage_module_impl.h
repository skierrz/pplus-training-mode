#pragma once

#include <types.h>

enum itKind {
    Item_BeamSword = 0x04,
    Item_BobOmb = 0x06,
    Item_MrSaturn = 0x13,
    Item_Peach_Daikon = 0x56
};

struct soItemInfo {
    void* m_item;
};

class soItemManageModule {
public:
    bool isHaveItem(int) const
    {
        return false;
    }
    int getHaveItemKind(int) const
    {
        return -1;
    }
    void getHaveItemInfo(soItemInfo* out, int) const
    {
        if (out != NULL)
        {
            out->m_item = NULL;
        }
    }
};
