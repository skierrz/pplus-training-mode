#pragma once

#include <types.h>

#include "main/cpu/fighter_input.h"
#include "main/record/slots.h"

// SF6-style "Start On First Input" recording helpers (adapted from lil klon's
// ProjectPunch dummy recorder; see CREDITS.md and THIRD_PARTY_NOTICES.md).
//
// Operates on the already-normalized record::Input (populated by readInput()
// in record.cpp) so GameCube, Classic, Nunchuk, and Sideways Wii Remote all
// share identical neutrality thresholds. Pure header (no Brawl headers) so
// host unit tests verify the exact runtime logic.
namespace tm {
namespace record {
    static const float kNeutralStickThreshold = 12.0f / 127.0f;
    static const float kNeutralTriggerThreshold = 40.0f / 255.0f;

    inline float absInputAxis(float v)
    {
        return v < 0.0f ? -v : v;
    }

    // True when no non-flick-jump button is held and all sticks and analog
    // triggers sit inside their dead zones.
    inline bool isNeutralInput(const Input& in)
    {
        if ((in.buttons & ~static_cast<u32>(cpu::button::kFlickJump)) != 0)
        {
            return false;
        }
        if (absInputAxis(in.x) >= kNeutralStickThreshold || absInputAxis(in.y) >= kNeutralStickThreshold
            || absInputAxis(in.cx) >= kNeutralStickThreshold || absInputAxis(in.cy) >= kNeutralStickThreshold)
        {
            return false;
        }
        if (in.l >= kNeutralTriggerThreshold || in.r >= kNeutralTriggerThreshold)
        {
            return false;
        }
        return true;
    }

    // Whether a run or mode entry should arm Start On First Input.
    inline bool shouldArmRecording(bool startOnFirstInput, bool hmnRecording, bool cpuRecording)
    {
        return startOnFirstInput && (hmnRecording || cpuRecording);
    }

    // True when either side is currently running playback (not yet ended or
    // taken over). Used so standalone kModeReRecord holds s_t once its
    // playback ends while armed, whereas co-playback keeps s_t advancing.
    inline bool hasActivePlayback(bool hmnPlayingMode, bool hmnCancelled, bool cpuPlayingMode, bool cpuCancelled)
    {
        return (hmnPlayingMode && !hmnCancelled) || (cpuPlayingMode && !cpuCancelled);
    }

    // Standalone armed recording holds the recording clock at frame 0 (or at
    // the re-record splice point) until the first non-neutral input;
    // co-playback keeps s_t advancing so the playing side stays synced.
    inline bool shouldHoldRecordingClock(bool recArmed, bool anyPlaying)
    {
        return recArmed && !anyPlaying;
    }

    // True when a quick savestate load may restore the saved armed flag.
    // Prevents freezing s_t if Start On Input was turned off or either side's
    // recording mode changed after the quick state was taken.
    inline bool shouldRestoreQuickArmed(bool startOnFirstInput, int savedHmnMode, int curHmnMode,
                                        int savedCpuMode, int curCpuMode)
    {
        return startOnFirstInput && savedHmnMode == curHmnMode && savedCpuMode == curCpuMode;
    }

    // Evaluates one recording frame against the armed gate. Returns true when
    // the caller should write this frame into the slot; sets `clearSlot` true
    // when a fresh kModeRecord recording transitions out of the armed state so
    // stale earlier frames in the target slot are discarded first.
    inline bool consumeArmedInput(bool& recArmed, const Input& in, bool isFreshRecord, bool& clearSlot)
    {
        clearSlot = false;
        if (!recArmed)
        {
            return true;
        }
        if (isNeutralInput(in))
        {
            return false;
        }
        recArmed = false;
        clearSlot = isFreshRecord;
        return true;
    }
}
}
