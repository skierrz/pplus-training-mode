#include "host/dump.h"

#include "host/mem.h"

namespace host {
namespace dump {
    static const u32 kAnimCmdVtable = 0x80AE70D8u;
    static const u32 kActionTable = 0x80FBFF8Cu;
    enum {
        kThreads = 0x20,
        kUnits = 0x1C,
        kUnitSize = 0x10,
        kUnitIndex = 0x8,
        kUnitCount = 11,
        kInterpStart = 0x10,
        kEnumAnimCmd = 0x6C,
        kEnumMotion = 0x08,
        kEnumStatus = 0x70,
        kAccesserObject = 0x08,
        kAccesserEnum = 0x0C,
        kAccesserEnumStart = 0xD8,
        kFighterAccesser = 0x60,
        kMotionKind = 0x58,
        kStatusKind = 0x34,
        kEntryActive = 0x0A,
        kEntryInstances = 0x30,
        kEntryPlayer = 0x58,
        kStatusCount = 0x112,
        kMaxSubactions = 0x400
    };

    static bool ram(u32 a)
    {
        return mem::inImage(a) && (a & 3) == 0;
    }

    // The script the subaction thread of `module` runs, 0 if the module is
    // not a fighter-like animation-command module.
    static u32 subactionScript(u32 module)
    {
        u32 threads = mem::word(module + kThreads);
        if (!ram(threads) || !mem::inImage(threads + kUnits + kUnitSize * kUnitCount))
        {
            return 0;
        }
        for (u32 i = 0; i < kUnitCount; i++)
        {
            u32 unit = threads + kUnits + kUnitSize * i;
            if (!ram(mem::word(unit)) || mem::word(unit + kUnitIndex) != i)
            {
                return 0;
            }
        }
        u32 script = mem::word(mem::word(threads + kUnits + kUnitSize) + kInterpStart);
        return ram(script) ? script : 0;
    }

    static int kindOf(u32 fighter)
    {
        for (u32 at = mem::kMem1 + kEntryInstances + 4; at < mem::kMem1 + mem::kMem1Size - 0x60; at += 4)
        {
            if (mem::word(at) != fighter)
            {
                continue;
            }
            u32 kind = mem::word(at - 4);
            if (kind >= 0x80)
            {
                continue;
            }
            // One of the entry's four instances {kind, Fighter*}.
            for (u32 slot = 0; slot < 4; slot++)
            {
                u32 entry = at - 4 - kEntryInstances - 8 * slot;
                u32 vtable = mem::word(entry);
                if (vtable >= mem::kMem1 && vtable < mem::kMem1 + 0x00C00000u && mem::byte(entry + kEntryActive) == slot
                    && mem::word(entry + kEntryPlayer) < 8)
                {
                    return static_cast<int>(kind);
                }
            }
        }
        return -1;
    }

    static u32 subactionTable(u32 script, int subaction)
    {
        if (subaction < 0 || subaction >= kMaxSubactions)
        {
            return 0;
        }
        for (u32 at = mem::kMem2; at < mem::kMem2 + mem::kMem2Size; at += 4)
        {
            if (mem::word(at) != script)
            {
                continue;
            }
            u32 table = at - 4 * static_cast<u32>(subaction);
            if (table < mem::kMem2)
            {
                continue;
            }
            // Every entry up to this one is a script in MEM2 or nothing.
            bool good = true;
            for (int i = 0; i < subaction && good; i++)
            {
                u32 entry = mem::word(table + 4 * i);
                good = entry == 0 || (ram(entry) && entry >= mem::kMem2);
            }
            if (good)
            {
                return table;
            }
        }
        return 0;
    }

    int fighters(FighterInfo* out, int max)
    {
        int count = 0;
        // The modules are in MEM1 (the fighters' instance heaps); a
        // savestate of the mod can hold copies of them in MEM2, which are
        // not looked at.
        for (u32 module = mem::kMem1; module < mem::kMem1 + mem::kMem1Size - 0x100 && count < max; module += 4)
        {
            if (mem::word(module) != kAnimCmdVtable)
            {
                continue;
            }
            u32 script = subactionScript(module);
            if (script == 0)
            {
                continue;
            }
            // The module enumeration that lists this module, inside its
            // accesser, which points back at it and at its Fighter.
            for (u32 at = mem::kMem1 + 0x100; at < mem::kMem1 + mem::kMem1Size - 0x100; at += 4)
            {
                if (mem::word(at) != module)
                {
                    continue;
                }
                u32 modules = at - kEnumAnimCmd;
                u32 accesser = modules - kAccesserEnum;
                u32 fighter = mem::word(accesser + kAccesserObject);
                u32 motion = mem::word(modules + kEnumMotion);
                u32 status = mem::word(modules + kEnumStatus);
                if (mem::word(accesser + kAccesserEnumStart) != modules || !ram(fighter)
                    || mem::word(fighter + kFighterAccesser) != accesser || !ram(motion) || !ram(status))
                {
                    continue;
                }
                FighterInfo& f = out[count++];
                f.fighter = fighter;
                f.modules = modules;
                f.animCmd = module;
                f.subaction = static_cast<int>(mem::word(motion + kMotionKind));
                f.status = static_cast<int>(mem::word(status + kStatusKind));
                f.kind = kindOf(fighter);
                f.subactions = subactionTable(script, f.subaction);
                break;
            }
        }
        return count;
    }

    bool fighterOfKind(int kind, FighterInfo& out)
    {
        static FighterInfo s_found[8];
        static int s_count = -1;
        // Found once: a run has one dump, and the scan takes a moment.
        if (s_count < 0)
        {
            s_count = fighters(s_found, 8);
        }
        for (int i = 0; i < s_count; i++)
        {
            if (s_found[i].kind == kind)
            {
                out = s_found[i];
                return true;
            }
        }
        return false;
    }

    const u32* subaction(const FighterInfo& fighter, int index)
    {
        if (fighter.subactions == 0 || index < 0 || index >= kMaxSubactions)
        {
            return NULL;
        }
        u32 script = mem::word(fighter.subactions + 4 * static_cast<u32>(index));
        return ram(script) ? reinterpret_cast<const u32*>(script) : NULL;
    }

    const u32* actionScript(int status)
    {
        if (status < 0 || status >= kStatusCount)
        {
            return NULL;
        }
        u32 script = mem::word(kActionTable + 4 * static_cast<u32>(status));
        return ram(script) ? reinterpret_cast<const u32*>(script) : NULL;
    }
}
}
