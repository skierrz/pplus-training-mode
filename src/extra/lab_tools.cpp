// Alter Inputs (lab_tools.h): writes a row's changed value into the managed
// recording slot.
#include "extra/lab_tools.h"

#include <stddef.h>
#include <string.h>

#include "main/record/record.h"
#include "main/record/slots.h"
#include "main/settings.h"
#include "main/ui/pages/alter_inputs.h"
#include "shared/log.h"

namespace tm {
namespace extra {
    // ---- Alter Inputs ------------------------------------------------------

    // The rows (ui/pages/alter_inputs.cpp) show one frame of the managed
    // slot as stored; a change goes straight into the slot in its format
    // (record/slots.h) for the next playback. Only the value a row changed is
    // rewritten: other values and the bits no row shows stay exact.
    enum {
        kFirstAnalog = 4,
        kSideTaunt = 7,
        kTapJump = 0x8000 // what playback gives a frame before a slot's start
    };

    // The rows' axes are the Input's six floats from x on, in their order.
    TM_STATIC_ASSERT(offsetof(record::Input, r) == offsetof(record::Input, x) + 20, tm_input_floats_in_a_row);
    static const float kScale[ui::pages::kAlterAxes] = {80.0f, 80.0f, 80.0f, 80.0f, 150.0f, 150.0f};
    // aiInput button bits (ipPadButton, ip/input.h). Clearing the side
    // taunt also clears its direction (0x2000 left, 0x4000 right).
    static const u16 kButtonBits[ui::pages::kAlterButtons] = {0x1, 0x2, 0x4, 0x8, 0x20, 0x100, 0x400, 0x800, 0x1000};

    static ui::pages::AlterCopy& copy()
    {
        return *reinterpret_cast<ui::pages::AlterCopy*>(ui::menuScratch());
    }

    static record::Slot* slot()
    {
        const main::RecordingSettings& r = main::settings().recording;
        return record::slotAt(r.manageSide, r.manageSlot);
    }

    static int shown(const float* axes, int k)
    {
        float v = axes[k] * kScale[k];
        return static_cast<int>(v < 0.0f ? v - 0.5f : v + 0.5f);
    }

    // A frame as playback gives it: the stored one, or no input before the
    // slot's start.
    static void frameAt(const record::Slot& s, int t, record::Input& in)
    {
        if (!record::slotRead(s, t, in))
        {
            memset(&in, 0, sizeof(in));
            in.buttons = kTapJump;
        }
    }

    // P+'s L/R press state (Source/ProjectM/ButtonPresses.asm:433-457, at
    // the end of its controller update, into LA-Basic[77]): 0 neither analog
    // trigger at 0.3 or more, 1 one just got there, 2 held.
    static u8 pressState(const record::Input& prev, const record::Input& now)
    {
        const float t = 0.3f;
        bool l = now.l >= t;
        bool r = now.r >= t;
        if (!l && !r)
        {
            return 0;
        }
        if (l && prev.l < t)
        {
            return 1;
        }
        return r && prev.r < t ? 1 : 2;
    }

    // Every row's `enabled`: false (grey) without inputs in the slot; else
    // the Frame row's range follows the slot's frames (frame n is the run's
    // step n - 1; a recording started after the run starts later) and the
    // copy is read back from the slot, so it shows what is stored.
    bool alterRow()
    {
        record::Slot* s = slot();
        ui::Option& frame = ui::pages::alterFrameRow();
        if (s == NULL || record::slotEmpty(*s))
        {
            frame.min = frame.max = 1;
            return false;
        }
        ui::pages::AlterCopy& c = copy();
        frame.min = static_cast<s16>(s->start + 1);
        frame.max = static_cast<s16>(s->end);
        c.frame = static_cast<u16>(c.frame <= s->start ? s->start + 1 : (c.frame > s->end ? s->end : c.frame));
        record::Input in;
        frameAt(*s, c.frame - 1, in);
        for (int k = 0; k < ui::pages::kAlterAxes; k++)
        {
            c.axis[k] = static_cast<s16>(shown(&in.x, k));
        }
        for (int k = 0; k < ui::pages::kAlterButtons; k++)
        {
            c.button[k] = (in.buttons & kButtonBits[k]) != 0;
        }
        return true;
    }

    // A value row changed: the stored value that differs takes the row's. A
    // new analog value also sets P+'s press state of this frame and the
    // next. A value new to a full table (a slot keeps 256 different values of
    // each) is stored as the nearest one there, and the HUD says so; no
    // other frame changes.
    void alterEdited()
    {
        record::Slot* s = slot();
        if (s == NULL || record::slotEmpty(*s))
        {
            return;
        }
        const ui::pages::AlterCopy& c = copy();
        int t = c.frame - 1;
        record::Input in;
        frameAt(*s, t, in);
        float* axes = &in.x;
        bool analog = false;
        for (int k = 0; k < ui::pages::kAlterAxes; k++)
        {
            if (shown(axes, k) != c.axis[k])
            {
                axes[k] = c.axis[k] / kScale[k];
                analog = analog || k >= kFirstAnalog;
            }
        }
        for (int k = 0; k < ui::pages::kAlterButtons; k++)
        {
            if (((in.buttons & kButtonBits[k]) != 0) != (c.button[k] != 0))
            {
                in.buttons ^= kButtonBits[k];
                in.buttons &= k == kSideTaunt && !c.button[k] ? ~0x6000u : ~0u;
            }
        }
        record::Input other;
        if (analog)
        {
            frameAt(*s, t - 1, other);
            in.press = pressState(other, in);
        }
        bool exact = record::slotEdit(*s, t, in);
        if (analog && t + 1 < s->end)
        {
            frameAt(*s, t + 1, other);
            other.press = pressState(in, other);
            exact = record::slotEdit(*s, t + 1, other) && exact;
        }
        if (!exact)
        {
            record::showMessage("Value table full: nearest value stored.");
        }
        TM_LOG("record: frame %d edited: buttons %x\n", c.frame, in.buttons);
    }
}
}
