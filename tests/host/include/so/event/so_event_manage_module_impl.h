#pragma once

#include <types.h>

class soEventManageModule {
public:
    s16 m_manageID;
    soEventManageModule() : m_manageID(-1)
    {
    }
    virtual s16 getManageId() const
    {
        return m_manageID;
    }
};

class soEventManageModuleImpl : public soEventManageModule {};
