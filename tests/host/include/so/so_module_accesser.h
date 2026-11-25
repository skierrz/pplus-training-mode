#pragma once

#include <types.h>

// Stand-in for the game's stage-object modules in the PC build
// (tests/host/). A fighter's modules here are host objects a test fills in
// (host/fakes.h makes a whole fake fighter): each class holds, as plain
// fields, what the game's virtual functions would answer, and the functions
// return the fields. Nothing of the game runs.
//
// soModuleEnumeration has the game's own layout (52 pointers, the order of
// lib/BrawlHeaders so/so_module_accesser.h), so an enumeration found in a
// memory dump can be read through it too. Modules the tested code only passes
// to the read helpers (isRam, wordAt) stay opaque; in a fake fighter they can
// point into the memory image.

enum SituationKind {
    Situation_None = -0x1,
    Situation_Ground = 0x0,
    Situation_Cliff = 0x1,
    Situation_Air = 0x2
};

class soMotionModule {
public:
    float getFrame() const
    {
        return m_frame;
    }
    float getRate() const
    {
        return m_rate;
    }
    int getOtherAnimSize() const
    {
        return 0;
    }
    int getOtherAnimKind(int) const
    {
        return -1;
    }
    void setOtherAnimFrame(float, int)
    {
    }
    // The bytes the mod reads by offset (the main animation's flags at
    // +0x54, bit 0x80: it loops). Must stay first.
    u8 m_raw[0x60];
    float m_frame;
    float m_rate;
};

class soSituationModule {
public:
    SituationKind getKind() const
    {
        return m_kind;
    }
    SituationKind m_kind;
};

class soStopModule {
public:
    s32 getHitStopRealFrame() const
    {
        return m_hitStopRealFrame;
    }
    s32 m_hitStopRealFrame;
};

class soStatusModule {
public:
    bool isEnableTransitionTermGroup(int group) const
    {
        return ((m_groups >> group) & 1) != 0;
    }
    int getStatusKind() const
    {
        return m_statusKind;
    }
    u32 m_groups; // bit g: transition term group g is enabled
    int m_statusKind;
};

class soEffectModuleImpl {
public:
    void kill(int, bool, bool)
    {
    }
};

// Work variables by their id (0x20000005 is RA-Basic[5], 0x12000003
// LA-Bit[3]); an id never set reads 0.
class soWorkManageModule {
public:
    enum { kMax = 32 };
    int getInt(u32 id) const
    {
        return find(id);
    }
    float getFloat(u32 id) const
    {
        return findFloat(id);
    }
    bool isFlag(u32 id) const
    {
        return find(id) != 0;
    }
    void setInt(int value, u32 id)
    {
        set(id, value);
    }
    void setFloat(float value, u32 id)
    {
        for (int i = 0; i < m_floatCount; i++)
        {
            if (m_floatIds[i] == id)
            {
                m_floatValues[i] = value;
                return;
            }
        }
        if (m_floatCount < kMax)
        {
            m_floatIds[m_floatCount] = id;
            m_floatValues[m_floatCount++] = value;
        }
    }
    void onFlag(u32 id)
    {
        set(id, 1);
    }
    void offFlag(u32 id)
    {
        set(id, 0);
    }
    void set(u32 id, int value)
    {
        for (int i = 0; i < m_count; i++)
        {
            if (m_ids[i] == id)
            {
                m_values[i] = value;
                return;
            }
        }
        if (m_count < kMax)
        {
            m_ids[m_count] = id;
            m_values[m_count++] = value;
        }
    }
    int m_count;
    u32 m_ids[kMax];
    int m_values[kMax];
    int m_floatCount;
    u32 m_floatIds[kMax];
    float m_floatValues[kMax];

private:
    int find(u32 id) const
    {
        for (int i = 0; i < m_count; i++)
        {
            if (m_ids[i] == id)
            {
                return m_values[i];
            }
        }
        return 0;
    }
    float findFloat(u32 id) const
    {
        for (int i = 0; i < m_floatCount; i++)
        {
            if (m_floatIds[i] == id)
            {
                return m_floatValues[i];
            }
        }
        return 0.0f;
    }
};

class soAnimCmdModule;
class soCollisionAttackModule;
class soCollisionHitModule;
class soEventManageModule;
class soItemManageModule;
class soPostureModule;

class soModuleEnumeration {
public:
    void* m_resourceModule;
    void* m_modelModule;
    soMotionModule* m_motionModule;
    soPostureModule* m_postureModule;
    void* m_groundModule;
    soSituationModule* m_situationModule;
    void* m_teamModule;
    soCollisionAttackModule* m_collisionAttackModule;
    soCollisionHitModule* m_collisionHitModule;
    void* m_collisionShieldModule;
    void* m_collisionReflectorModule;
    void* m_collisionAbsorberModule;
    void* m_collisionCatchModule;
    void* m_collisionSearchModule;
    void* m_damageModule;
    void* m_catchModule;
    void* m_captureModule;
    soStopModule* m_stopModule;
    void* m_turnModule;
    void* m_shakeModule;
    void* m_soundModule;
    void* m_linkModule;
    void* m_visibilityModule;
    void* m_controllerModule;
    void* m_cameraModule;
    soWorkManageModule* m_workManageModule;
    void* m_debugModule;
    soAnimCmdModule* m_animCmdModule;
    soStatusModule* m_statusModule;
    void* m_generalTermDecideModule;
    void* m_switchDecideModule;
    void* m_kineticModule;
    soEventManageModule* m_eventManageModule;
    void* m_generateArticleManageModule;
    soEffectModuleImpl* m_effectModule;
    void* m_comboModule;
    void* m_areaModule;
    void* m_territoryModule;
    void* m_targetSearchModule;
    void* m_physicsModule;
    void* m_slopeModule;
    void* m_shadowModule;
    soItemManageModule* m_itemManageModule;
    void* m_colorBlendModule;
    void* m_jostleModule;
    void* m_abnormalModule;
    void* m_slowModule;
    void* m_reflectModule;
    void* m_heapModule;
    void* m_paramCustomizeModule;
    void* m_glowModule;
};
typedef char host_module_enumeration_size[sizeof(soModuleEnumeration) == 0xCC ? 1 : -1];

// Only what the mod reads: where the fighter's modules are. The customize
// parameters soValueAccesser::getConstantFloat answers for this fighter are
// kept here too (an id never set reads 0).
class soModuleAccesser {
public:
    enum { kMaxConstants = 8 };
    const soModuleEnumeration* m_enumerationStart;
    int m_constantCount;
    u32 m_constantIds[kMaxConstants];
    float m_constants[kMaxConstants];
    void setConstant(u32 id, float value)
    {
        for (int i = 0; i < m_constantCount; i++)
        {
            if (m_constantIds[i] == id)
            {
                m_constants[i] = value;
                return;
            }
        }
        if (m_constantCount < kMaxConstants)
        {
            m_constantIds[m_constantCount] = id;
            m_constants[m_constantCount++] = value;
        }
    }
};

class soValueAccesser {
public:
    static float getConstantFloat(soModuleAccesser* accesser, u32 id, u32 = 0)
    {
        for (int i = 0; i < accesser->m_constantCount; i++)
        {
            if (accesser->m_constantIds[i] == id)
            {
                return accesser->m_constants[i];
            }
        }
        return 0.0f;
    }
    static s32 getConstantInt(soModuleAccesser* accesser, u32 id, u32 = 0)
    {
        return static_cast<s32>(getConstantFloat(accesser, id, 0));
    }
};
