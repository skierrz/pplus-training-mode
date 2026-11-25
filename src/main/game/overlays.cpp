#include "main/game/overlays.h"

#include <ft/fighter.h>
#include <ft/ft_entry_manager.h>
#include <ft/ft_manager.h>
#include <GX/GXTypes.h>
#include <so/color/so_color_blend_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/visibility/so_visibility_module_impl.h>

#include "main/game/fighters.h"
#include "main/game/status_ids.h"
#include "main/game/tech_frames.h"
#include "main/game/techniques.h"
#include "main/game/tracker.h"
#include "shared/game/mem.h"
#include "shared/game/menu_sound.h"
#include "shared/log.h"

// How tinting, hiding and the tech invisibility work: docs/internals.md,
// "Colour overlays and hiding". Before the first call on a fighter its
// module must read back as the header says (+0x14F, +0xE), else nothing is
// done. The colour call follows ProjectPunch's color_overlay.cpp, the tech
// subactions and window its invis_techs.cpp (MIT; THIRD_PARTY_NOTICES.md).
namespace tm {
namespace game {
    enum {
        kSubColorEnabled = 0x14F,
        kVisibilityWhole = 0xE,
        // Fighter+0x138: bit 2 = cursor (setCursor), bit 1 = nameCursor
        // (setNameCursor); verified in 0x8083CBF0-0x8083CD2C.
        kFighterCursorFlags = 0x138,
        kCursorBit = 0x04,
        kNameCursorBit = 0x02,
        kStatusCliffWaitId = 0x75
    };

    struct Applied {
        Fighter* fighter;
        int entryId;
        u8 choice;
        s8 group; // the group that applied on the last live step, -1 none
        bool colored;
        bool hidden;
        u8 savedWhole;
        bool cursorSaved;
        u8 savedCursor;
        u8 savedNameCursor;
    };

    static Applied s_applied[kMaxTracked];
    static bool s_colorChecked = false;
    static bool s_colorOk = false;
    static bool s_visChecked = false;
    static bool s_visOk = false;

    // High-contrast P+ state-overlay tint palette (RGBA).
    static const GXColor kColors[kOverlayChoiceCount] = {
        {0, 0, 0, 0},
        {230, 55, 70, 175},
        {45, 210, 115, 175},
        {65, 130, 245, 175},
        {245, 205, 50, 175},
        {235, 240, 248, 175},
        {32, 36, 44, 185},
        {45, 215, 225, 175},
        {245, 130, 45, 175},
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0},
        {0, 0, 0, 0}};

    static soColorBlendModule* colorModule(Fighter* f)
    {
        const soModuleEnumeration* m = fighterModules(f);
        if (m == NULL || !isRam(m->m_colorBlendModule))
        {
            return NULL;
        }
        soColorBlendModule* c = m->m_colorBlendModule;
        if (!s_colorChecked)
        {
            s_colorChecked = true;
            s_colorOk = (c->isEnableSubColor() != 0) == (byteAt(c, kSubColorEnabled) != 0);
            TM_LOG("overlays: colour blend module %08x %s\n", c, s_colorOk ? "ok" : "not as expected: overlays off");
        }
        return s_colorOk ? c : NULL;
    }

    static soVisibilityModule* visibilityModule(Fighter* f)
    {
        const soModuleEnumeration* m = fighterModules(f);
        if (m == NULL || !isRam(m->m_visibilityModule))
        {
            return NULL;
        }
        soVisibilityModule* v = m->m_visibilityModule;
        if (!s_visChecked)
        {
            s_visChecked = true;
            s_visOk = v->getWhole() == byteAt(v, kVisibilityWhole);
            TM_LOG("overlays: visibility module %08x %s\n", v, s_visOk ? "ok" : "not as expected: hiding off");
        }
        return s_visOk ? v : NULL;
    }

    // Statuses where the game itself keeps m_whole at 0 (Dead, Standby:
    // knocked out or swapped out by a transformation; Rebirth shows the
    // fighter on the revival platform).
    static bool gameHides(Fighter* f)
    {
        const soModuleEnumeration* m = fighterModules(f);
        return m != NULL && outOfPlay(fighterStatus(m), false);
    }

    static void restoreVisibility(Applied& a)
    {
        if (!a.hidden)
        {
            return;
        }
        if (!gameHides(a.fighter))
        {
            soVisibilityModule* v = visibilityModule(a.fighter);
            if (v != NULL)
            {
                v->setWhole(a.savedWhole);
            }
            if (a.cursorSaved)
            {
                if (a.savedCursor != 0)
                {
                    a.fighter->setCursor(true);
                }
                if (a.savedNameCursor != 0)
                {
                    a.fighter->setNameCursor(true);
                }
            }
        }
        a.hidden = false;
        a.cursorSaved = false;
    }

    static void putBack(Applied& a)
    {
        if (a.fighter == NULL)
        {
            return;
        }
        if (a.colored)
        {
            soColorBlendModule* c = colorModule(a.fighter);
            if (c != NULL)
            {
                c->setSubColor(kColors[kOverlayNone], 0);
            }
        }
        restoreVisibility(a);
        a.colored = false;
        a.choice = kOverlayNone;
    }

    // True if `f` is still one of the entry's fighter instances (a transform
    // keeps the instance it left in the entry).
    static bool stillThere(int entryId, Fighter* f)
    {
        ftEntryManager* entries = g_ftEntryManager;
        if (!isRam(entries) || !isRam(f))
        {
            return false;
        }
        ftEntry* entry = entries->getEntity(entryId);
        if (!isRam(entry) || entry->m_entryId != entryId)
        {
            return false;
        }
        for (int k = 0; k < 4; k++)
        {
            if (entry->m_instances[k].m_fighter == f)
            {
                return true;
            }
        }
        ftManager* manager = g_ftManager;
        return isRam(manager) && manager->getSubFighter(entryId) == f;
    }

    // Releases a slot's record whose fighter is not the tracked one any more.
    static void release(Applied& a)
    {
        if (a.fighter != NULL && stillThere(a.entryId, a.fighter))
        {
            putBack(a);
        }
        a.fighter = NULL;
        a.colored = false;
        a.hidden = false;
        a.cursorSaved = false;
        a.choice = kOverlayNone;
        a.group = -1;
    }

    // Ground tech subactions (Passive, Passive_Stand_F, Passive_Stand_B) use
    // P+'s per-character distinguishable frame (tech_frames.cpp). Get-ups and
    // slip get-ups (Down_*_U/D 0xAF..0xB2, 0xB8..0xBB; Slip_* 0xE6..0xE9) use
    // ProjectPunch's 0-indexed frame 5 + delay < subactionFrame < 20
    // (invis_techs.cpp:11-52).
    static bool passiveMotion(int kind)
    {
        return kind >= 0xBE && kind <= 0xC0;
    }

    static bool getupMotion(int kind)
    {
        return (kind >= 0xAF && kind <= 0xB2) || (kind >= 0xB8 && kind <= 0xBB) || (kind >= 0xE6 && kind <= 0xE9);
    }

    bool techHidden(int i, const OverlayConfig& config)
    {
        const Tracked* t = tracked(i);
        const TechniqueState* st = techniqueState(i);
        if (t == NULL || st == NULL || config.techInvis == 0 || (config.techInvis == 1 && !t->cpu))
        {
            return false;
        }
        int kind = st->body.motionKind;
        // st->body.motionFrame is the 0-indexed frame the previous step ended
        // on; the coming step shows subactionFrame = motionFrame + 1 (0-indexed,
        // ProjectPunch invis_techs.cpp:50) and 1-indexed stateFrame =
        // subactionFrame + 1.
        int subactionFrame = st->body.motionFrame + 1;
        if (passiveMotion(kind))
        {
            int dist = techDistinguishable(fighterKind(t->entryId, t->partner));
            return techInvisFrame(subactionFrame + 1, dist, config.techDelay);
        }
        if (getupMotion(kind))
        {
            return getupInvisFrame(subactionFrame, config.techDelay);
        }
        return false;
    }

    static bool groupActive(int g, const Tracked& t, const TechniqueState& st)
    {
        int s = t.cur.status;
        switch (g)
        {
        case kGroupActionable:
            return t.cur.actionable != 0;
        case kGroupHitstun:
            return t.cur.hitstun > 0;
        case kGroupInvincible:
            return st.body.intangible != 0;
        case kGroupLedgeActionable:
            return s == kStatusCliffWaitId;
        case kGroupMissedLCancel:
            return st.missedLCancel;
        case kGroupCanFastfall:
            return st.canFastfall;
        case kGroupAutocancel:
            return s == kStatusAttackAirFirst && !st.body.landingLag;
        case kGroupCrouch:
            return s >= kStatusSquat && s <= kStatusSquatRv;
        case kGroupWait:
            return s == kStatusWait;
        case kGroupWalk:
            return s == kStatusWalk || s == kStatusWalkBrake;
        case kGroupDash:
            return s == kStatusDash;
        case kGroupRun:
            return s == kStatusRun || s == kStatusRunBrake;
        case kGroupFullHop:
            return s == kStatusJump && !st.body.shortHop;
        case kGroupShortHop:
            return s == kStatusJump && st.body.shortHop;
        case kGroupIasa:
            return t.cur.actionable && ((s >= kStatusAttack && s <= kStatusAttackAirFirst)
                                         || (s >= kStatusSpecialN && s < kStatusFinal));
        case kGroupShieldStun:
            return s == kStatusGuardDamage && t.cur.shieldStun > 0;
        default:
            return false;
        }
    }

    static u32 activeGroups(const Tracked& t, const TechniqueState& st)
    {
        u32 active = 0;
        for (int g = 0; g < kOverlayGroupCount; g++)
        {
            active |= groupActive(g, t, st) ? 1u << g : 0u;
        }
        return active;
    }

    // Only own the whole-model byte when the game currently has the fighter
    // visible: if the game already holds it at 0 (Dead, Standby, vanish,
    // cape, transformation), leave ownership false so putBack never writes a
    // stale 0 after the game later shows the fighter, nor writes 1 onto a
    // fighter that went out of play while we were hiding it. Setting whole to
    // 0 makes isVisible() false, so soShadowModuleImpl::update already skips
    // submitting the ground shadow without touching activeStatus. Cursor and
    // name cursor (Fighter+0x138 bits 2 and 1) are also kept off by
    // overlaysFixCamera after Fighter::processFixCamera re-evaluates them.
    static void hide(Applied& a, Fighter* f, soVisibilityModule* v, int status)
    {
        if (outOfPlay(status, false))
        {
            a.hidden = false;
            a.cursorSaved = false;
            return;
        }
        if (!a.hidden)
        {
            u8 whole = v->getWhole();
            if (whole == 0)
            {
                return;
            }
            a.savedWhole = whole;
            a.hidden = true;
        }
        v->setWhole(0);
        if (!a.cursorSaved)
        {
            u8 cur = byteAt(f, kFighterCursorFlags);
            a.savedCursor = (cur & kCursorBit) != 0 ? 1 : 0;
            a.savedNameCursor = (cur & kNameCursorBit) != 0 ? 1 : 0;
            a.cursorSaved = true;
        }
        f->setCursor(false);
        f->setNameCursor(false);
    }

    void overlaysApply(bool live, const OverlayConfig& config)
    {
        for (int i = 0; i < kMaxTracked; i++)
        {
            Applied& a = s_applied[i];
            const Tracked* t = tracked(i);
            const TechniqueState* st = techniqueState(i);
            if (a.fighter != NULL && (t == NULL || t->fighter != a.fighter))
            {
                release(a);
            }
            if (!live || t == NULL || st == NULL)
            {
                putBack(a);
                continue;
            }
            if (a.fighter != t->fighter)
            {
                a.group = -1;
            }
            a.fighter = t->fighter;
            a.entryId = t->entryId;
            const u8* groups = config.groups[t->cpu ? 1 : 0];
            int group = overlayGroup(groups, activeGroups(*t, *st));
            int choice = group >= 0 ? groups[group] : kOverlayNone;
            // Play Sound cues once on entering a Play Sound group using the
            // menu cursor sound (game::kSoundMove). `a.group` persists across
            // pauses so resuming inside the same state stays silent.
            if (overlaySoundDue(a.group, group, groups) && !config.quiet)
            {
                TM_LOG("overlays: P%d play sound, group %d (handle %d)\n", t->playerNo + 1, group,
                       playMenuSound(kSoundMove));
            }
            a.group = static_cast<s8>(group);
            bool hidden = choice == kOverlayInvisible || techHidden(i, config)
                          || (t->playerNo >= 0 && (config.hideModels & (1 << t->playerNo)) != 0);
            soColorBlendModule* c = colorModule(t->fighter);
            if (c != NULL)
            {
                if (choice >= kOverlayRed && choice <= kOverlayOrange)
                {
                    c->setSubColor(kColors[choice], 1);
                    a.colored = true;
                }
                // Show Collision and Remove clear any active sub-colour; a
                // Play Sound group clears only a colour we set.
                else if (choice == kOverlayRemove || choice == kOverlayCollision || a.colored)
                {
                    c->setSubColor(kColors[kOverlayNone], 0);
                    a.colored = false;
                }
            }
            soVisibilityModule* v = visibilityModule(t->fighter);
            if (v != NULL)
            {
                if (hidden)
                {
                    hide(a, t->fighter, v, t->cur.status);
                }
                else
                {
                    restoreVisibility(a);
                }
            }
            a.choice = static_cast<u8>(choice);
        }
    }

    void overlaysFixCamera(void* fighter)
    {
        if (fighter == NULL)
        {
            return;
        }
        for (int i = 0; i < kMaxTracked; i++)
        {
            if (s_applied[i].fighter == fighter && s_applied[i].hidden)
            {
                Fighter* f = static_cast<Fighter*>(fighter);
                f->setCursor(false);
                f->setNameCursor(false);
                return;
            }
        }
    }

    void overlaysClear()
    {
        for (int i = 0; i < kMaxTracked; i++)
        {
            release(s_applied[i]);
        }
        s_colorChecked = s_visChecked = false;
    }

    bool overlayShowsCollision(const Fighter* f)
    {
        for (int i = 0; i < kMaxTracked; i++)
        {
            if (f != NULL && s_applied[i].fighter == f && s_applied[i].choice == kOverlayCollision)
            {
                return true;
            }
        }
        return false;
    }
}
}
