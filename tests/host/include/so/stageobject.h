#pragma once

#include <so/anim/so_anim_cmd_event_presenter.h>
#include <so/so_module_accesser.h>
#include <types.h>

class StageObject : public soAnimCmdEventObserver {
public:
    soModuleAccesser* m_moduleAccesser;
    u32 m_taskId;

    StageObject() : soAnimCmdEventObserver(-1, -1), m_moduleAccesser(NULL), m_taskId(0)
    {
    }
};
