#pragma once

#include <ft/fighter.h>
#include <types.h>

struct ftEntryInstance {
    int m_kind;
    Fighter* m_fighter;
};

class ftOwner {};

class ftEntry {
public:
    int m_entryId;
    int m_playerNo;
    int m_activeInstanceIndex;
    ftEntryInstance m_instances[4];
    Fighter* m_subFighter;
    ftOwner* m_owner;

    ftEntry() : m_entryId(-1), m_playerNo(-1), m_activeInstanceIndex(0), m_subFighter(NULL), m_owner(NULL)
    {
        for (int i = 0; i < 4; i++)
        {
            m_instances[i].m_kind = -1;
            m_instances[i].m_fighter = NULL;
        }
    }
};

class ftEntryManager {
public:
    enum { kMaxEntries = 8 };
    ftEntry* m_entries[kMaxEntries];

    void reset()
    {
        for (int i = 0; i < kMaxEntries; i++)
        {
            m_entries[i] = NULL;
        }
    }
    void setSlot(int idx, ftEntry* e)
    {
        if (idx >= 0 && idx < kMaxEntries)
        {
            m_entries[idx] = e;
        }
    }
    ftEntry* getEntity(int entryId) const
    {
        for (int i = 0; i < kMaxEntries; i++)
        {
            if (m_entries[i] != NULL && m_entries[i]->m_entryId == entryId)
            {
                return m_entries[i];
            }
        }
        return NULL;
    }
};

extern ftEntryManager* g_ftEntryManager;
