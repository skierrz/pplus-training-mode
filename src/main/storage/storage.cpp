#include "main/storage/storage.h"

#include "main/extra.h"
#include "main/gfx/draw.h"
#include "main/lab.h"
#include "main/storage/notice.h"
#include "main/storage/recording_file.h"
#include "main/storage/settings_file.h"
#include "main/ui/settings_modal.h"

namespace tm {
namespace storage {
    void attach(u8* block)
    {
        noticeClear();
        settingsAttach(block);
        recordingAttach(block);
    }

    void detach()
    {
        settingsDetach();
        recordingDetach();
    }

    void onRender(const game::MatchState& match)
    {
        settingsUpdate();
        recordingUpdate();
        if (match.running && (!match.paused || lab::menuOpen()) && noticeActive())
        {
            bool modalOpen = lab::menuOpen() || ui::settingsModalOpen() || (main::extraDrill(0, NULL) & 2u) != 0u;
            gfx::begin();
            noticeDraw(modalOpen);
            gfx::end();
        }
    }
}
}
