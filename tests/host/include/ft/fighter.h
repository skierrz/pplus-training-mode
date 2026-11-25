#pragma once

#include <so/so_module_accesser.h>
#include <so/stageobject.h>
#include <so/status/so_status_event_presenter.h>
#include <types.h>

// Stand-in for the game's Fighter in the PC build: keeps m_moduleAccesser
// (via StageObject), m_taskId, and the status/anim event observer bases so
// tracker and character_rng tests can compile against it.
class Fighter : public StageObject, public soStatusEventObserver {
public:
    Fighter() : StageObject(), soStatusEventObserver(-1)
    {
    }
};
