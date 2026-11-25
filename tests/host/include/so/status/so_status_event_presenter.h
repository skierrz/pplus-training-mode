#pragma once

#include <so/event/so_event_system.h>
#include <types.h>

class soStatusData;
class soModuleAccesser;

class soStatusEventObserver : public soEventObserver<soStatusEventObserver> {
public:
    soStatusEventObserver(s16 unitID = -1) : soEventObserver<soStatusEventObserver>(unitID)
    {
    }
    virtual void notifyEventChangeStatus(int, int, soStatusData*, soModuleAccesser*)
    {
    }
};
