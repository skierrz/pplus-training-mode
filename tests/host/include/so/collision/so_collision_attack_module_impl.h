#pragma once

#include <types.h>

class soCollisionAttackModule {
public:
    u8 m_isInflict;
    soCollisionAttackModule() : m_isInflict(0)
    {
    }
};

class soCollisionAttackModuleImpl : public soCollisionAttackModule {};
