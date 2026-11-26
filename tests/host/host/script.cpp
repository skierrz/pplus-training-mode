#include "host/script.h"

#include "host/mem.h"

namespace host {
    // Lists from 0x92000000 up, arguments from 0x92400000, modules from
    // 0x92800000: Broadway-safe MEM2 (< 0x93600000), far from each other.
    static const u32 kLists = 0x92000000u;
    static const u32 kArgs = 0x92400000u;
    static const u32 kModules = 0x92800000u;
    static u32 s_nextList = kLists;
    static u32 s_nextArg = kArgs;
    static u32 s_nextModule = kModules;

    void Script::reset()
    {
        s_nextList = kLists;
        s_nextArg = kArgs;
        s_nextModule = kModules;
    }

    Script::Script() : m_start(s_nextList), m_next(s_nextList), m_last(0)
    {
        s_nextList += kRoom;
    }

    Script& Script::cmd(u32 group, u32 type)
    {
        if (m_next + 8 > m_start + kRoom)
        {
            printf("host: a Script holds at most %d commands\n", static_cast<int>(kRoom / 8));
            exit(3);
        }
        m_last = m_next;
        mem::setWord(m_last, (group << 24) | (type << 16));
        mem::setWord(m_last + 4, 0);
        m_next += 8;
        return *this;
    }

    Script& Script::raw(u32 kind, u32 value)
    {
        u32 head = mem::word(m_last);
        u32 count = (head >> 8) & 0xFF;
        if (count == 0)
        {
            mem::setWord(m_last + 4, s_nextArg);
        }
        // A command's arguments are one after the other: nothing else may
        // have taken an argument since its first.
        if (mem::word(m_last + 4) + 8 * count != s_nextArg)
        {
            printf("host: Script: give a command all its arguments before the next command\n");
            exit(3);
        }
        mem::setWord(s_nextArg, kind);
        mem::setWord(s_nextArg + 4, value);
        s_nextArg += 8;
        mem::setWord(m_last, (head & ~0xFF00u) | ((count + 1) << 8));
        return *this;
    }

    Script& Script::integer(s32 value)
    {
        return raw(0, static_cast<u32>(value));
    }

    Script& Script::real(float value)
    {
        double scaled = static_cast<double>(value) * 60000.0;
        return raw(1, static_cast<u32>(static_cast<s32>(scaled < 0 ? scaled - 0.5 : scaled + 0.5)));
    }

    Script& Script::pointer(u32 address)
    {
        return raw(2, address);
    }

    Script& Script::boolean(bool value)
    {
        return raw(3, value ? 1 : 0);
    }

    Script& Script::variable(u32 id)
    {
        return raw(5, id);
    }

    Script& Script::requirement(u32 id)
    {
        return raw(6, id);
    }

    Script& Script::filler(int count)
    {
        for (int i = 0; i < count; i++)
        {
            cmd(0x12, 0x0A).variable(0x2200001Eu);
        }
        return *this;
    }

    enum {
        kThreads = 0x20, // module -> thread vector
        kUnits = 0x1C,   // vector -> first unit
        kUnitSize = 0x10,
        kUnitCount = 11,
        kInterpSize = 0x40
    };

    AnimCmd::AnimCmd() : m_module(s_nextModule)
    {
        s_nextModule += 0x1000;
        u32 threads = m_module + 0x100;
        u32 interps = m_module + 0x400;
        static const u16 kMasks[kUnitCount] = {0x01, 0x02, 0x40, 0x40, 0x02, 0x20, 0x80, 0x80, 0x20, 0x04, 0x10};
        mem::setWord(m_module, 0x80AE70D8u); // the class's vtable, as in the game
        mem::setWord(m_module + kThreads, threads);
        for (u32 i = 0; i < kUnitCount; i++)
        {
            u32 unit = threads + kUnits + kUnitSize * i;
            mem::setWord(unit, interps + kInterpSize * i);
            mem::setWord(unit + 8, i);
            mem::setHalf(unit + 12, kMasks[i]);
        }
    }

    u32 AnimCmd::unitAddress(int unit) const
    {
        return m_module + 0x100 + kUnits + kUnitSize * unit;
    }

    void AnimCmd::setThread(int unit, const u32* start, const u32* current, float clock)
    {
        u32 interp = mem::word(unitAddress(unit));
        mem::setWord(interp + 0x0C, reinterpret_cast<u32>(current));
        mem::setWord(interp + 0x10, reinterpret_cast<u32>(start));
        mem::setReal(interp + 0x24, clock);
    }
}
