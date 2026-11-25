#include "main/ui/info_display.h"

#include <cstdio>

#include <cm/cm_camera_controller.h>
#include <ft/fighter.h>
#include <gf/gf_pad_system.h>
#include <so/controller/so_controller_impl.h>
#include <so/controller/so_controller_module_impl.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/kinetic/so_kinetic_module_impl.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>

#include "shared/game/stick.h"

#include "extra/frame_data.h"
#include "main/cpu/view.h"
#include "main/extra.h"
#include "main/game/body.h"
#include "main/game/fighters.h"
#include "main/game/motion.h"
#include "main/game/status_names.h"
#include "main/game/tracker.h"
#include "main/gfx/draw.h"
#include "main/gfx/format_core.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "main/ui/menu_theme_core.h"
#include "main/ui/style.h"
#include "main/ui/wall_info_core.h"
#include "shared/game/mem.h"
#include "shared/log.h"

// Neutral dark-slate per-fighter info panels; reads game state only.
// Geometry lives in ui/hud_layout.h. Rows draw up to the last non-None slot;
// None rows in between remain blank.
namespace tm {
namespace ui {
    using main::settings;

    static const float kCap[main::kInfoSizeCount] = {6.2f, 7.6f, 8.8f};
    static const float kPitch[main::kInfoSizeCount] = {9.4f, 11.2f, 13.0f};
    // Port accent border colours (P1 red, P2 blue, P3 gold, P4 green).
    static const gfx::Rgba kOutline[4] = {0xEF4444FF, 0x3B82F6FF, 0xEAB308FF, 0x22C55EFF};

    // Per side: frames in the current status kind per simulated step
    // (re-entering the same kind does not restart it), and the last shield
    // hit's stun ("left/total").
    struct StatusClock {
        int entryId;
        int status;
        int frames;
        int stunLeft;
        int stunTotal;
    };
    static StatusClock s_clock[main::kSideCount] = {{-1, -1, 0, 0, 0}, {-1, -1, 0, 0, 0}};

    void infoStep(bool previousRan)
    {
        for (int side = 0; side < main::kSideCount; side++)
        {
            game::FighterRef ref;
            game::FighterState st;
            StatusClock& c = s_clock[side];
            if (!game::findFighter(side == main::kSideCpu, ref) || !game::readState(ref, st))
            {
                c.entryId = -1;
                continue;
            }
            if (ref.entryId != c.entryId || st.status != c.status)
            {
                c.entryId = ref.entryId;
                c.status = st.status;
                c.frames = 1;
            }
            else if (previousRan)
            {
                c.frames++;
            }
            if (st.shieldStun > c.stunLeft)
            {
                c.stunTotal = st.shieldStun;
            }
            else if (st.shieldStun == 0 && c.stunLeft == 0)
            {
                c.stunTotal = 0;
            }
            c.stunLeft = st.shieldStun;
        }
    }

    void infoReset()
    {
        for (int side = 0; side < main::kSideCount; side++)
        {
            s_clock[side].entryId = -1;
        }
    }

    int statusFrames(int side)
    {
        return side >= 0 && side < main::kSideCount ? s_clock[side].frames : 0;
    }

    static int rowCount(int side)
    {
        const main::InfoSettings& s = settings().info[side];
        int n = 0;
        for (int r = 0; r < main::kInfoRows; r++)
        {
            if (s.rows[r] != kInfoNone)
            {
                n = r + 1;
            }
        }
        return n;
    }

    bool infoWanted()
    {
        return rowCount(main::kSideHuman) > 0 || rowCount(main::kSideCpu) > 0;
    }

    void applyInfoPreset(int side)
    {
        static const u8 kPresets[kInfoPresetCount][main::kInfoRows] = {
            {kInfoNone},
            {kInfoStateName, kInfoStateFrame, kInfoActionable, kInfoActIn},
            {kInfoPosition, kInfoVelocityTotal, kInfoIntangibility, kInfoEcbLock, kInfoEcbBottom, kInfoJumps},
            {kInfoDamage, kInfoHitlag, kInfoHitstun, kInfoShieldHealth, kInfoShieldStun, kInfoVelocityKb},
            {kInfoStateName, kInfoSubaction, kInfoStateFrame, kInfoActIn, kInfoCancelFlags, kInfoAirborne}
        };
        main::InfoSettings& s = settings().info[side];
        int preset = s.preset < kInfoPresetCount ? s.preset : kInfoPresetNone;
        for (int r = 0; r < main::kInfoRows; r++)
        {
            s.rows[r] = kPresets[preset][r];
        }
    }

    // Short names for Brawl interrupt groups 1..0x12 (ProjectPunch's
    // AnimCmdInterruptGroup in include/pp/anim_cmd_utils.h).
    static const char* const kGroupNames[game::kGroupCount] = {
        "", "Spc", "Itm", "Grab", "Atk", "Roll", "Shld", "Jump", "Move",
        "Land", "Ledge", "ASpc", "AItm", "Tthr", "ADodge", "AAtk", "Trd", "WJmp", "DJmp"
    };

    static void groupText(u32 groups, bool cancel, char* buf, u32 size)
    {
        u32 used = snprintf(buf, size, "IASA:");
        for (int g = 1; g < game::kGroupCount && used + 1 < size; g++)
        {
            if (groups & (1u << g))
            {
                used += snprintf(buf + used, size - used, " %s", kGroupNames[g]);
            }
        }
        if (groups == 0 && used + 1 < size)
        {
            used += snprintf(buf + used, size - used, " -");
        }
        if (cancel && used + 1 < size)
        {
            snprintf(buf + used, size - used, "  Cancel: true");
        }
    }

    static int since(const game::Tracked* t, s32 frame)
    {
        return frame == game::kNoFrame ? 0 : t->cur.frame - frame + 1;
    }

    // Compact panel labels where shorter than the menu name.
    static const char* panelLabel(int value)
    {
        switch (value)
        {
        case kInfoVelocitySelf:
            return "Vel (Self)";
        case kInfoVelocityKb:
            return "Vel (KB)";
        case kInfoEngineLStick:
            return "Stick (Eng)";
        case kInfoSystemLStick:
            return "Stick (Pad)";
        case kInfoEngineCStick:
            return "C-Stick (Eng)";
        case kInfoSystemCStick:
            return "C-Stick (Pad)";
        case kInfoEngineTrigger:
            return "Trig (Eng)";
        case kInfoSystemTrigger:
            return "Trig (Pad)";
        case kInfoLineInfo:
            return "Coll Lines";
        case kInfoBlastzoneLeftRight:
            return "Blast L/R";
        case kInfoBlastzoneUpDown:
            return "Blast U/D";
        default:
            return infoValueLabel(value);
        }
    }

    // Values read from the game, not the trackers: docs/internals.md,
    // "Where the info display's values come from".
    enum {
        kLedgeInt = 2,
        kJumpsInt = 1,
        kWallJumpsInt = 3,
        kAirFrames = 0x10,
        kDropLock = 0x18,
        kJumpMax = 23003,
        kDeadRange = 0x158,
        kCatchPull = 0x35,
        kCatchAttack = 0x3A,
        kCapturePulled = 0x3D,
        kCaptureDamage = 0x3F
    };

    static bool captured(int s)
    {
        return s >= kCapturePulled && s <= kCaptureDamage;
    }

    // The grab timer of the grab this fighter is in, held or holding.
    static float grip(const game::FighterRef& ref, int status)
    {
        bool holding = status >= kCatchPull && status <= kCatchAttack;
        for (int i = 0; i < game::kMaxTracked; i++)
        {
            const game::Tracked* o = game::tracked(i);
            const soModuleEnumeration* m = o != NULL ? game::fighterModules(o->fighter) : NULL;
            // The status read live: the trackers finish a frame only when
            // the next one starts.
            if (m != NULL && (o->entryId == ref.entryId || holding) && captured(game::fighterStatus(m))
                && game::isRam(m->m_controllerModule))
            {
                return m->m_controllerModule->getClatterTime(0);
            }
        }
        return 0.0f;
    }

    // Shadows libc snprintf in this translation unit with typed overloads for
    // hot info-panel formats (%d, %s, %[+].[0-4]f) so MWCC's slow float
    // formatter is bypassed without varargs (__va_arg) while keeping full
    // vsnprintf fallback for everything else.
    static int snprintf(char* buf, u32 size, const char* format)
    {
        int n = gfx::formatFast(buf, size, format, NULL, 0);
        return n >= 0 ? n : std::snprintf(buf, size, format);
    }

    static int snprintf(char* buf, u32 size, const char* format, int a)
    {
        gfx::FmtArg args[1] = {gfx::FmtArg::fromInt(a)};
        int n = gfx::formatFast(buf, size, format, args, 1);
        return n >= 0 ? n : std::snprintf(buf, size, format, a);
    }

    static int snprintf(char* buf, u32 size, const char* format, int a, int b)
    {
        gfx::FmtArg args[2] = {gfx::FmtArg::fromInt(a), gfx::FmtArg::fromInt(b)};
        int n = gfx::formatFast(buf, size, format, args, 2);
        return n >= 0 ? n : std::snprintf(buf, size, format, a, b);
    }

    static int snprintf(char* buf, u32 size, const char* format, const char* s)
    {
        gfx::FmtArg args[1] = {gfx::FmtArg::fromStr(s)};
        int n = gfx::formatFast(buf, size, format, args, 1);
        return n >= 0 ? n : std::snprintf(buf, size, format, s);
    }

    static int snprintf(char* buf, u32 size, const char* format, float a)
    {
        gfx::FmtArg args[1] = {gfx::FmtArg::fromFloat(a)};
        int n = gfx::formatFast(buf, size, format, args, 1);
        return n >= 0 ? n : std::snprintf(buf, size, format, a);
    }

    static int snprintf(char* buf, u32 size, const char* format, float a, float b)
    {
        gfx::FmtArg args[2] = {gfx::FmtArg::fromFloat(a), gfx::FmtArg::fromFloat(b)};
        int n = gfx::formatFast(buf, size, format, args, 2);
        return n >= 0 ? n : std::snprintf(buf, size, format, a, b);
    }

    static int snprintf(char* buf, u32 size, const char* format, const char* s, float a, float b)
    {
        gfx::FmtArg args[3] = {gfx::FmtArg::fromStr(s), gfx::FmtArg::fromFloat(a), gfx::FmtArg::fromFloat(b)};
        int n = gfx::formatFast(buf, size, format, args, 3);
        return n >= 0 ? n : std::snprintf(buf, size, format, s, a, b);
    }

    static int snprintf(char* buf, u32 size, const char* format, const char* s, int a, int b)
    {
        return std::snprintf(buf, size, format, s, a, b);
    }

    static int snprintf(char* buf, u32 size, const char* format, int a, int b, int c)
    {
        return std::snprintf(buf, size, format, a, b, c);
    }

    static int snprintf(char* buf, u32 size, const char* format, int a, const char* s, int b)
    {
        return std::snprintf(buf, size, format, a, s, b);
    }

    static int snprintf(char* buf, u32 size, const char* format, int a, int b, int c, int d)
    {
        return std::snprintf(buf, size, format, a, b, c, d);
    }

    // Formats direct game-module values; returns false if unhandled or unreadable.
    static bool gameText(int value, const game::FighterState& st, const game::FighterRef& ref, int side, char* buf,
                         u32 size)
    {
        const soModuleEnumeration* m = game::fighterModules(ref.fighter);
        if (m == NULL)
        {
            return false;
        }
        const gfPadSystem* pads = g_gfPadSystem;
        const gfPadStatus* pad = game::isRam(pads) && static_cast<u32>(ref.playerNo) < 4
                                     ? &pads->m_gameGcnPads[ref.playerNo]
                                     : NULL;
        soModuleAccesser* acc = ref.fighter->m_moduleAccesser;
        const char* format = "%s: %.0f";
        float x = 0.0f;
        float y = 0.0f;
        switch (value)
        {
        case kInfoVelocitySelf:
        case kInfoVelocityKb:
        {
            soKineticModule* k = m->m_kineticModule;
            if (!game::isRam(k))
            {
                return false;
            }
            Vec2f v = k->getSumSpeed(soKineticEnergy::AttributeFlag(value - kInfoVelocitySelf + 1));
            format = "%s: (%+.3f , %+.3f)";
            x = v.m_x;
            y = v.m_y;
            break;
        }
        case kInfoEngineLStick:
        {
            game::Body b;
            if (!game::readBody(ref.fighter, b))
            {
                return false;
            }
            format = "%s: (%+.4f , %+.4f)";
            x = b.stickX;
            y = b.stickY;
            break;
        }
        case kInfoSystemLStick:
        case kInfoSystemCStick:
        case kInfoSystemTrigger:
            if (pad == NULL)
            {
                return false;
            }
            format = "%s: (%+.4f , %+.4f)";
            if (value == kInfoSystemLStick)
            {
                // Raw pad scaled by P+'s directional-input rule (shared/game/stick.h).
                game::mainStick(static_cast<s8>(pad->m_stickX), static_cast<s8>(pad->m_stickY), &x, &y);
            }
            else
            {
                game::cStick(static_cast<s8>(pad->m_subStickX), static_cast<s8>(pad->m_subStickY), &x, &y);
            }
            if (value == kInfoSystemTrigger)
            {
                format = "%s: (%+.3f , %+.3f)";
                x = static_cast<u8>(pad->m_lTriggerAnalog) / 255.0f;
                y = static_cast<u8>(pad->m_rTriggerAnalog) / 255.0f;
            }
            break;
        case kInfoEngineTrigger:
        {
            soController* c = game::isRam(m->m_controllerModule) ? m->m_controllerModule->getController() : NULL;
            if (!game::isRam(c))
            {
                return false;
            }
            format = "%s: (%+.3f)";
            x = c->getLr();
            break;
        }
        case kInfoLedgegrabTimer:
            x = game::instanceInt(m, kLedgeInt);
            break;
        case kInfoIntangibility:
        {
            game::Body b;
            if (!game::readBody(ref.fighter, b))
            {
                return false;
            }
            x = b.xluFrames > b.invFrames ? b.xluFrames : b.invFrames;
            if (side == main::kSideCpu && settings().cpu.options.intangible && x > 0.0f)
            {
                format = "%s: lab";
            }
            else if (x <= 0.0f && b.intangible)
            {
                format = "%s: move";
            }
            break;
        }
        case kInfoShieldHealth:
            cpu::shieldHealth(ref.fighter, x);
            format = "%s: %.3f";
            break;
        case kInfoGripStrength:
            x = grip(ref, st.status);
            break;
        case kInfoEcbLock:
        {
            const void* info = game::extraInfo(m);
            if (info != NULL)
            {
                s32 air = static_cast<s32>(game::wordAt(info, kAirFrames));
                s32 drop = static_cast<s32>(game::wordAt(info, kDropLock));
                s32 left = air >= 1 && air <= 9 ? 10 - air : 0;
                x = drop > left ? drop : left;
            }
            break;
        }
        case kInfoEcbBottom:
        {
            // ECB bottom offset above the fighter's root position.
            soGroundModule* g = m->m_groundModule;
            if (!game::isRam(g))
            {
                return false;
            }
            format = "%s: %.3f";
            x = g->getDownPos(0).m_y - st.y;
            break;
        }
        case kInfoJumps:
            // The accesser is in RAM: fighterModules checked it.
            format = "%s: %.0f/%.0f";
            x = game::instanceInt(m, kJumpsInt);
            y = soValueAccesser::getConstantInt(acc, kJumpMax, 0);
            break;
        case kInfoWalljumps:
            // Brawl's wall jump count (LA int 3), read by the wall jump at
            // 0x8086e4a4 with parameter 3224; landing clears it (Mario, FD).
            x = game::instanceInt(m, kWallJumpsInt);
            break;
        case kInfoCanWalljump:
        {
            // P+ ftInfo +0x24 via getConstantInt(23189); LA-Basic[4] counts
            // down from IC-Basic[23069] (20f) after a walljump.
            bool air = game::isRam(m->m_situationModule) && m->m_situationModule->getKind() == Situation_Air;
            int can = soValueAccesser::getConstantInt(acc, kParamCanWalljump, 0);
            int count = game::instanceInt(m, kWallJumpCountLa);
            int cd = game::instanceInt(m, kWallJumpCooldownLa);
            formatCanWalljump(can, air, count, cd, buf, size);
            return true;
        }
        case kInfoWallCling:
        {
            // P+ ftInfo +0x28 via getConstantInt(23190); Knuckles (0x2D) uses
            // custom WallClimb* actions; LA-Basic[11..12] track 1 cling/airtime.
            bool air = game::isRam(m->m_situationModule) && m->m_situationModule->getKind() == Situation_Air;
            int can = soValueAccesser::getConstantInt(acc, kParamCanWallCling, 0);
            int kind = game::fighterKind(ref.entryId);
            int count = game::instanceInt(m, kWallClingCountLa);
            int noAttach = game::instanceInt(m, kWallClingNoAttachLa);
            formatWallCling(can, kind, air, count, noAttach, buf, size);
            return true;
        }
        case kInfoBlastzoneLeftRight:
        case kInfoBlastzoneUpDown:
        {
            const CameraController* cam = CameraController::getInstance();
            if (!game::isRam(cam))
            {
                return false;
            }
            const float* d = reinterpret_cast<const float*>(reinterpret_cast<u32>(cam) + kDeadRange);
            format = "%s: (%+.3f,%+.3f)";
            x = d[0];
            y = d[1];
            if (value == kInfoBlastzoneUpDown)
            {
                // Top, then bottom (observed on Final Destination: 188, -140).
                format = "%s: (%.2f,%.2f)";
                x = d[2];
                y = d[3];
            }
            break;
        }
        default:
            return false;
        }
        snprintf(buf, size, format, panelLabel(value), x, y);
        return true;
    }

    // Stage collision line ids currently touched by the ECB (ground, ceiling,
    // left wall, right wall; -1 when not touching).
    static bool touchedLines(Fighter* fighter, int* out)
    {
        const soModuleEnumeration* m = game::fighterModules(fighter);
        soGroundModule* g = m != NULL ? m->m_groundModule : NULL;
        if (!game::isRam(g))
        {
            return false;
        }
        out[0] = g->isTouch(grCollStatus::TOUCH_MASK_DOWN, 0) ? static_cast<int>(g->getDownLineId(0)) : -1;
        out[1] = g->isTouch(grCollStatus::TOUCH_MASK_UP, 0) ? static_cast<int>(g->getUpLineId(0)) : -1;
        out[2] = g->isTouch(grCollStatus::TOUCH_MASK_LEFT, 0) ? static_cast<int>(g->getLeftLineId(0)) : -1;
        out[3] = g->isTouch(grCollStatus::TOUCH_MASK_RIGHT, 0) ? static_cast<int>(g->getRightLineId(0)) : -1;
        return true;
    }

    // Formats one info row; returns false for unbuilt values (drawn muted).
    static bool rowText(int value, const game::FighterState& st, const game::FighterRef& ref,
                        const game::Tracked* t, int side, char* buf, u32 size)
    {
        buf[0] = '\0';
        switch (value)
        {
        case kInfoPosition:
            snprintf(buf, size, "Pos: (%+.2f, %+.2f)", st.x, st.y);
            return true;
        case kInfoVelocityTotal:
            // Position change over the last frame (self + knockback).
            snprintf(buf, size, "Vel (Tot): (%+.3f, %+.3f)", st.vx, st.vy);
            return true;
        case kInfoDamage:
            snprintf(buf, size, "Damage: %.1f%%", st.damage);
            return true;
        case kInfoHitstun:
            snprintf(buf, size, "Hitstun: %d", st.hitstun);
            return true;
        case kInfoShieldStun:
            snprintf(buf, size, "Shield Stun: %d/%d", st.shieldStun, s_clock[side].stunTotal);
            return true;
        case kInfoLineInfo:
        {
            int l[4];
            if (touchedLines(ref.fighter, l))
            {
                snprintf(buf, size, "Coll: G:%d C:%d L:%d R:%d", l[0], l[1], l[2], l[3]);
            }
            return true;
        }
        case kInfoActIn:
        {
            // Read ahead from the move's scripts by the frame data service; blank
            // when the scripts do not settle it.
            int how = main::kActNone;
            int n = extra::framesUntilActionable(ref.fighter, &how);
            snprintf(buf, size, n >= 0 ? "Actionable In: %d" : "Actionable In: -", n);
            return true;
        }
        case kInfoStateName:
        {
            const char* name = game::statusName(st.status);
            if (name != NULL)
            {
                snprintf(buf, size, "State: %s", name);
            }
            else
            {
                snprintf(buf, size, "State: 0x%X", st.status);
            }
            return true;
        }
        case kInfoSubaction:
        {
            game::MotionInfo mi;
            if (!game::readMotion(ref.fighter, mi))
            {
                snprintf(buf, size, "Subaction: ?");
            }
            else if (mi.name != NULL)
            {
                snprintf(buf, size, "Subaction: %.20s %d/%d", mi.name, mi.frame, mi.length);
            }
            else
            {
                snprintf(buf, size, "Subaction: 0x%X %d/%d", mi.kind, mi.frame, mi.length);
            }
            return true;
        }
        default:
            break;
        }
        if (gameText(value, st, ref, side, buf, size))
        {
            return true;
        }
        if (!infoValueAvailable(value))
        {
            snprintf(buf, size, "%s: -", panelLabel(value));
            return false;
        }
        if (t == NULL || t->cur.frame == game::kNoFrame)
        {
            return true;
        }
        const game::FighterFrame& f = t->cur;
        switch (value)
        {
        case kInfoStateFrame:
        {
            // Animation frame over animation length (falls back to status clock).
            game::MotionInfo mi;
            if (game::readMotion(ref.fighter, mi) && mi.length > 0)
            {
                snprintf(buf, size, "Frame: %d/%d", mi.frame, mi.length);
            }
            else
            {
                snprintf(buf, size, "Frame: %d", since(t, t->statusStart));
            }
            break;
        }
        case kInfoActionable:
            if (f.actionable)
            {
                snprintf(buf, size, "Actionable: true, %d f", since(t, t->actionableSince));
            }
            else
            {
                snprintf(buf, size, "Actionable: false, %d f", since(t, t->busySince));
            }
            break;
        case kInfoHitlag:
            snprintf(buf, size, "Hitlag: %d", f.hitlag);
            break;
        case kInfoCancelFlags:
            groupText(f.groups, f.cancel != 0, buf, size);
            break;
        case kInfoAirborne:
            snprintf(buf, size, "Airborne: %s", f.airborne ? "true" : "false");
            break;
        case kInfoLastHit:
        {
            const game::Tracked* by = game::tracked(t->lastReceived.other);
            if (t->lastReceived.frame == game::kNoFrame)
            {
                snprintf(buf, size, "Last Hit: none");
            }
            else
            {
                snprintf(buf, size, "Last Hit: P%d%s, %d f ago", by != NULL ? by->playerNo + 1 : 0,
                         t->lastReceived.shield ? " on shield" : "", f.frame - t->lastReceived.frame);
            }
            break;
        }
        default:
            break;
        }
        return true;
    }

    // One side's panel from `top`; returns its bottom (`top` if nothing drawn).
    static float drawSide(int side, float top)
    {
        int rows = rowCount(side);
        game::FighterRef ref;
        game::FighterState st;
        if (rows == 0 || !game::findFighter(side == main::kSideCpu, ref) || !game::readState(ref, st))
        {
            return top;
        }
        const main::InfoSettings& s = settings().info[side];
        int size = s.size < main::kInfoSizeCount ? s.size : main::kInfoMedium;
        bool wide = gfx::widescreen();
        float k = layout::infoScale(size);
        float width = layout::infoWidth(size, wide);
        float height = layout::infoHeight(rows, size);
        float left = layout::infoLeft(wide);
        const main::Settings& all = settings();
        InfoThemeColors theme = resolveInfoTheme(all.size ? all.osd.infoBgColor : static_cast<u8>(main::kInfoBgDefault),
                                                 all.size ? all.osd.infoBgAlpha : static_cast<u8>(main::kInfoAlphaDefault));
        if (theme.fillColor != 0)
        {
            gfx::fillRect(left, top, width, height, theme.fillColor);
        }
        gfx::Rgba edge = kOutline[ref.playerNo >= 0 && ref.playerNo < 4 ? ref.playerNo : 0];
        gfx::frameRect(left, top, width, height, edge);
        gfx::frameRect(left + 1.0f, top + 1.0f, width - 2.0f, height - 2.0f, edge);

        const game::Tracked* t = game::trackedEntry(ref.entryId);
        float scale = style::scaleForCap(kCap[size]);
        float inset = style::w(8.0f * k);
        float maxW = width - 2.0f * inset;
        float capTop = top + 8.0f * k;
        char buf[64];
        for (int r = 0; r < rows; r++)
        {
            int value = s.rows[r];
            if (value != kInfoNone)
            {
                bool built = rowText(value, st, ref, t, side, buf, sizeof(buf));
                if (game::g_trackerTrace)
                {
                    TM_LOG("info P%d: %s\n", ref.playerNo + 1, buf);
                }
                float textW = style::boldWidth(buf, scale);
                float sx = textW > maxW && textW > 0.0f ? scale * maxW / textW : scale;
                float tx = left + inset;
                float ty = style::textYForCap(capTop + r * kPitch[size], scale);
                if (theme.shadowColor != 0)
                {
                    style::boldText(tx + gfx::xScale(), ty + 1.0f, buf, theme.shadowColor, sx, scale);
                }
                style::boldText(tx, ty, buf, built ? style::kText : style::kTextDisabled, sx, scale);
            }
        }
        return top + height;
    }

    void drawInfo()
    {
        if (gfx::lineHeight(1.0f) <= 0.0f)
        {
            return;
        }
        // Under the top band where the wider of the two panels would meet it
        // (hud_layout.h).
        bool wide = gfx::widescreen();
        float width = 0.0f;
        for (int side = 0; side < main::kSideCount; side++)
        {
            const main::InfoSettings& s = settings().info[side];
            float w = rowCount(side) > 0 ? layout::infoWidth(s.size, wide) : 0.0f;
            width = w > width ? w : width;
        }
        float top = layout::infoTop(layout::topBand(), width, wide);
        float bottom = drawSide(main::kSideHuman, top);
        drawSide(main::kSideCpu, layout::infoCpuTop(top, bottom));
    }
}
}
