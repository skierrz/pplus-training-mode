#pragma once

#include <so/event/so_event_system.h>
#include <types.h>

struct acAnimCmdArg {
    s32 type;
    s32 data;
};

struct acAnimCmdConv {
    u8 group;
    u8 type;
    u8 argNum;
    u8 pad;
    acAnimCmdArg* args;
};

class acAnimCmd {
public:
    const acAnimCmdConv* m_cmd;
    const acAnimCmdConv* getCmdAddress() const
    {
        return m_cmd;
    }
};

class soModuleAccesser;

class soAnimCmdEventObserver : public soEventObserver<soAnimCmdEventObserver> {
public:
    soAnimCmdEventObserver(s16 unitID = -1, s8 = -1) : soEventObserver<soAnimCmdEventObserver>(unitID)
    {
    }
    virtual bool isObserv(char)
    {
        return false;
    }
    virtual bool notifyEventAnimCmd(acAnimCmd*, soModuleAccesser*, s32)
    {
        return false;
    }
};
