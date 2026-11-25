#include "main/storage/storage.h"

#include "main/gfx/draw.h"
#include "main/lab.h"
#include "main/storage/notice.h"
#include "main/storage/recording_file.h"
#include "main/storage/settings_file.h"

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
            gfx::begin();
            noticeDraw(lab::menuOpen());
            gfx::end();
        }
    }
}
}
