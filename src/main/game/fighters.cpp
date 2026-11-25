#include "main/game/fighters.h"

#include <ft/ft_entry_manager.h>
#include <gm/gm_global.h>
#include <ft/ft_manager.h>
#include <so/so_module_accesser.h>

#include "shared/game/mem.h"

// Where each value comes from: docs/internals.md, "Fighter values".
namespace tm {
namespace game {
    enum {
        kPosturePos = 0xC,
        kPosturePrevPos = 0x18,
        kStatusKind = 0x34,
        kWorkInstance = 0x20,
        kWorkStatus = 0x24,
        kGeneralWorkInts = 0xC,
        kGeneralWorkIntCount = 0x10,
        kHitstunInt = 0x38,
        kShieldStunInt = 5,
        kStateHuman = 0,
        kStateCpu = 1
    };

    const soModuleEnumeration* fighterModules(Fighter* fighter)
    {
        if (!isRam(fighter))
        {
            return NULL;
        }
        soModuleAccesser* accesser = fighter->m_moduleAccesser;
        if (!isRam(accesser) || !isRam(accesser->m_enumerationStart))
        {
            return NULL;
        }
        return accesser->m_enumerationStart;
    }

    static float floatAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
    }

    // Int `index` of the work `which` (kWorkInstance or kWorkStatus), or 0
    // if the work or index is not there.
    static int workInt(const void* workModule, u32 which, u32 index)
    {
        const void* work = reinterpret_cast<const void*>(wordAt(workModule, which));
        if (!isRam(work))
        {
            return 0;
        }
        const s32* ints = reinterpret_cast<const s32*>(wordAt(work, kGeneralWorkInts));
        if (!isRam(ints) || index >= wordAt(work, kGeneralWorkIntCount))
        {
            return 0;
        }
        return ints[index];
    }

    static bool fill(int entryId, FighterRef& ref)
    {
        ftManager* manager = g_ftManager;
        ftEntryManager* entries = g_ftEntryManager;
        if (!isRam(manager) || !isRam(entries))
        {
            return false;
        }
        ftEntry* entry = entries->getEntity(entryId);
        if (!isRam(entry) || entry->m_entryId != entryId)
        {
            return false;
        }
        int active = entry->m_activeInstanceIndex;
        if (active < 0 || active >= 4)
        {
            return false;
        }
        Fighter* fighter = entry->m_instances[active].m_fighter;
        ftOwner* owner = manager->getOwner(entryId);
        if (fighterModules(fighter) == NULL || !isRam(owner))
        {
            return false;
        }
        ref.entryId = entryId;
        ref.playerNo = entry->m_playerNo;
        ref.cpu = cpuPlayer(ref.playerNo, manager->getFighterOperationType(entryId) != 0);
        ref.port = ref.cpu ? -1 : ref.playerNo;
        ref.fighter = fighter;
        Fighter* partner = manager->getSubFighter(entryId);
        ref.partner = (isRam(partner) && partner != fighter) ? partner : NULL;
        ref.owner = owner;
        return true;
    }

    int listFighters(FighterRef* out, int max)
    {
        ftManager* manager = g_ftManager;
        if (!isRam(manager))
        {
            return 0;
        }
        int count = manager->getEntryCount();
        int n = 0;
        for (int i = 0; i < count && n < max; i++)
        {
            if (fill(manager->getEntryIdFromIndex(i), out[n]))
            {
                n++;
            }
        }
        return n;
    }

    bool findFighter(bool cpu, FighterRef& out)
    {
        FighterRef refs[kMaxFighters];
        int n = listFighters(refs, kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            if (refs[i].cpu == cpu)
            {
                out = refs[i];
                return true;
            }
        }
        return false;
    }

    bool readState(const FighterRef& ref, FighterState& out)
    {
        const soModuleEnumeration* m = fighterModules(ref.fighter);
        if (m == NULL || !isRam(m->m_postureModule) || !isRam(m->m_statusModule)
            || !isRam(m->m_workManageModule) || !isRam(ref.owner))
        {
            return false;
        }
        const void* posture = m->m_postureModule;
        out.x = floatAt(posture, kPosturePos);
        out.y = floatAt(posture, kPosturePos + 4);
        out.vx = out.x - floatAt(posture, kPosturePrevPos);
        out.vy = out.y - floatAt(posture, kPosturePrevPos + 4);
        out.status = static_cast<s32>(wordAt(m->m_statusModule, kStatusKind));
        out.damage = ref.owner->getDamage();
        out.hitstun = workInt(m->m_workManageModule, kWorkInstance, kHitstunInt);
        out.shieldStun = out.status == kStatusGuardDamage
                             ? workInt(m->m_workManageModule, kWorkStatus, kShieldStunInt)
                             : 0;
        if (out.hitstun < 0)
        {
            out.hitstun = 0;
        }
        if (out.shieldStun < 0)
        {
            out.shieldStun = 0;
        }
        return true;
    }

    int fighterHitstun(const soModuleEnumeration* m)
    {
        int v = isRam(m->m_workManageModule) ? workInt(m->m_workManageModule, kWorkInstance, kHitstunInt) : 0;
        return v > 0 ? v : 0;
    }

    int fighterShieldStun(const soModuleEnumeration* m, int status)
    {
        if (status != kStatusGuardDamage || !isRam(m->m_workManageModule))
        {
            return 0;
        }
        int v = workInt(m->m_workManageModule, kWorkStatus, kShieldStunInt);
        return v > 0 ? v : 0;
    }

    int fighterKind(int entryId, bool partner)
    {
        ftEntryManager* entries = g_ftEntryManager;
        ftEntry* entry = isRam(entries) ? entries->getEntity(entryId) : NULL;
        if (!isRam(entry) || entry->m_entryId != entryId)
        {
            return -1;
        }
        int active = entry->m_activeInstanceIndex;
        if (active < 0 || active >= 4)
        {
            return -1;
        }
        int kind = static_cast<int>(entry->m_instances[active].m_kind);
        if (partner && kind == Fighter_Popo)
        {
            return Fighter_Nana;
        }
        return kind;
    }

    int fighterStatus(const soModuleEnumeration* m)
    {
        return isRam(m->m_statusModule) ? static_cast<s32>(wordAt(m->m_statusModule, kStatusKind)) : -1;
    }

    bool outOfPlay(int status, bool platform)
    {
        return status == kStatusDead || status == kStatusStandby || (platform && status == kStatusRebirth);
    }

    int playerState(int playerNo)
    {
        GameGlobal* global = g_GameGlobal;
        if (!isRam(global) || !isRam(global->m_modeMelee) || playerNo < 0 || playerNo >= MAX_PLAYERS)
        {
            return -1;
        }
        return global->m_modeMelee->m_playersInitData[playerNo].m_state;
    }

    bool cpuPlayer(int playerNo, bool fallback)
    {
        int state = playerState(playerNo);
        return state < 0 ? fallback : state == kStateCpu;
    }

    bool humanPlayer(int playerNo)
    {
        return playerState(playerNo) == kStateHuman;
    }

    bool playerInMatch(int playerNo)
    {
        GameGlobal* global = g_GameGlobal;
        if (!isRam(global) || !isRam(global->m_modeMelee) || playerNo < 0 || playerNo >= MAX_PLAYERS)
        {
            return true;
        }
        u8 state = global->m_modeMelee->m_playersInitData[playerNo].m_state;
        return state == kStateHuman || state == kStateCpu;
    }

    float damage(const FighterRef& ref)
    {
        return isRam(ref.owner) ? ref.owner->getDamage() : 0.0f;
    }

    void setDamage(const FighterRef& ref, float percent)
    {
        if (isRam(ref.owner))
        {
            ref.owner->setDamage(percent, false);
        }
    }
}
}
