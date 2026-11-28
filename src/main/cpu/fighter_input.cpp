#include "main/cpu/fighter_input.h"

#include <ai/ai_mgr.h>
#include <ft/ft_entry.h>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <so/controller/so_controller_impl.h>
#include <so/controller/so_controller_module_impl.h>

#include "main/cpu/view.h"
#include "main/game/fighters.h"
#include "shared/game/mem.h"

// Offsets read in the emulator (a whole frame's instruction trace, memory
// under each Training CPU option):
//  - aiInput +0xC buttons (held), +0x10/+0x14 main stick, +0x20/+0x24
//    triggers (always 0 as produced; the controller ORs in guard when the
//    larger is > 0), +0x2C the owning fighter's entry id (Nana: Popo's).
//  - ftEntry m_input -> ftInput: +0xC the main aiInput, +0x10 Nana's (0 for
//    other fighters).
//  - aiMgr +0x84 + slot: u8 per slot 0-6, written by aiMgr::set_ai_player
//    (0x808FC36C, stb at 0x808FC3B0), read by aiMgr::chkIpHumanUpdate
//    (0x808FC8A4, lbz at 0x808FC950) and P+'s Random DI (C2 hook at
//    0x80877954, by the ftEntry slot; Source/Project+/TrainingMode.asm:263,
//    skipped when the byte is 0). Symbol map: fn_27_1F1958, fn_27_1F1E90.
//  - aiInput +0x113: CPU type; Training's Control sets 25 when its menu
//    closes (its other options set other types).
namespace tm {
namespace cpu {
    enum {
        kButtons = 0x0C,
        kStickX = 0x10,
        kStickY = 0x14,
        kTriggerL = 0x20,
        kTriggerR = 0x24,
        kHumanPadFlag = 0x28,
        kFtInputMain = 0x0C,
        kFtInputSub = 0x10,
        kAiMgrSlotFlags = 0x84,
        kCpuType = 0x113,
        kAiMgrSlots = 7,
        // Exceeds Brawl's soControllerImpl smash/SDI flick timer window so a
        // held TDI stick is never misread as a fresh SDI flick.
        kHeldFlick = 6,
        kModuleController = 0x14C
    };
    static const u32 kControllerVtable = 0x80AE4EF0; // soControllerImpl

    static float& floatAt(void* base, u32 offset)
    {
        return *reinterpret_cast<float*>(reinterpret_cast<u32>(base) + offset);
    }

    bool findOwner(const void* input, int entryId, Owner& out)
    {
        ftManager* manager = g_ftManager;
        ftEntryManager* entries = g_ftEntryManager;
        if (!game::isRam(input) || !game::isRam(manager) || !game::isRam(entries) || !manager->isValidEntryId(entryId))
        {
            return false;
        }
        ftEntry* entry = entries->getEntity(entryId);
        if (!game::isRam(entry) || entry->m_entryId != entryId || !game::isRam(entry->m_input))
        {
            return false;
        }
        u32 address = reinterpret_cast<u32>(input);
        bool main = game::wordAt(entry->m_input, kFtInputMain) == address;
        bool sub = game::wordAt(entry->m_input, kFtInputSub) == address;
        if (!main && !sub)
        {
            return false;
        }
        out.entryId = entryId;
        out.playerNo = entry->m_playerNo;
        out.slot = entry->m_slotIndex;
        out.cpu = game::cpuPlayer(out.playerNo, manager->getFighterOperationType(entryId) != 0);
        out.sub = sub;
        out.trainingControl = game::byteAt(input, kCpuType) == kCpuTypeControl;
        out.input = const_cast<void*>(input);
        return true;
    }

    void readFrame(const void* input, Frame& out)
    {
        void* object = const_cast<void*>(input);
        out.buttons = game::wordAt(input, kButtons);
        out.x = floatAt(object, kStickX);
        out.y = floatAt(object, kStickY);
    }

    void writeFrame(void* input, const Frame& frame)
    {
        *reinterpret_cast<u32*>(reinterpret_cast<u32>(input) + kButtons) = frame.buttons;
        floatAt(input, kStickX) = frame.x;
        floatAt(input, kStickY) = frame.y;
        floatAt(input, kTriggerL) = 0.0f;
        floatAt(input, kTriggerR) = 0.0f;
    }

    bool holdStickStill(int entryId, bool sub, const Frame& frame)
    {
        const soModuleEnumeration* m = game::fighterModules(fighterOf(entryId, sub));
        if (m == NULL || !game::isRam(m->m_controllerModule))
        {
            return false;
        }
        // The fighter's controller module forwards getFlickX, getStickX and
        // the other controller getters to the controller at its +0x14C (its
        // getFlickX at 0x8085B9D4; its getController() gives another one,
        // at +0x30): the one update() fills from the fighter's input.
        soControllerImpl* c =
            reinterpret_cast<soControllerImpl*>(game::wordAt(m->m_controllerModule, kModuleController));
        if (!game::isRam(c) || game::wordAt(c, 0) != kControllerVtable)
        {
            return false;
        }
        c->m_mainStickPrevX = frame.x;
        c->m_mainStickPrevY = frame.y;
        if (c->m_flickX < kHeldFlick)
        {
            c->m_flickX = kHeldFlick;
        }
        if (c->m_flickY < kHeldFlick)
        {
            c->m_flickY = kHeldFlick;
        }
        return true;
    }

    static u8* aiFlagAt(int slot)
    {
        if (!game::isRam(g_aiMgr) || slot < 0 || slot >= kAiMgrSlots)
        {
            return NULL;
        }
        return reinterpret_cast<u8*>(reinterpret_cast<u32>(g_aiMgr) + kAiMgrSlotFlags + slot);
    }

    bool aiFlag(int slot, u8& out)
    {
        u8* flag = aiFlagAt(slot);
        if (flag == NULL)
        {
            return false;
        }
        out = *flag;
        return true;
    }

    bool setAiFlag(int slot, u8 value)
    {
        u8* flag = aiFlagAt(slot);
        if (flag == NULL)
        {
            return false;
        }
        *flag = value;
        return true;
    }

    int cpuType(const void* input)
    {
        return game::isRam(input) ? game::byteAt(input, kCpuType) : -1;
    }

    void setCpuType(void* input, int type)
    {
        if (game::isRam(input))
        {
            *reinterpret_cast<u8*>(reinterpret_cast<u32>(input) + kCpuType) = static_cast<u8>(type);
        }
    }

    u8 humanPadFlag(const void* input)
    {
        return game::isRam(input) ? game::byteAt(input, kHumanPadFlag) : 0u;
    }

    void setHumanPadFlag(void* input, u8 flag)
    {
        if (game::isRam(input))
        {
            *reinterpret_cast<u8*>(reinterpret_cast<u32>(input) + kHumanPadFlag) = flag;
        }
    }
}
}
