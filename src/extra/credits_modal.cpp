#include "extra/credits_modal.h"

#include "common/credits_modal.h"
#include "main/ui/menu.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    static common::CreditsModalState s_credits;

    void creditsModalAttach()
    {
        common::creditsModalReset(s_credits);
    }

    void creditsModalDetach()
    {
        common::creditsModalReset(s_credits);
    }

    bool creditsModalVisible()
    {
        return common::creditsModalVisible(s_credits);
    }

    void creditsModalDraw()
    {
        common::creditsModalDraw(s_credits);
    }

    const char* creditsMenuDesc()
    {
        return TM_CREDITS_MENU_DESC;
    }

    u32 creditsModalHook(u32 op, const main::MenuPad* pad)
    {
        u32 cmd = op & 0xFFu;
        u8 ctrlType = static_cast<u8>((op >> 8) & 0xFFu);
        switch (cmd)
        {
        case main::kCreditsAvailable:
            return 1u;
        case main::kCreditsOpen:
            common::creditsModalOpen(s_credits, ctrlType);
            TM_LOG("credits: opened\n");
            return 1u;
        case main::kCreditsStep:
        {
            if (!s_credits.open)
            {
                return 0u;
            }
            if (pad == NULL)
            {
                common::creditsModalReset(s_credits);
                return 1u;
            }
            int snd = common::creditsModalStep(s_credits, pad->held, pad->pressed, pad->stickY, pad->controllerType);
            if (snd >= 0)
            {
                ui::menuSound(snd);
                if (!s_credits.open)
                {
                    TM_LOG("credits: closed\n");
                }
            }
            // Return 1 even on the frame B/Start closes Credits so the close press is consumed.
            return 1u;
        }
        case main::kCreditsForceClose:
            common::creditsModalReset(s_credits);
            return 0u;
        default:
            return 0u;
        }
    }
}
}
