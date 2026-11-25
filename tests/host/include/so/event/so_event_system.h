#pragma once

#include <types.h>

// Stand-in for Brawl's soEventSystem / soEventManager in PC host tests.
class soEventManager {
public:
    enum { kMaxUnits = 8, kMaxSlots = 8 };
    struct Unit {
        void* slots[kMaxSlots];
        int capacity;
    };
    Unit m_units[kMaxUnits];

    soEventManager()
    {
        reset();
    }
    void reset()
    {
        for (int u = 0; u < kMaxUnits; u++)
        {
            m_units[u].capacity = kMaxSlots;
            for (int s = 0; s < kMaxSlots; s++)
            {
                m_units[u].slots[s] = NULL;
            }
        }
    }
    int addToUnit(int unit, void* obs)
    {
        if (unit < 0 || unit >= kMaxUnits)
        {
            return -1;
        }
        for (int s = 0; s < m_units[unit].capacity && s < kMaxSlots; s++)
        {
            if (m_units[unit].slots[s] == NULL)
            {
                m_units[unit].slots[s] = obs;
                return s;
            }
        }
        return -1;
    }
    void eraseObserver(s16 unit, s16 sendId)
    {
        if (unit >= 0 && unit < kMaxUnits && sendId >= 0 && sendId < kMaxSlots)
        {
            m_units[unit].slots[sendId] = NULL;
        }
    }
    int observerCount(int unit) const
    {
        if (unit < 0 || unit >= kMaxUnits)
        {
            return 0;
        }
        int n = 0;
        for (int s = 0; s < kMaxSlots; s++)
        {
            if (m_units[unit].slots[s] != NULL)
            {
                n++;
            }
        }
        return n;
    }
    bool containsObserver(int unit, const void* obs) const
    {
        if (unit < 0 || unit >= kMaxUnits)
        {
            return false;
        }
        for (int s = 0; s < kMaxSlots; s++)
        {
            if (m_units[unit].slots[s] == obs)
            {
                return true;
            }
        }
        return false;
    }
    u32 getObserverCapacity(int unit) const
    {
        if (unit < 0 || unit >= kMaxUnits)
        {
            return 0;
        }
        return observerCount(unit) < m_units[unit].capacity ? 1u : 0u;
    }
    u32 getObserverSize(int unit) const
    {
        return unit >= 0 && unit < kMaxUnits ? static_cast<u32>(m_units[unit].capacity) : 0u;
    }
};

class soEventInstanceManager {
public:
    enum { kMaxManagers = 32 };
    soEventManager* m_managers[kMaxManagers];
    bool m_live[kMaxManagers];

    soEventInstanceManager()
    {
        reset();
    }
    void reset()
    {
        for (int i = 0; i < kMaxManagers; i++)
        {
            m_managers[i] = NULL;
            m_live[i] = false;
        }
    }
    bool isContain(s32 id) const
    {
        return id >= 0 && id < kMaxManagers && m_live[id];
    }
};

class soEventSystem {
public:
    soEventInstanceManager m_inst;

    static soEventSystem* getInstance()
    {
        static soEventSystem s_inst;
        return &s_inst;
    }
    void reset()
    {
        m_inst.reset();
    }
    void registerManager(s16 id, soEventManager* mgr)
    {
        if (id >= 0 && id < soEventInstanceManager::kMaxManagers)
        {
            m_inst.m_managers[id] = mgr;
            m_inst.m_live[id] = true;
        }
    }
    void removeManager(s16 id)
    {
        if (id >= 0 && id < soEventInstanceManager::kMaxManagers)
        {
            m_inst.m_managers[id] = NULL;
            m_inst.m_live[id] = false;
        }
    }
    const soEventInstanceManager& getInstanceManager() const
    {
        return m_inst;
    }
    soEventManager* getManager(s32 id) const
    {
        return (id >= 0 && id < soEventInstanceManager::kMaxManagers && m_inst.m_live[id]) ? m_inst.m_managers[id]
                                                                                           : NULL;
    }
};

template <typename T>
class soEventObserver {
public:
    s16 m_manageID;
    s16 m_unitID;
    s16 m_sendID;

    soEventObserver(s16 unitID = -1) : m_manageID(-1), m_unitID(unitID), m_sendID(-1)
    {
    }
    virtual void addObserver(s16 manageId, s8)
    {
        soEventSystem* sys = soEventSystem::getInstance();
        if (manageId < 0 || m_unitID < 0 || !sys->getInstanceManager().isContain(manageId))
        {
            return;
        }
        soEventManager* mgr = sys->getManager(manageId);
        int send = mgr->addToUnit(m_unitID, static_cast<T*>(this));
        if (send >= 0)
        {
            m_manageID = manageId;
            m_sendID = static_cast<s16>(send);
        }
    }
    void removeObserver(s16 manageId)
    {
        soEventSystem* sys = soEventSystem::getInstance();
        if (manageId >= 0 && sys->getInstanceManager().isContain(manageId))
        {
            sys->getManager(manageId)->eraseObserver(m_unitID, m_sendID);
        }
        m_manageID = -1;
        m_sendID = -1;
    }
};
