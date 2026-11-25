#pragma once

#include <types.h>

struct soCollisionAttackData {
    u16 m_serialHitFrame;
};

class soCollisionAttackModule {
public:
    u8 m_isInflict;
    soCollisionAttackModule() : m_isInflict(0)
    {
    }
    u32 getPartSize() const
    {
        return 0;
    }
    bool isAttack(u32, bool) const
    {
        return false;
    }
    const soCollisionAttackData* getData(u32, bool) const
    {
        return NULL;
    }
};

class soCollisionAttackModuleImpl : public soCollisionAttackModule {};
