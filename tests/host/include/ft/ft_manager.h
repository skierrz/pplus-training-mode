#pragma once

#include <ft/ft_entry_manager.h>
#include <so/event/so_event_manage_module_impl.h>
#include <so/event/so_event_system.h>
#include <types.h>

class ftOutsideEventObserver : public soEventObserver<ftOutsideEventObserver> {
public:
    ftOutsideEventObserver(s16 unitID = 0) : soEventObserver<ftOutsideEventObserver>(unitID)
    {
    }
    virtual void notifyEventRemoveEntry(int)
    {
    }
};

class ftManager : public ftOutsideEventObserver {
public:
    soEventManageModuleImpl m_eventManageModule;

    ftManager() : ftOutsideEventObserver(0)
    {
    }
    Fighter* getSubFighter(int entryId) const
    {
        ftEntry* e = g_ftEntryManager != NULL ? g_ftEntryManager->getEntity(entryId) : NULL;
        return e != NULL ? e->m_subFighter : NULL;
    }
    int getEntryCount() const
    {
        if (g_ftEntryManager == NULL)
        {
            return 0;
        }
        int n = 0;
        for (int i = 0; i < ftEntryManager::kMaxEntries; i++)
        {
            if (g_ftEntryManager->m_entries[i] != NULL)
            {
                n++;
            }
        }
        return n;
    }
    void notifyRemoveEntry(int entryId)
    {
        s16 mid = m_eventManageModule.getManageId();
        soEventSystem* sys = soEventSystem::getInstance();
        if (mid < 0 || !sys->getInstanceManager().isContain(mid))
        {
            return;
        }
        soEventManager* mgr = sys->getManager(mid);
        soEventManager::Unit& unit = mgr->m_units[0];
        // Snapshot pointers so an observer may unregister during notification.
        void* snap[soEventManager::kMaxSlots];
        for (int i = 0; i < soEventManager::kMaxSlots; i++)
        {
            snap[i] = unit.slots[i];
        }
        for (int i = 0; i < soEventManager::kMaxSlots; i++)
        {
            if (snap[i] != NULL)
            {
                static_cast<ftOutsideEventObserver*>(snap[i])->notifyEventRemoveEntry(entryId);
            }
        }
    }
};

extern ftManager* g_ftManager;
