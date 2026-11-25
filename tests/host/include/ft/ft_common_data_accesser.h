#pragma once

#include <types.h>

enum ftKind {
    Fighter_Mario = 0x0,
    Fighter_Fox = 0x6,
    Fighter_Luigi = 0x8,
    Fighter_Peach = 0xc,
    Fighter_Popo = 0xf,
    Fighter_Nana = 0x10,
    Fighter_GameWatch = 0x12,
    Fighter_Falco = 0x13
};

class ftCommonDataAccesser {
public:
    enum { kMaxKinds = 64 };
    const void* m_data[kMaxKinds];

    void reset()
    {
        for (int i = 0; i < kMaxKinds; i++)
        {
            m_data[i] = NULL;
        }
    }
    void setData(int kind, const void* data)
    {
        if (kind >= 0 && kind < kMaxKinds)
        {
            m_data[kind] = data;
        }
    }
    const void* getData(int kind) const
    {
        return (kind >= 0 && kind < kMaxKinds) ? m_data[kind] : NULL;
    }
};

extern ftCommonDataAccesser g_ftCommonDataAccesser;
