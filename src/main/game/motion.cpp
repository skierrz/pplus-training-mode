#include "main/game/motion.h"

#include <so/motion/so_motion_module_impl.h>
#include <so/so_module_accesser.h>

#include "main/game/fighters.h"
#include "shared/game/mem.h"

// soMotionModuleImpl (header): m_kind is the subaction id; the main
// animation (soAnimChr at +0x3C) holds the nw4r AnmObjChrRes and the frame
// (+0x40, observed counting up by one per frame).
// The animation resource (CHR0) holds its frame count and, at its string
// offset, its name (ResAnmChrData, header); ProjectPunch shows the same name
// through soMotionModule::getName (main.cpp, MIT; see
// THIRD_PARTY_NOTICES.md). Observed in the emulator: "Wait1", "Attack11".
namespace tm {
namespace game {
    enum { kMainAnimRes = 0x3C, kMainAnimFrame = 0x40 };

    static bool printable(const char* s)
    {
        for (int i = 0; i < 32; i++)
        {
            char c = s[i];
            if (c == '\0')
            {
                return i > 0;
            }
            if (c < 0x20 || c > 0x7E)
            {
                return false;
            }
        }
        return false;
    }

    bool readMotion(Fighter* fighter, MotionInfo& out)
    {
        const soModuleEnumeration* m = fighterModules(fighter);
        if (m == NULL || !isRam(m->m_motionModule))
        {
            return false;
        }
        const soMotionModuleImpl* motion = static_cast<const soMotionModuleImpl*>(m->m_motionModule);
        out.kind = motion->m_kind;
        out.exactFrame = *reinterpret_cast<const float*>(reinterpret_cast<u32>(motion) + kMainAnimFrame);
        out.frame = static_cast<int>(out.exactFrame);
        out.length = 0;
        out.name = NULL;
        const nw4r::g3d::AnmObjChrRes* res =
            reinterpret_cast<const nw4r::g3d::AnmObjChrRes*>(wordAt(motion, kMainAnimRes));
        if (!isRam(res))
        {
            return true;
        }
        const nw4r::g3d::ResAnmChrData* data = res->m_anmChrFile.ptr();
        if (!isRam(data))
        {
            return true;
        }
        out.length = data->m_animLength;
        const char* name = reinterpret_cast<const char*>(data) + data->m_stringOffset;
        if (isRam(name) && printable(name))
        {
            out.name = name;
        }
        return true;
    }
}
}
