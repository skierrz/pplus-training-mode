#pragma once

#include <types.h>

class soAnimCmdModule;

// Animation command lists written into the memory image, for tests of code
// that reads a move's scripts (src/extra/frame_data.cpp describes the
// format): a command is two words, (group, type, argument count, option) and
// a pointer to its arguments; an argument is two words, a kind and a value.
//
//   host::Script s;
//   s.frame(23).cmd(0x04, 0x07).real(1.875f)   // Frame Speed Modifier
//    .frame(38).cmd(0x64, 0x00)                // Allow Interrupt
//    .end();
//   const u32* list = s.list();
//
// Lists and arguments are laid out in MEM2 from fixed addresses, a new
// Script after the last; the image is zeroed before every test.
namespace host {
    class Script {
    public:
        Script();
        // A command with no arguments yet; the argument calls that follow
        // add to it.
        Script& cmd(u32 group, u32 type);
        Script& integer(s32 value);    // kind 0
        Script& real(float value);     // kind 1: a scalar, stored times 60000
        Script& pointer(u32 address);  // kind 2
        Script& boolean(bool value);   // kind 3
        Script& variable(u32 id);      // kind 5
        Script& requirement(u32 id);   // kind 6
        Script& raw(u32 kind, u32 value);

        // The interpreter's own commands (group 0).
        Script& wait(float frames)
        {
            return cmd(0, 1).real(frames);
        }
        Script& frame(float time)
        {
            return cmd(0, 2).real(time);
        }
        Script& loop(s32 count)
        {
            return cmd(0, 4).integer(count);
        }
        Script& endLoop()
        {
            return cmd(0, 5);
        }
        Script& call(const Script& sub)
        {
            return cmd(0, 7).pointer(sub.address());
        }
        Script& ret()
        {
            return cmd(0, 8);
        }
        Script& jump(const Script& to)
        {
            return cmd(0, 9).pointer(to.address());
        }
        // An If on a requirement nothing here evaluates.
        Script& iff()
        {
            return cmd(0, 0xA).requirement(7);
        }
        Script& elseIf()
        {
            return cmd(0, 0xD).requirement(7);
        }
        Script& orElse()
        {
            return cmd(0, 0xE);
        }
        Script& endIf()
        {
            return cmd(0, 0xF);
        }
        Script& end()
        {
            return cmd(0, 0);
        }
        // `count` commands that mean nothing to a walk (Bit Variable Set).
        Script& filler(int count);

        u32 address() const
        {
            return m_start;
        }
        const u32* list() const
        {
            return reinterpret_cast<const u32*>(m_start);
        }
        // The address of command `index` (0 is the first).
        const u32* at(int index) const
        {
            return reinterpret_cast<const u32*>(m_start + 8 * index);
        }

        // Called by the runner before every test.
        static void reset();

    private:
        enum { kRoom = 0x1000 }; // commands a Script may hold: 512
        u32 m_start;
        u32 m_next;
        u32 m_last; // the command being given arguments
    };

    // A fighter's animation-command module as the mod reads it, written
    // into the image: the module, its thread vector, the 11 control units
    // and their interpreters. setThread gives unit `unit` (0 the action
    // script, 1 the subaction script, 9 the status's loop) its script: the
    // first command, the command it is at, and its clock.
    class AnimCmd {
    public:
        AnimCmd();
        void setThread(int unit, const u32* start, const u32* current, float clock);
        // What a fake fighter's m_animCmdModule is set to.
        soAnimCmdModule* module() const
        {
            return reinterpret_cast<soAnimCmdModule*>(m_module);
        }
        // The address of control unit `unit` (16 bytes: interpreter,
        // address packs, index, type mask).
        u32 unitAddress(int unit) const;

    private:
        u32 m_module;
    };
}
