#include <gf/gf_pad_status.h>
#include <types.h>

#include "main/cpu/driver.h"
#include "main/cpu/options.h"
#include "main/cpu/view.h"
#include "main/display/display.h"
#include "main/game/freeze.h"
#include "main/game/pad_inmatch.h"
#include "main/lab.h"
#include "main/record/record.h"
#include "main/savestate/savestate.h"
#include "main/savestate/settings.h"
#include "main/savestate/snapshot.h"
#include "main/extra.h"
#include "main/settings.h"
#include "main/ui/info_display.h"
#include "menu/game/training.h"
#include "menu/sound.h"
#include "shared/game/pad_map.h"

// Functions of sources that are not in the PC build which the sources that
// are in it name without the tests ever depending on their answer: what the
// lab menu's rows run when they change (the pages are compiled for their
// tables) and the like. Here they do nothing and answer "no".
// Each group names the source that holds the real one. A test that needs
// one of them to answer something moves it to fakes.cpp and gives it a
// value to set.
namespace tm {
namespace game {
    // src/main/game/freeze.cpp: frame advance never holds the game.
    bool frozen()
    {
        return false;
    }
}

namespace lab {
    // src/main/lab.cpp.
    void applyPercent(int)
    {
    }
    void openTrainingMenu()
    {
    }
    void exitMatch()
    {
    }
    char g_resetLabel[12];
    void resetAllPressed()
    {
    }
    char g_diPickLabel[16] = "Tilt + A";
    void toggleDiStickPicker()
    {
    }
    void frameAdvanceChanged()
    {
    }
    bool menuOpen()
    {
        return false;
    }
    bool comboLHeldOnPads(const gfPadStatus*)
    {
        return false;
    }
    bool olimarDpadActive(const game::MatchState& m)
    {
        u8 mode = static_cast<u8>((main::settings().fighterMechanics.olimarColorMatAndMode >> 10) & 0x3u);
        return mode != main::kOlimarDpadOff && mode < main::kOlimarDpadModeCount && m.running && m.training
               && !m.paused && main::extraOlimarP1();
    }
    bool minusBoundToFrameAdvance(const game::MatchState& m)
    {
        if (!main::settings().general.frameAdvance || !m.running || !m.training)
        {
            return false;
        }
        u32 bits = game::faButtonBits(gfPadType::WIIMOTE, main::settings().controls.advanceButton, false)
                   | game::faButtonBits(gfPadType::WIIMOTE, main::settings().controls.decrementButton, true);
        return (bits & game::kPadWiiMinus) != 0;
    }
    void osdSettingsChanged()
    {
    }
    void resetLabSettings()
    {
    }
}

namespace cpu {
    // src/main/cpu/options.cpp.
    char g_freezeCpuLabel[32] = "";
    char g_moveCpuLabel[32] = "";
    void controlledByChanged()
    {
    }
    void freezeCpuToggle()
    {
    }
    void moveCpuToggle()
    {
    }
    // src/main/cpu/driver.cpp, view.cpp.
    bool controlled(int)
    {
        return false;
    }
    bool take(const Owner&, Source*, u32)
    {
        return false;
    }
    bool fighterX(int, float&)
    {
        return false;
    }
    bool firstCpuX(float&)
    {
        return false;
    }
    bool humanPlayer(int)
    {
        return false;
    }
}

namespace record {
    // src/main/record/record.cpp.
    void chanceChanged(int, int)
    {
    }
    void copyManagedSlot()
    {
    }
    void cpuModeChanged()
    {
    }
    void deleteManagedSlot()
    {
    }
    void deletePositions()
    {
    }
    char g_deletePosLabel[12];
    char g_deleteSlotLabel[12];
    void deletePositionsPressed()
    {
    }
    void deleteManagedSlotPressed()
    {
    }
    void clearDeleteConfirm()
    {
    }
    void hmnModeChanged()
    {
    }
    void mirroredChanged()
    {
    }
    void prunePositions()
    {
    }
    void resavePositions()
    {
    }
    void saveOrRestore()
    {
    }
    void slotChanged()
    {
    }
}

namespace savestate {
    // src/main/savestate/savestate.cpp, snapshot.cpp.
    char g_stateText[kStateTextSize] = "";
    bool recordingStateValid()
    {
        return false;
    }
    void requestLoad()
    {
    }
    void requestSave()
    {
    }
    u32 boundBits()
    {
        const main::ControlSettings& c = main::settings().controls;
        return (c.dpadLeft == kLeftLoadState ? 1u : 0u) | (c.dpadRight == kRightSaveState ? 2u : 0u);
    }
    void setOwnExclusion(u32, u32)
    {
    }
    void setOwnExclusionAt(u32, u32, u32)
    {
    }
}

namespace menu {
    // src/menu/sound.cpp: no sound.
    void sound(int)
    {
    }

namespace select {
    // src/menu/game/select.cpp: no character select to shape on the PC.
    void attach(int)
    {
    }
    void detach(bool)
    {
    }
    void update()
    {
    }
    bool needsRebuild(int)
    {
        return false;
    }
    const char* reopen()
    {
        return "not rebuilt on the PC";
    }
}
}
}
