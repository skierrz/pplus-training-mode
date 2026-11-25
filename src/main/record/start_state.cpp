#include "main/record/start_state.h"

#include <ft/fighter.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/kinetic/so_kinetic_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/status/so_status_module_impl.h>
#include <so/stop/so_stop_module_impl.h>

#include "main/cpu/view.h"
#include "main/game/fighters.h"
#include "main/game/status_ids.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// Game calls (RSBE01 rev 2 symbols and lib/BrawlHeaders declarations; their
// effect is checked by scripts/record_tests.py on every run):
//  - Fighter::warp(Vec3f*, float lr, u32) (sora_melee .text+0x13C860):
//    puts the fighter at a position, facing lr.
//  - soStatusModule::changeStatus (header virtual) to Wait (0).
//  - soGroundModule::attachGround(0) and setStGroundForce(0) (header
//    virtuals): the fighter is attached to the ground line under it and
//    counts as standing there.
//  - soKineticModule::clearSpeedAll, soStopModule::cancelHitStop (header
//    virtuals; the stop module's getters are already used by cpu/view.cpp).
//  - The last hit's launch: the kinetic module's damage energy
//    (cpu::clearKnockback).
//  - Hitstun: instance work int 0x38 (see game/fighters.cpp), set to 0.
//  - Damage: ftOwner::setDamage as Training's own option (game::setDamage).
//  - Shield health: instance work float 3 (cpu::setShieldHealth).
//  - Position +0xC and facing +0x40 of the posture module (cpu/view.cpp).
namespace tm {
namespace record {
    enum {
        kPosturePos = 0xC,
        kPostureLr = 0x40,
        kWorkInstance = 0x20,
        kGeneralWorkInts = 0xC,
        kGeneralWorkIntCount = 0x10,
        kHitstunInt = 0x38
    };

    static float floatAt(const void* base, u32 offset)
    {
        return *reinterpret_cast<const float*>(reinterpret_cast<u32>(base) + offset);
    }

    static bool findPlayer(int player, game::FighterRef& out)
    {
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
            if (refs[i].playerNo == player)
            {
                out = refs[i];
                return true;
            }
        }
        return false;
    }

    static void setWorkInt(void* workModule, u32 index, s32 value)
    {
        void* work = reinterpret_cast<void*>(game::wordAt(workModule, kWorkInstance));
        if (!game::isRam(work))
        {
            return;
        }
        s32* ints = reinterpret_cast<s32*>(game::wordAt(work, kGeneralWorkInts));
        if (game::isRam(ints) && index < game::wordAt(work, kGeneralWorkIntCount))
        {
            ints[index] = value;
        }
    }

    struct Saved {
        int player;
        int entryId;
        float x, y, z;
        float lr;
        float damage;
        float shield;
        bool haveShield;
    };

    static bool capture(int player, Saved& out)
    {
        game::FighterRef ref;
        if (!findPlayer(player, ref))
        {
            return false;
        }
        const soModuleEnumeration* m = game::fighterModules(ref.fighter);
        if (m == NULL || !game::isRam(m->m_postureModule))
        {
            return false;
        }
        out.player = player;
        out.entryId = ref.entryId;
        out.x = floatAt(m->m_postureModule, kPosturePos);
        out.y = floatAt(m->m_postureModule, kPosturePos + 4);
        out.z = floatAt(m->m_postureModule, kPosturePos + 8);
        out.lr = floatAt(m->m_postureModule, kPostureLr) < 0.0f ? -1.0f : 1.0f;
        out.damage = game::damage(ref);
        out.shield = 0.0f;
        out.haveShield = cpu::shieldHealth(ref.fighter, out.shield);
        return true;
    }

    // Status, then warp, then the ground module, and the launch cleared:
    // docs/internals.md, "Putting a fighter back at its saved position".
    static bool place(Fighter* f, const Saved& s, bool mirror)
    {
        soModuleEnumeration* m = const_cast<soModuleEnumeration*>(game::fighterModules(f));
        if (m == NULL || !game::isRam(m->m_postureModule) || !game::isRam(m->m_statusModule)
            || !game::isRam(m->m_kineticModule) || !game::isRam(m->m_stopModule) || !game::isRam(m->m_workManageModule)
            || !game::isRam(m->m_groundModule))
        {
            return false;
        }
        Vec3f pos(mirror ? -s.x : s.x, s.y, s.z);
        float lr = mirror ? -s.lr : s.lr;
        if (m->m_stopModule->getHitStopRealFrame() > 0)
        {
            m->m_stopModule->cancelHitStop();
        }
        setWorkInt(m->m_workManageModule, kHitstunInt, 0);
        m->m_statusModule->changeStatus(game::kStatusWait, f->m_moduleAccesser);
        f->warp(&pos, lr, 0);
        if (m->m_groundModule->attachGround(0))
        {
            m->m_groundModule->setStGroundForce(0);
        }
        m->m_kineticModule->clearSpeedAll();
        cpu::clearKnockback(f);
        if (s.haveShield)
        {
            cpu::setShieldHealth(f, s.shield);
        }
        return true;
    }

    bool placeStanding(Fighter* f, float x, float y, float lr)
    {
        Saved s;
        s.x = x;
        s.y = y;
        s.z = 0.0f;
        s.lr = lr;
        s.haveShield = false;
        return place(f, s, false);
    }

    class LiteStartState : public StartState {
    public:
        Saved side[2];
        bool have;

        virtual const char* name() const
        {
            return "lite";
        }

        virtual bool save(const Players& who, const char*& why)
        {
            Saved hmn, cpu;
            if (!capture(who.hmn, hmn) || !capture(who.cpu, cpu))
            {
                why = "both fighters must be in the match";
                return false;
            }
            side[0] = hmn;
            side[1] = cpu;
            have = true;
            return true;
        }

        virtual bool restore(bool mirror, const int* extra, const char*& why)
        {
            if (!have)
            {
                why = "no positions saved";
                return false;
            }
            game::FighterRef refs[2];
            for (int i = 0; i < 2; i++)
            {
                if (!findPlayer(side[i].player, refs[i]) || refs[i].entryId != side[i].entryId)
                {
                    why = "a saved fighter left the match";
                    return false;
                }
            }
            for (int i = 0; i < 2; i++)
            {
                const Saved& s = side[i];
                if (!place(refs[i].fighter, s, mirror))
                {
                    why = "a fighter is not readable";
                    return false;
                }
                if (refs[i].partner != NULL)
                {
                    place(refs[i].partner, s, mirror);
                }
                if (extra != NULL)
                {
                    float damage = s.damage + static_cast<float>(extra[i]);
                    game::setDamage(refs[i], damage > 999.0f ? 999.0f : damage);
                }
            }
            return true;
        }

        virtual void clear()
        {
            have = false;
        }

        virtual bool saved() const
        {
            return have;
        }

        virtual Players players() const
        {
            Players p;
            p.hmn = have ? side[0].player : -1;
            p.cpu = have ? side[1].player : -1;
            return p;
        }
    };

    static LiteStartState s_lite;

    StartState& liteStartState()
    {
        return s_lite;
    }

    bool liteGet(LitePlacement out[2], Players& who)
    {
        if (!s_lite.have)
        {
            return false;
        }
        for (int i = 0; i < 2; i++)
        {
            const Saved& s = s_lite.side[i];
            out[i].x = s.x;
            out[i].y = s.y;
            out[i].z = s.z;
            out[i].lr = s.lr;
            out[i].damage = s.damage;
            out[i].shield = s.shield;
            out[i].haveShield = s.haveShield;
        }
        who = s_lite.players();
        return true;
    }

    bool liteSet(const Players& who, const LitePlacement in[2], const char*& why)
    {
        int players[2] = {who.hmn, who.cpu};
        Saved next[2];
        for (int i = 0; i < 2; i++)
        {
            game::FighterRef ref;
            if (!findPlayer(players[i], ref))
            {
                why = "both fighters must be in the match";
                return false;
            }
            next[i].player = players[i];
            next[i].entryId = ref.entryId;
            next[i].x = in[i].x;
            next[i].y = in[i].y;
            next[i].z = in[i].z;
            next[i].lr = in[i].lr < 0.0f ? -1.0f : 1.0f;
            next[i].damage = in[i].damage;
            next[i].shield = in[i].shield;
            next[i].haveShield = in[i].haveShield;
        }
        s_lite.side[0] = next[0];
        s_lite.side[1] = next[1];
        s_lite.have = true;
        return true;
    }
}
}
