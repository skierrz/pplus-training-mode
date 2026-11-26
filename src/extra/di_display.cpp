// General > DI Display (di_display.h): predicts and draws a hit fighter's
// path from its knockback and gravity energies, P+'s DI rules and the
// stage's rays. Game state: P+'s own Draw DI Code Menu value is set to 0
// while the display is shown, put back after. Measured on RSBE01 rev 2 with
// P+ 3.2.1 (scripts/di_tests.py --measure, --measure-ray, --measure-bounce:
// Mario's smashes and up tilt on Fox, Final Destination and Battlefield);
// "read": the game's code in a memory dump.
// Long form: docs/internals-extra.md, "DI Display".
#include "extra/di_display.h"
#include "extra/di_cstick_core.h"
#include "extra/di_picker_core.h"
#include "extra/hud_draw.h"
#include "extra/net_alloc.h"

#include <GX/GXTypes.h>
#include <ft/ft_entry.h>
#include <ft/ft_entry_manager.h>
#include <ip/ip_padconfig.h>
#include <so/ground/so_ground_module_impl.h>
#include <so/situation/so_situation_module_impl.h>
#include <so/so_module_accesser.h>
#include <so/so_value_accesser.h>
#include <so/stop/so_stop_module_impl.h>
#include <st/st_utility.h>
#include <cstdio>
#include <string.h>

#include "main/cpu/driver.h"
#include "main/cpu/fighter_input.h"
#include "main/cpu/options.h"
#include "main/display/display.h"
#include "main/display/input_display.h"
#include "main/game/fighters.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/settings.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"
#include "shared/game/mem.h"
#include "shared/game/stick.h"
#include "shared/log.h"

#ifdef TM_EXPERIMENTS
#include <revolution/OS/OSTime.h>

#include "main/experiments/ray_probe.h"
#endif

// Flight (measured):
//  - Kinetic module (soModuleEnumeration +0x7C): energies from +0x4C, 12
//    bytes each (pointer, id); id 1 gravity, id 4 knockback (kEnergy*; fall
//    acceleration -0.23, largest fall 2.8 for Fox). In hitlag the knockback
//    holds the launch speed before DI and gravity's speed is 0.
//  - Trajectory DI, first frame after hitlag: the launch turns by 18 degrees
//    times c squared (sign kept), c the stick's part across it, positive
//    counter-clockwise, after P+ scales the stick onto a circle of 1.0025
//    (P+ 3.2.1 Source/ProjectM/DirectionalInfluence.asm:25-88).
//  - ASDI on that frame: ASDI distance x the hit's SDI multiplier (damage
//    log +0x30) x the last hitlag frame's stick, if past the threshold. SDI:
//    SDI distance x the stick on a hitlag frame it enters a new one of 8
//    directions. Neither moves down through the floor.
//  - Threshold 0.7: parameter 3173 at 0x80B88350. SDI 6, ASDI 3, decay 0.051:
//    Code Menu values P+ writes every frame (P+ 3.2.1
//    Source/CodeMenu/CodeMenu.asm:2218-2251, hook 0x80023D60, which also
//    writes 3285; addresses in kEnvAt).
//  - P+'s Random DI drives a fighter whose AI flag is set
//    (cpu/fighter_input.h): no-DI path only.
// Stage (read, then measured):
//  - stRayCheck(pos, dir, hit, normal, b1, collision, b2, mask) (0x809326D8,
//    sora_melee +0x227CC4) tests the segment pos to pos + dir: collision list
//    0x8049E57C walked by 0x80138348, each line by 0x801380BC; P+ skips lines
//    facing no side (patch 0x80138154); pass-through lines (+0x10 bit 0x01)
//    only with b1; target kinds (+0xE) must meet the mask (1 finds every line
//    here, 2 and 4 none); b2 false: front faces only (0x80138240). Returns
//    the normal facing the start. About 0.6 us a call, 3 to 4.5 on a hit.
//  - A landing shows at the crossing frame's x, feet on the floor. The ECB's
//    bottom is at the feet standing and ~5 frames after launch, then 3.5 to
//    6.7 up (Fox, tumble): taken as 0.4 of its height, so a landing can show
//    a frame early or late.
//  - Rays cover 4 frames, then each frame of a span that meets the stage;
//    the arc leaves that chord by ~0.5, so a grazed ceiling may be missed.
//    One fighter's paths take 0.1 to 0.3 ms over or near the stage.
// After contact, no tech (dashed):
//  - DamageFly or DamageFall landing becomes Down (0x4A; Fighter.pac
//    transition 10012).
//    Downward ASDI on the ground stays grounded (Landing 0x16 or Down) unless
//    the launch rises more (inferred from measurements, not read in code).
//    The slide loses Ground_Brake (3004) x Damage_Ground_Mul (3293, 1.0) a
//    frame (ftKineticEnergyDamage::updateEnergy 0x80869938; Fox 0.08,
//    measured); under two frames of brake, none (Down's own 0.3 a frame for
//    3 to 6 frames is not knockback, not drawn).
//  - Bounce above Damage_Fly_Reflect_Speed (3284, 1.0): DamageFlyReflect
//    (0x47 wall, 0x48 ceiling; transitions 10006-10008), gravity 0,
//    knockback mirrored on getTouchNormal x 3285 (0.8), no pause (0x8086DCD4
//    wall, 0x8086DF20 ceiling). Followed only off upright walls and level
//    ceilings (elsewhere the ECB is pushed up to 7 along the line); the
//    line ends at the next contact.
// Not predicted: teching, slope or corner contacts, Down's own movement,
// wind, moving platforms, later hits.
namespace tm {
namespace extra {
    u8 g_diTrace = 0;

    enum {
        kEnergyGravity = 0x58,
        kEnergyDamage = 0x7C,
        kSpeed = 0x8,
        kAccel = 0x10,
        kMaxFall = 0x14,
        kDamageLog = 0x88, // getDamageLog's slot in the damage module's vtable (at +8)
        kLogSdiMul = 0x30,
        kMaxFrames = 150,
        kTrail = 9,
        kWidthMain = 30,
        kWidthThin = 12,
        kChord = 4,
        kGroundBrake = 3004, // the fighter's Ground_Brake
        kRayFighter = 1 // stRayCheck's target mask: the lines fighters touch
    };

    static const gfx::Rgba kBlue = 0x008AFFFF;
    static const gfx::Rgba kGrey = 0xC8C8C8B0;
    static const gfx::Rgba kRed = 0xFF2020FF;
    static const gfx::Rgba kLand = 0xFFFFFFFF;
    static const gfx::Rgba kHit = 0xFFE040FF;
    static const float kMark = 6.0f;
    static const float kLowEcb = 0.5f;
    static const float kTumbleBottom = 0.4f;
    static const float kFloorProbe = 3.0f;
    static const float kOnFloor = 0.1f;
    static const float kProbe = 4.0f;
    static const float kSameLine = 0.999f;
    static const float kUpright = 0.98f;
    static const float kDashOn = 3.0f;  // units drawn, then
    static const float kDashOff = 2.0f; // units skipped
    static const float kMaxDi = 0.31415927f; // 18 degrees

    // The positions a fighter (main fighters only) took during hitlag: the
    // hit's, then one per smash DI.
    struct Trail {
        s32 entryId;
        s32 n;
        float xy[kTrail][2];
    };

    // Per-fighter raw C-stick tracking (fallback if ipPadConfig state bytes are
    // unreadable; cleared in diReset() and on entry change / hitlag end).
    struct CStickTrack {
        s32 entryId;
        int lastHitlag;
        s8 prevCx, prevCy;
        bool valid;
    };

    // Active fighter launch + DI state recorded during diDraw() for the 2D unit-circle DI compass.
    struct DiCompassState {
        bool active;
        bool inHitlag;
        u8 mode;
        float launchX, launchY;
        float diX, diY;
        float liveX, liveY;
    };

    // RAM debug block for live Dolphin verification ("TMDP").
    struct DiPickerDebugBlock {
        u32 magic;        // 0x00 "TMDP"
        u32 compassDrawn; // 0x04
        u32 totalDrawn;   // 0x08
        u32 mode;         // 0x0C
        float launchX;    // 0x10
        float launchY;    // 0x14
        float diX;        // 0x18
        float diY;        // 0x1C
    };

    struct DiNet {
        Trail trails[game::kMaxFighters];
        CStickTrack cstick[game::kMaxFighters];
        DiCompassState compass;
        DiPickerDebugBlock pickerDebug;
    };

    static DiNet* s_net = NULL;

    static struct {
        float threshold, sdi, asdi, decay;
        float reflectSpeed, reflectMul, groundMul;
        float left, right, top, bottom;
    } s_env;
    // Where each is read (the common parameters 3284 Damage_Fly_Reflect_Speed,
    // 3285 its multiplier and 3293 Damage_Ground_Mul last) and the largest
    // value it is accepted with.
    static const u32 kEnvAt[7] = {0x80B88350, 0x80B88354, 0x80B88358, 0x80B88534, 0x80B8850C, 0x80B88510, 0x80B88530};
    static const float kEnvMax[7] = {1.0f, 20.0f, 20.0f, 1.0f, 10.0f, 2.0f, 10.0f};

    // A fighter in flight.
    struct Flight {
        float x, y;   // position (feet)
        float vx, vy; // knockback speed
        float g;      // gravity speed (y)
        float accel;  // fall acceleration (negative)
        float maxFall;
    };

    static struct {
        float x, y, lift;
        float down, up, half, mid;
        float brake; // knockback lost per frame on the ground
        int frames;
        bool ground, floor, trace;
    } s_body;

    // stRayCheck's first words as read on this setup (its fifth word, a
    // relative call, is skipped); checked once per attach.
    static const u32 kRayWords[] = {0x9421FF70, 0x7C0802A6, 0x90010094, 0x39610090,
                                    0, 0x7C781B78, 0x7C992378, 0x7CBA2B78};
    static bool s_rays = false;

#pragma dont_inline on
    static bool within(float v, float lo, float hi)
    {
        return v >= lo && v <= hi;
    }

    static DiVectorResult resolveDi(u8 mode, float kx, float ky, float posX, float stageCenterX, float facing,
                                    float liveX, float liveY)
    {
        return resolveDiVector(mode, kx, ky, posX, stageCenterX, facing, liveX, liveY);
    }

    static void setCompass(bool inHitlag, u8 mode, float lx, float ly, float dx, float dy, float sx, float sy)
    {
        if (s_net == NULL || (s_net->compass.active && (!inHitlag || s_net->compass.inHitlag)))
        {
            return;
        }
        DiCompassState& c = s_net->compass;
        c.active = true;
        c.inHitlag = inHitlag;
        c.mode = mode;
        c.launchX = lx;
        c.launchY = ly;
        c.diX = dx;
        c.diY = dy;
        c.liveX = sx;
        c.liveY = sy;
    }

    static float root(float v)
    {
        return game::stickRoot(v);
    }

    // Scales (x, y) back onto a circle of `radius` if it is longer.
    static void clampLength(float& x, float& y, float radius)
    {
        float len = root(x * x + y * y);
        if (len > radius)
        {
            x *= radius / len;
            y *= radius / len;
        }
    }
#pragma dont_inline reset

    // The energy with `id` at its place in the kinetic module's list; NULL
    // if the list is not as measured.
    static const void* energy(const soModuleEnumeration* m, u32 at, u32 id)
    {
        const void* kinetic = m->m_kineticModule;
        if (!game::isRam(kinetic) || game::wordAt(kinetic, at + 4) != id)
        {
            return NULL;
        }
        const void* e = reinterpret_cast<const void*>(game::wordAt(kinetic, at));
        return game::isRam(e) ? e : NULL;
    }

    // The hit's smash DI multiplier from the damage log, where the game's
    // getDamageLog finds it (its code read in the emulator: "addi r3, r3,
    // offset; blr"); 1 if anything differs.
    static float sdiMul(const soModuleEnumeration* m)
    {
        const void* damage = m->m_damageModule;
        const void* vtable = game::isRam(damage) ? reinterpret_cast<const void*>(game::wordAt(damage, 8)) : NULL;
        const u32* fn = game::isRam(vtable) ? reinterpret_cast<const u32*>(game::wordAt(vtable, kDamageLog)) : NULL;
        if (!game::isRam(fn) || (fn[0] & 0xFFFF0000) != 0x38630000 || fn[1] != 0x4E800020)
        {
            return 1.0f;
        }
        float v = floatAt(damage, (fn[0] & 0xFFFF) + kLogSdiMul);
        return within(v, 0.0f, 10.0f) ? v : 1.0f;
    }

    struct Stick {
        float x, y;   // main stick now
        float cx, cy; // C-stick now (humans' pads only)
        float lx, ly; // main stick on the fighter's last game frame
        s8 rawMainX, rawMainY;
        s8 rawCx, rawCy;
        u8 cstickMode;
        bool hasHumanPad;
        bool cstickWasActive;
    };

    // The hardware pad port (0-3 GameCube, 4-7 Wii) for a human fighter from
    // ipPadConfig (+0xB5 + playerNo), falling back to ref.port.
    static int humanPort(const game::FighterRef& ref, const u8* cfg)
    {
        if (game::isRam(cfg) && ref.playerNo >= 0 && ref.playerNo < game::kMaxFighters)
        {
            int contNo = cfg[kPadConfigPorts + ref.playerNo] & 0xF;
            if (contNo >= 0 && contNo < display::kPortPads)
            {
                return contNo;
            }
        }
        return ref.port;
    }

    // The sticks the fighter's DI will come from; false if P+'s Random DI
    // decides instead. A human's stick now: the game pads' (read also while
    // frame advance holds the game); a fighter the lab drives: what the lab
    // writes (the CPU Options' planned DI while they handle a hit).
    static bool readStick(const game::FighterRef& ref, bool partner, int hitlag, Stick& out)
    {
        memset(&out, 0, sizeof(out));
        out.cstickMode = kCStickNone;
        ftEntryManager* entries = extraEntryManager();
        ftEntry* entry = game::isRam(entries) ? entries->getEntity(ref.entryId) : NULL;
        if (!game::isRam(entry) || !game::isRam(entry->m_input))
        {
            return true;
        }
        int slot = entry->m_slotIndex;
        u8 ai = 0;
        if (cpu::aiFlag(slot, ai) && ai != 0)
        {
            return false;
        }
        const void* input = reinterpret_cast<const void*>(game::wordAt(entry->m_input, partner ? 0x10 : 0xC));
        if (game::isRam(input))
        {
            out.lx = out.x = floatAt(input, 0x10);
            out.ly = out.y = floatAt(input, 0x14);
        }
        s8 raw[4];
        u8 controllerType = kPadGcc;
        if (cpu::optionsDi(slot, out.x, out.y))
        {
            out.lx = out.x;
            out.ly = out.y;
        }
        else if (!ref.cpu && !cpu::controlled(slot))
        {
            const u8* cfg = reinterpret_cast<const u8*>(ipPadConfig::getInstance());
            int contNo = humanPort(ref, cfg);
            if (display::inputStickWithType(contNo, raw, &controllerType))
            {
                out.hasHumanPad = true;
                out.rawMainX = raw[0];
                out.rawMainY = raw[1];
                out.rawCx = raw[2];
                out.rawCy = raw[3];
                game::mainStick(raw[0], raw[1], &out.x, &out.y);
                out.x = mainDeadZone(out.x);
                out.y = mainDeadZone(out.y);
                game::cStick(raw[2], raw[3], &out.cx, &out.cy);
                if (game::isRam(cfg))
                {
                    out.cstickMode = cstickModeFromConfig(cfg, contNo, controllerType);
                    out.cstickWasActive = cstickWasActiveFromConfig(cfg, contNo, out.cstickMode);
                }
                else
                {
                    out.cstickMode = (controllerType == kPadGcc || controllerType == kPadClassic) ? kCStickSmash
                                                                                                   : kCStickNone;
                    if (s_net != NULL && !partner && ref.playerNo >= 0 && ref.playerNo < game::kMaxFighters)
                    {
                        CStickTrack& ct = s_net->cstick[ref.playerNo];
                        out.cstickWasActive = ct.valid && cstickActiveRaw(ct.prevCx, ct.prevCy);
                    }
                }
                if (s_net != NULL && !partner && ref.playerNo >= 0 && ref.playerNo < game::kMaxFighters)
                {
                    CStickTrack& ct = s_net->cstick[ref.playerNo];
                    if (ct.entryId != ref.entryId || !ct.valid || ct.lastHitlag != hitlag)
                    {
                        ct.entryId = ref.entryId;
                        ct.lastHitlag = hitlag;
                        ct.prevCx = raw[2];
                        ct.prevCy = raw[3];
                        ct.valid = true;
                    }
                }
            }
        }
        return true;
    }

#pragma dont_inline on
    // Moves the start by `distance` along the stick (scaled onto a circle of
    // 1); never down through the floor.
    static void shift(Flight& s, float x, float y, float distance)
    {
        clampLength(x, y, 1.0f);
        s.x += x * distance;
        s.y += (s_body.ground && y < 0.0f) ? 0.0f : y * distance;
    }

    // Turns the knockback by trajectory DI from stick (sx, sy).
    static void applyDi(Flight& s, float sx, float sy)
    {
        float len = root(s.vx * s.vx + s.vy * s.vy);
        if (len < 0.00001f)
        {
            return;
        }
        clampLength(sx, sy, 1.0025f);
        float c = (s.vx * sy - s.vy * sx) / len;
        c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
        float a = kMaxDi * c * (c < 0.0f ? -c : c);
        float a2 = a * a;
        float sn = a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f));
        float cs = 1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f);
        float x = s.vx * cs - s.vy * sn;
        s.vy = s.vx * sn + s.vy * cs;
        s.vx = x;
    }

    static gfx::Rgba colourAt(const Flight& s, gfx::Rgba colour)
    {
        bool out = s.x < s_env.left || s.x > s_env.right || s.y > s_env.top || s.y < s_env.bottom;
        return out ? kRed : colour;
    }

    // One frame of flight.
    static void step(Flight& s)
    {
        float len = root(s.vx * s.vx + s.vy * s.vy);
        float k = len > s_env.decay ? (len - s_env.decay) / len : 0.0f;
        s.vx *= k;
        s.vy *= k;
        s.g += s.accel;
        s.g = s.g < -s.maxFall ? -s.maxFall : s.g;
        s.x += s.vx;
        s.y += s.vy + s.g;
    }
#pragma dont_inline reset

    // Where a path first meets the stage.
    struct Contact {
        int frame;    // the path's frame that touches
        float x, y;   // the fighter's position when it touches
        float sx, sy; // the point of the stage touched
        float nx, ny; // the line's normal, facing the fighter
        float t;      // how far into the frame's move (dx, dy)
        float dx, dy;
        bool land;    // a floor, from above
    };

    // stRayCheck from (ox, oy) by (dx, dy); keeps the hit in `c` if it is
    // nearer than the fraction `t` of the way. Only the ECB's bottom lands:
    // a floor met from a side point (one at the height of a platform, say)
    // does not count.
    static void ray(float ox, float oy, float dx, float dy, float& t, Contact& c, bool side)
    {
        Vec3f pos(ox, oy, 0.0f);
        Vec3f dir(dx, dy, 0.0f);
        Vec3f hit, normal;
#ifdef TM_EXPERIMENTS
        experiments::g_rayCount++;
#endif
        if (!extraRayCheck(&pos, &dir, &hit, &normal, true, NULL, false, kRayFighter))
        {
            return;
        }
        float f = ((hit.m_x - ox) * dx + (hit.m_y - oy) * dy) / (dx * dx + dy * dy);
        bool floor = normal.m_y > 0.5f;
        if (f < t && !(side && floor))
        {
            t = f;
            c.sx = hit.m_x;
            c.sy = hit.m_y;
            c.nx = normal.m_x;
            c.ny = normal.m_y;
            c.land = floor;
        }
    }

    // Whether the fighter's ECB meets the stage on its way from a to b: a
    // ray from its bottom or top point, and one from the side it moves to.
    static bool touches(const Flight& a, const Flight& b, Contact& c)
    {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float t = 2.0f;
        if (dy != 0.0f)
        {
            ray(a.x, a.y + (dy < 0.0f ? s_body.down : s_body.up), dx, dy, t, c, false);
        }
        if (dx != 0.0f)
        {
            ray(a.x + (dx < 0.0f ? -s_body.half : s_body.half), a.y + s_body.mid, dx, dy, t, c, true);
        }
        if (t > 1.0f)
        {
            return false;
        }
        // A landing shows on the frame whose move crossed the floor, at that
        // frame's x, the feet put on the floor.
        c.x = c.land ? b.x : a.x + t * dx;
        c.y = c.land ? c.sy : a.y + t * dy;
        c.t = t;
        c.dx = dx;
        c.dy = dy;
        return true;
    }

    // The first frame on which the path from `start` meets the stage, in
    // c.frame; 0 if it does not within the frames drawn. One pair of rays
    // covers kChord frames; only a chord that meets the stage is cast again
    // frame by frame.
    static int firstContact(const Flight& start, int frames, Contact& c)
    {
        if (!s_rays)
        {
            return 0;
        }
        Flight a = start;
        for (int i = 0; i < frames;)
        {
            Flight b = a;
            int k = frames - i < kChord ? frames - i : kChord;
            for (int j = 0; j < k; j++)
            {
                step(b);
            }
            if (touches(a, b, c))
            {
                Flight p = a;
                for (int j = 1; j <= k; j++)
                {
                    Flight q = p;
                    step(q);
                    if (touches(p, q, c))
                    {
                        return c.frame = i + j;
                    }
                    p = q;
                }
            }
            a = b;
            i += k;
        }
        return 0;
    }

    // "Lands here": a line down to the floor and a bar along it. "Hits the
    // stage here": a line to the point touched and a cross on it.
    static void drawContact(const Contact& c, float lift)
    {
        gfx::Rgba m = c.land ? kLand : kHit;
        float w = c.land ? kMark : kMark * 0.6f;
        float h = c.land ? 0.0f : w;
        gfx::lineBegin(GX_LINES, c.land ? 4 : 6, kWidthMain);
        gfx::vertex(c.x, c.y + lift, m);
        gfx::vertex(c.sx, c.sy, m);
        gfx::vertex(c.sx - w, c.sy - h, m);
        gfx::vertex(c.sx + w, c.sy + h, m);
        if (!c.land)
        {
            gfx::vertex(c.sx - w, c.sy + h, m);
            gfx::vertex(c.sx + w, c.sy - h, m);
        }
    }

    // Whether there is a floor at x within kFloorProbe of y; y becomes its
    // height.
    static bool floorAt(float x, float& y)
    {
        Contact f;
        float t = 2.0f;
        if (!s_rays)
        {
            return false;
        }
        ray(x, y + kFloorProbe, 0.0f, -2.0f * kFloorProbe, t, f, false);
        if (t > 1.0f || !f.land)
        {
            return false;
        }
        y = f.sy;
        return true;
    }

    // A dashed line, kDashOn units drawn and kDashOff skipped, measured
    // along the path; it only counts its pieces unless `draw` is set.
    struct Dash {
        bool draw;
        int pieces;
        float phase; // how far into the pattern
        float x, y;  // the point reached
        gfx::Rgba colour;
    };

    static void dashTo(Dash& d, float x, float y)
    {
        float dx = x - d.x;
        float dy = y - d.y;
        float len = root(dx * dx + dy * dy);
        for (float done = 0.0f; done < len;)
        {
            bool on = d.phase < kDashOn;
            float span = (on ? kDashOn : kDashOn + kDashOff) - d.phase;
            float part = len - done < span ? len - done : span;
            if (on)
            {
                d.pieces++;
                if (d.draw)
                {
                    float a = done / len;
                    float b = (done + part) / len;
                    float ex = d.x + dx * b;
                    float ey = d.y + dy * b;
                    bool out = ex < s_env.left || ex > s_env.right || ey > s_env.top || ey < s_env.bottom;
                    gfx::Rgba c = out ? kRed : d.colour;
                    gfx::vertex(d.x + dx * a, d.y + dy * a + s_body.lift, c);
                    gfx::vertex(ex, ey + s_body.lift, c);
                }
            }
            d.phase += part;
            d.phase = d.phase >= kDashOn + kDashOff ? 0.0f : d.phase;
            done += part;
        }
        d.x = x;
        d.y = y;
    }

    // Batches trace point lines ("di: %c %d %.3f %.3f\n" or with "+") using
    // integer fixed-point formatting so tracing a 70-150 frame arc does not
    // spend a full console frame in per-line OSReport / float formatting.
    struct TraceBuf {
        char data[240];
        int len;
        void init() { len = 0; }
        void flush()
        {
            if (len > 0)
            {
                data[len] = '\0';
                TM_LOG("%s", data);
                len = 0;
            }
        }
        void putNum(s32 v, bool frac3);
        void putCoord(float v);
        void addPoint(char tag, bool plus, int frame, float x, float y);
    };

#pragma dont_inline on
    void TraceBuf::putNum(s32 v, bool frac3)
    {
        if (v < 0)
        {
            data[len++] = '-';
            v = -v;
        }
        u32 a = static_cast<u32>(v);
        char tmp[12];
        int n = 0;
        do
        {
            tmp[n++] = static_cast<char>('0' + (a % 10u));
            a /= 10u;
        } while (a != 0u || (frac3 && n < 4));
        while (n > 0)
        {
            if (frac3 && n == 3)
            {
                data[len++] = '.';
            }
            data[len++] = tmp[--n];
        }
    }
#pragma dont_inline reset

    void TraceBuf::putCoord(float v)
    {
        data[len++] = ' ';
        v = within(v, -100000.0f, 100000.0f) ? v : 0.0f;
        putNum(f2i(v >= 0.0f ? (v * 1000.0f + 0.5f) : (v * 1000.0f - 0.5f)), true);
    }

    void TraceBuf::addPoint(char tag, bool plus, int frame, float x, float y)
    {
        if (len > static_cast<int>(sizeof(data)) - 42)
        {
            flush();
        }
        data[len++] = 'd';
        data[len++] = 'i';
        data[len++] = ':';
        data[len++] = ' ';
        data[len++] = tag;
        if (plus)
        {
            data[len++] = '+';
        }
        data[len++] = ' ';
        putNum(frame, false);
        putCoord(x);
        putCoord(y);
        data[len++] = '\n';
    }

    // What follows a contact without a tech (rules above): the slide after
    // a landing (or from a grounded start) or the flight after a bounce,
    // from the contact frame's flight `at` for at most `left` frames, dashed
    // into `d`. Stops where the fighter stops, leaves the floor or meets the
    // stage again.
    static void follow(const Flight& at, const Contact& c, int left, Dash& d, char tag)
    {
        Flight s = at;
        s.x = c.x;
        s.y = c.y;
        int n = 0;
        TraceBuf tb;
        tb.init();
        if (c.land)
        {
            float b = s_body.brake;
            float v = s.vx;
            if (b <= 0.0f || (v < 0.0f ? -v : v) <= 2.0f * b)
            {
                return;
            }
            while (n < kMaxFrames)
            {
                v = v > b ? v - b : (v < -b ? v + b : 0.0f);
                if (v == 0.0f)
                {
                    break;
                }
                if (!floorAt(s.x + v, s.y))
                {
                    break;
                }
                s.x += v;
                n++;
                dashTo(d, s.x, s.y);
                if (d.draw && s_body.trace)
                {
                    tb.addPoint(tag, true, c.frame + n, s.x, s.y);
                }
            }
            tb.flush();
            return;
        }
        // Bounce (rules above): faster than Damage_Fly_Reflect_Speed into
        // the line.
        float dot = s.vx * c.nx + s.vy * c.ny;
        if (-dot <= s_env.reflectSpeed)
        {
            return;
        }
        // Only off an upright wall or level ceiling, the same line on both
        // sides of the point: on a slope the game pushes the ECB along the
        // line by up to 7 units (measured), and near a corner it can meet
        // another line or be pushed over it; the bounce would go elsewhere.
        if (c.nx * c.nx < kUpright && c.ny * c.ny < kUpright)
        {
            return;
        }
        for (int side = -1; side <= 1; side += 2)
        {
            Contact o;
            float t = 2.0f;
            float px = c.sx + kProbe * (c.nx - side * c.ny);
            float py = c.sy + kProbe * (c.ny + side * c.nx);
            ray(px, py, -2.0f * kProbe * c.nx, -2.0f * kProbe * c.ny, t, o, false);
            if (t > 1.0f || o.nx * c.nx + o.ny * c.ny < kSameLine)
            {
                return;
            }
        }
        float along = (1.0f - c.t) * (c.dx * c.ny - c.dy * c.nx);
        s.x += along * c.ny;
        s.y -= along * c.nx;
        s.vx = (s.vx - 2.0f * dot * c.nx) * s_env.reflectMul;
        s.vy = (s.vy - 2.0f * dot * c.ny) * s_env.reflectMul;
        s.g = 0.0f;
        Contact again;
        int end = firstContact(s, left, again);
        n = end != 0 ? end - 1 : left;
        for (int i = 0; i <= n; i++)
        {
            dashTo(d, s.x, s.y);
            if (d.draw && s_body.trace)
            {
                tb.addPoint(tag, true, c.frame + i, s.x, s.y);
            }
            step(s);
        }
        tb.flush();
    }

    // The continuation from (x, y), dashed, two thirds of `width`.
    static void drawFollow(const Flight& at, const Contact& c, int left, float x, float y, u8 width,
                           gfx::Rgba colour, char tag)
    {
        Dash d = {false, 0, 0.0f, x, y, colour};
        follow(at, c, left, d, tag);
        if (d.pieces > 0)
        {
            gfx::lineBegin(GX_LINES, 2 * d.pieces, static_cast<u8>(width * 2 / 3));
            Dash e = {true, 0, 0.0f, x, y, colour};
            follow(at, c, left, e, tag);
        }
    }

    // Draws the path from the body through the start of `start` until
    // hitstun ends or the fighter meets the stage, then what follows if it
    // does not tech, dashed and thinner; logs it while traced. On the
    // ground a knockback that does not lift the fighter only slides it.
    static void drawPath(const Flight& start, u8 width, gfx::Rgba colour, char tag)
    {
        Flight s = start;
        float lift = s_body.lift;
        Contact c;
        if (s_body.floor && s.vy <= 0.0f)
        {
            c.frame = 0;
            c.x = s.x;
            c.y = s.y;
            c.land = true;
            drawFollow(s, c, 0, s.x, s.y, width, colour, tag);
            return;
        }
        int end = firstContact(s, s_body.frames, c);
        int n = end != 0 ? end : s_body.frames;
        TraceBuf tb;
        tb.init();
        gfx::lineBegin(GX_LINESTRIP, n + 2, width);
        gfx::vertex(s_body.x, s_body.y + lift, colour);
        gfx::vertex(s.x, s.y + lift, colourAt(s, colour));
        for (int i = 1; i <= n; i++)
        {
            step(s);
            if (i == end)
            {
                gfx::vertex(c.x, c.y + lift, colourAt(s, colour));
                break;
            }
            gfx::vertex(s.x, s.y + lift, colourAt(s, colour));
            if (s_body.trace)
            {
                tb.addPoint(tag, false, i, s.x, s.y);
            }
        }
        tb.flush();
        if (end == 0)
        {
            return;
        }
        drawFollow(s, c, s_body.frames - end, c.x, c.y, width, colour, tag);
        drawContact(c, lift);
        if (s_body.trace)
        {
            TM_LOG("di: %c %s %d %.3f %.3f normal %.3f %.3f speed %.3f %.3f\n", tag, c.land ? "lands" : "touches", end,
                   c.x, c.y, c.nx, c.ny, s.vx, s.vy);
        }
    }

    // Alternating cyan and violet segments for smash DI shifts during hitlag.
    static void drawTrail(const Trail& t, float lift)
    {
        if (t.n < 2)
        {
            return;
        }
        gfx::lineBegin(GX_LINES, 2 * (t.n - 1), kWidthMain);
        for (int k = 1; k < t.n; k++)
        {
            gfx::Rgba c = (k & 1) ? 0x38E8FFFF : 0xB878FFFF;
            gfx::vertex(t.xy[k - 1][0], t.xy[k - 1][1] + lift, c);
            gfx::vertex(t.xy[k][0], t.xy[k][1] + lift, c);
        }
    }

    static bool readEnv()
    {
        float* v = &s_env.threshold;
        for (int i = 0; i < 7; i++)
        {
            v[i] = *reinterpret_cast<const float*>(kEnvAt[i]);
            if (!within(v[i], 0.0f, kEnvMax[i]))
            {
                return false;
            }
        }
        void* camera = display::cameraController();
        if (camera == NULL)
        {
            return false;
        }
        const float* zones = reinterpret_cast<const float*>(reinterpret_cast<u32>(camera) + 0x158);
        for (int i = 0; i < 4; i++)
        {
            v[7 + i] = zones[i];
        }
        return s_env.left < s_env.right && s_env.bottom < s_env.top;
    }

    static void fighter(const game::FighterRef& ref, Fighter* f, bool partner, Trail* t)
    {
        const soModuleEnumeration* m = game::fighterModules(f);
        if (m == NULL || !game::isRam(m->m_postureModule) || !game::isRam(m->m_stopModule)
            || !game::isRam(m->m_groundModule) || !game::isRam(m->m_situationModule))
        {
            return;
        }
        const void* gravity = energy(m, kEnergyGravity, 1);
        const void* damage = energy(m, kEnergyDamage, 4);
        int hitlag = static_cast<soStopModuleImpl*>(m->m_stopModule)->getHitStopRealFrame();
        int hitstun = game::fighterHitstun(m);
        Flight s;
        s.x = floatAt(m->m_postureModule, 0xC);
        s.y = floatAt(m->m_postureModule, 0x10);
        if (t != NULL)
        {
            if (t->entryId != ref.entryId || (hitlag <= 0 && hitstun <= 0))
            {
                t->entryId = ref.entryId;
                t->n = 0;
            }
            if (hitlag > 0 && t->n < kTrail && (t->n == 0 || t->xy[t->n - 1][0] != s.x || t->xy[t->n - 1][1] != s.y))
            {
                t->xy[t->n][0] = s.x;
                t->xy[t->n][1] = s.y;
                t->n++;
            }
        }
        if (s_net != NULL && !partner && ref.playerNo >= 0 && ref.playerNo < game::kMaxFighters)
        {
            CStickTrack& ct = s_net->cstick[ref.playerNo];
            if (ct.entryId != ref.entryId || hitlag <= 0)
            {
                ct.entryId = ref.entryId;
                ct.lastHitlag = 0;
                ct.prevCx = 0;
                ct.prevCy = 0;
                ct.valid = false;
            }
        }
        if (hitstun <= 0 || gravity == NULL || damage == NULL)
        {
            return;
        }
        s.vx = floatAt(damage, kSpeed);
        s.vy = floatAt(damage, kSpeed + 4);
        s.g = floatAt(gravity, kSpeed + 4);
        s.accel = floatAt(gravity, kAccel);
        s.maxFall = floatAt(gravity, kMaxFall);
        if (!within(s.accel, -2.0f, 0.0f) || !within(s.maxFall, 0.0f, 20.0f))
        {
            return;
        }
        s_body.frames = hitstun < kMaxFrames ? hitstun : kMaxFrames;
        s_body.x = s.x;
        s_body.y = s.y;
        // Draw trajectories from the fighter's ECB centre.
        float lift = m->m_groundModule->getCenterPos(0).m_y - s.y;
        s_body.lift = within(lift, 0.0f, 40.0f) ? lift : 0.0f;
        soGroundModule* ground = m->m_groundModule;
        Vec2f left = ground->getLeftPos(0);
        s_body.down = ground->getDownPos(0).m_y - s.y;
        s_body.up = ground->getUpPos(0).m_y - s.y;
        s_body.half = ground->getRightPos(0).m_x - left.m_x;
        s_body.half *= 0.5f;
        s_body.mid = left.m_y - s.y;
        soModuleAccesser* accesser = reinterpret_cast<soModuleAccesser*>(game::wordAt(f, 0x60));
        s_body.brake = extraConstFloat(accesser, kGroundBrake) * s_env.groundMul;
        s_body.brake = within(s_body.brake, 0.0f, 1.0f) ? s_body.brake : 0.0f;
        if (s_body.down < kLowEcb)
        {
            // The ECB's bottom is at the feet (standing, or held there just
            // after the launch); in tumble it rises (see the flight notes at the top).
            s_body.down = kTumbleBottom * (s_body.up - s_body.down);
        }
        if (t != NULL)
        {
            drawTrail(*t, s_body.lift);
        }
        // In hitlag a fighter hit on the ground can already be in the air
        // situation (its smash DI then moves it down in full); for staying
        // on the ground and sliding, its feet on a floor count.
        float feet = s.y;
        s_body.ground = m->m_situationModule->getKind() != Situation_Air;
        s_body.floor = s_body.ground || (hitlag > 0 && floorAt(s.x, feet) && s.y - feet < kOnFloor);
        Stick st;
        memset(&st, 0, sizeof(st));
        st.cstickMode = kCStickNone;
        u8 diMode = main::settings().display.diVectorMode;
        bool own = hitlag > 0 && readStick(ref, partner, hitlag, st);
        float stageCenterX = (s_env.left + s_env.right) * 0.5f;
        float facing = s.vx >= 0.0f ? 1.0f : -1.0f;
        int player = ref.playerNo + (partner ? 4 : 0);
        s_body.trace = g_diTrace == player + 1;
        if (s_body.trace)
        {
            TM_LOG("di: P%d at %.3f %.3f hitlag %d hitstun %d speed %.4f %.4f stick %.3f %.3f\n", player + 1, s.x, s.y,
                   hitlag, hitstun, s.vx, s.vy, st.x, st.y);
        }
        if (hitlag <= 0)
        {
            // Launched: the speeds already carry the DI.
            if (s_net != NULL && !s_net->compass.active)
            {
                Stick liveSt;
                readStick(ref, partner, 0, liveSt);
                DiVectorResult dv = resolveDi(diMode, s.vx, s.vy, s.x, stageCenterX, facing, liveSt.x, liveSt.y);
                setCompass(false, diMode, s.vx, s.vy, dv.dx, dv.dy, liveSt.x, liveSt.y);
            }
            drawPath(s, kWidthMain, kBlue, 'l');
            return;
        }
        bool hasPreview = own || diMode != main::kDiVectorLive;
        drawPath(s, hasPreview ? kWidthThin : kWidthMain, hasPreview ? kGrey : kBlue, 'n');
        if (!hasPreview)
        {
            return;
        }
        if (diMode != main::kDiVectorLive)
        {
            float launchVx = s.vx;
            float launchVy = s.vy;
            DiVectorResult dv = resolveDi(diMode, launchVx, launchVy, s.x, stageCenterX, facing, st.x, st.y);
            applyDi(s, dv.dx, dv.dy);
            setCompass(true, diMode, launchVx, launchVy, dv.dx, dv.dy, st.x, st.y);
            drawPath(s, kWidthMain, kBlue, 'p');
            return;
        }
        float mul = sdiMul(m);
        HitlagPreviewInput pin;
        pin.hitlag = hitlag;
        pin.mode = st.cstickMode;
        pin.hasHumanPad = st.hasHumanPad;
        pin.rawMainX = st.rawMainX;
        pin.rawMainY = st.rawMainY;
        pin.rawCx = st.rawCx;
        pin.rawCy = st.rawCy;
        pin.cstickWasActive = st.cstickWasActive;
        pin.prevMainX = st.lx;
        pin.prevMainY = st.ly;
        pin.fallbackMainX = st.x;
        pin.fallbackMainY = st.y;
        pin.threshold = s_env.threshold;
        HitlagPreviewResult preview = previewHitlagFrame(pin);
        for (int k = 0; k < preview.sdiCount; k++)
        {
            shift(s, preview.sdiX[k], preview.sdiY[k], s_env.sdi * mul);
        }
        // Downward ASDI on the ground keeps the fighter there, to slide,
        // unless the launch lifts it more than the ASDI moved it down
        // (inferred from the measurements, rules above).
        float sink = 0.0f;
        if (preview.hasAsdi)
        {
            sink = s_body.floor && preview.asdiY < 0.0f ? -preview.asdiY * s_env.asdi * mul : 0.0f;
            shift(s, preview.asdiX, preview.asdiY, s_env.asdi * mul);
        }
        float launchVx = s.vx;
        float launchVy = s.vy;
        applyDi(s, preview.tdiX, preview.tdiY);
        setCompass(true, diMode, launchVx, launchVy, preview.tdiX, preview.tdiY, st.x, st.y);
        if (s.vy + s.accel < sink)
        {
            // Stays on its floor (smash DI down moves it no lower).
            s.vy = 0.0f;
            s.y = s_body.y;
        }
        drawPath(s, kWidthMain, kBlue, 'p');
    }

    void diDraw(const game::MatchState& m)
    {
#ifdef TM_EXPERIMENTS
        experiments::rayProbeRun();
#endif
        if (s_net != NULL)
        {
            s_net->compass.active = false;
            s_net->compass.inHitlag = false;
        }
        if (s_net == NULL || !m.running || !readEnv())
        {
            return;
        }
        game::FighterRef refs[game::kMaxFighters];
        int n = game::listFighters(refs, game::kMaxFighters);
        for (int i = 0; i < n; i++)
        {
#ifdef TM_EXPERIMENTS
            u32 start = OSGetTick();
            experiments::g_rayCount = 0;
#endif
            fighter(refs[i], refs[i].fighter, false, s_net != NULL ? &s_net->trails[i] : NULL);
#ifdef TM_EXPERIMENTS
            if (experiments::g_rayProbe.ecb & 2)
            {
                u32 ticks = OSGetTick() - start;
                TM_LOG("di: time P%d %u ticks %u rays\n", refs[i].playerNo + 1, ticks, experiments::g_rayCount);
            }
#endif
            if (refs[i].partner != NULL)
            {
                fighter(refs[i], refs[i].partner, true, NULL);
            }
        }
    }

    // P+'s Draw DI (Code Menu > Debug Mode Settings) follows its Code Menu
    // line, found by its text from 0x804E0000 (u16 size, value +8, range
    // +0x10/+0x14, text +0x1C); not found, it is left alone. Hidden while the
    // DI Display is on: with both on it read the human's pad under frame
    // advance and left lines on screen (seen; setup not recorded).
    enum { kMenuData = 0x804E0000, kMenuScan = 0x3000, kLineText = 0x1C, kLineValue = 8 };
    static u32* s_drawDi = NULL;
    static bool s_drawDiSearched = false;
    static bool s_drawDiHidden = false;
    static u32 s_drawDiSaved = 0;

    static u32* findDrawDi()
    {
        for (u32 at = kMenuData; at < kMenuData + kMenuScan; at += 4)
        {
            const void* line = reinterpret_cast<const void*>(at);
            u32 size = *reinterpret_cast<const u16*>(line);
            if (game::sameString(reinterpret_cast<const char*>(at + kLineText), "Draw DI:  %s") && size >= 0x2A
                && size <= 0x40 && game::wordAt(line, 0x10) == 0 && game::wordAt(line, 0x14) == 1
                && game::wordAt(line, kLineValue) <= 1)
            {
                return reinterpret_cast<u32*>(at + kLineValue);
            }
        }
        return NULL;
    }

    void diHideDrawDi(bool hide)
    {
        if (hide && !s_drawDiSearched)
        {
            s_drawDiSearched = true;
            s_drawDi = findDrawDi();
            TM_LOG("di: Project+ Draw DI line %08x\n", s_drawDi);
        }
        if (s_drawDi == NULL)
        {
            return;
        }
        if (hide)
        {
            // Turned on in the Code Menu while hidden: that is the value
            // to put back.
            if (!s_drawDiHidden || *s_drawDi != 0)
            {
                s_drawDiSaved = *s_drawDi;
                *s_drawDi = 0;
                s_drawDiHidden = true;
            }
        }
        else if (s_drawDiHidden)
        {
            *s_drawDi = s_drawDiSaved;
            s_drawDiHidden = false;
        }
    }

    bool diCompassVisible(const game::MatchState& m, bool menuOpen)
    {
        const main::Settings& set = main::settings();
        if (s_net == NULL || !m.running || !m.training || m.paused || menuOpen || set.general.diDisplay == 0)
        {
            return false;
        }
        return s_net->compass.active || set.display.diVectorMode != main::kDiVectorLive;
    }

    void diCompassDraw(const game::MatchState& m, bool menuOpen)
    {
        if (s_net == NULL)
        {
            return;
        }
        DiCompassState& s_compass = s_net->compass;
        DiPickerDebugBlock& s_pickerDebug = s_net->pickerDebug;
        s_pickerDebug.compassDrawn = 0;
        const main::Settings& set = main::settings();
        u8 mode = set.display.diVectorMode;
        if (!m.running || m.paused || menuOpen || set.general.diDisplay == 0)
        {
            return;
        }
        if (!s_compass.active && mode == main::kDiVectorLive)
        {
            return;
        }

        float launchX = s_compass.launchX;
        float launchY = s_compass.launchY;
        float diX = s_compass.diX;
        float diY = s_compass.diY;
        float liveX = s_compass.liveX;
        float liveY = s_compass.liveY;
        if (!s_compass.active)
        {
            // Reference 45 deg preview launch when inspecting a preset while idle.
            launchX = 0.70710678f;
            launchY = 0.70710678f;
            liveX = 0.0f;
            liveY = 0.0f;
            game::FighterRef human;
            if (game::findFighter(false, human))
            {
                const u8* cfg = reinterpret_cast<const u8*>(ipPadConfig::getInstance());
                int port = humanPort(human, cfg);
                s8 raw[4];
                if (display::inputStickWithType(port, raw, NULL))
                {
                    game::mainStick(raw[0], raw[1], &liveX, &liveY);
                }
            }
            DiVectorResult dv = resolveDi(mode, launchX, launchY, 0.0f, 0.0f, 1.0f, liveX, liveY);
            diX = dv.dx;
            diY = dv.dy;
        }

        // Count active Input Viewer slots so the compass stacks cleanly below them.
        game::FighterRef refs[game::kMaxFighters];
        int viewerSlots = set.display.inputViewer != display::kWhoNone
                              ? countInputViewerSlots(refs, game::listFighters(refs, game::kMaxFighters))
                              : 0;

        ui::layout::Box b = hudRightCardBox(kHudCardDiCompass, gfx::widescreen(), viewerSlots);
        float xs = gfx::xScale();
        float x = b.x0;
        float y = b.y0;
        float w = b.x1 - b.x0;
        float h = b.y1 - b.y0;

        drawHudCard(x, y, w, h, xs, 0x2BB7ABFFu, 0x8EF5EA90u);

        // Unit-circle compass centered inside the lower body of the card.
        float cx = (b.x0 + b.x1) * 0.5f;
        float cy = b.y0 + 16.0f + (h - 19.0f) * 0.5f;
        float ry = 22.0f;
        float rx = ry * xs;

        drawUnitDial(cx, cy, rx, ry, 0x060E14D0u, 0x5A96A8F0u);

        // Launch direction arrow (amber-gold).
        float lLen = root(launchX * launchX + launchY * launchY);
        if (lLen > 0.001f)
        {
            float ux = launchX / lLen;
            float uy = launchY / lLen;
            float tipX = cx + ux * rx * 0.88f;
            float tipY = cy - uy * ry * 0.88f;
            gfx::lineBegin(GX_LINES, 2, 14);
            gfx::vertex(cx, cy, 0xF59E0BFFu);
            gfx::vertex(tipX, tipY, 0xF59E0BFFu);

            float head = 5.0f;
            float bx = tipX - ux * head * xs;
            float by = tipY + uy * head;
            float perpX = -uy * 3.0f * xs;
            float perpY = -ux * 3.0f;
            gfx::fillTri(tipX, tipY, bx + perpX, by + perpY, bx - perpX, by - perpY, 0xF59E0BFFu);
        }

        // Active DI vector ray (electric cyan).
        float dLen = root(diX * diX + diY * diY);
        if (dLen > 0.01f)
        {
            float dTipX = cx + diX * rx * 0.90f;
            float dTipY = cy - diY * ry * 0.90f;
            gfx::lineBegin(GX_LINES, 2, 16);
            gfx::vertex(cx, cy, 0x22D3EEFFu);
            gfx::vertex(dTipX, dTipY, 0x22D3EEFFu);
            gfx::fillEllipse(dTipX, dTipY, 3.0f * xs, 3.0f, 0x22D3EEFFu);
        }

        // Live stick dot (crisp white with dark rim).
        float lStickX = liveX;
        float lStickY = liveY;
        clampLength(lStickX, lStickY, 1.0f);
        float dotX = cx + lStickX * rx * 0.85f;
        float dotY = cy - lStickY * ry * 0.85f;
        gfx::fillEllipse(dotX, dotY, 3.2f * xs, 3.2f, 0x050C12E8u);
        gfx::fillEllipse(dotX, dotY, 2.3f * xs, 2.3f, 0xFFFFFFFFu);

        // Draw mode header text last to avoid a mid-card GX shape->text->shape switch.
        char label[16];
        diVectorShortLabel(mode, label);
        float scale = ui::style::scaleForCap(6.5f);
        float tx = x + ui::style::w(5.0f);
        float ty = ui::style::textYForCap(y + 6.0f, scale);
        drawShadowText(tx, ty, label, 0xE2EEF4FFu, scale);

        s_pickerDebug.compassDrawn = 1;
        s_pickerDebug.totalDrawn++;
        s_pickerDebug.mode = mode;
        s_pickerDebug.launchX = launchX;
        s_pickerDebug.launchY = launchY;
        s_pickerDebug.diX = diX;
        s_pickerDebug.diY = diY;
    }

    void diDrawCustomPickerOverlay(bool railMode)
    {
        const main::Settings& set = main::settings();
        if (set.cpu.options.control != cpu::kControlLab || set.cpu.options.tdi != cpu::kTdiCustom)
        {
            return;
        }
        float xs = gfx::xScale();
        float paneCenter = railMode ? 392.0f : 315.0f;
        bool compact = !railMode;
        float topY = compact ? 308.0f : 288.0f;
        float cardW = ui::style::w(196.0f);
        float cardH = compact ? 62.0f : 80.0f;
        float cardX = ui::style::cx(paneCenter) - cardW * 0.5f;

        drawHudCard(cardX, topY, cardW, cardH, xs, 0x2BB7ABFFu, 0x8EF5EA90u);

        float ry = compact ? 21.0f : 27.0f;
        float rx = ry * xs;
        float cx = cardX + ui::style::w(46.0f);
        float cy = topY + 5.0f + (cardH - 7.0f) * 0.5f;

        drawUnitDial(cx, cy, rx, ry, 0x060E14D8u, 0x5A96A8F0u);

        float vx = i2fScale(set.cpuTables.customDiX, 0.01f);
        float vy = i2fScale(set.cpuTables.customDiY, 0.01f);
        clampLength(vx, vy, 1.0f);
        float tipX = cx + vx * rx * 0.90f;
        float tipY = cy - vy * ry * 0.90f;
        if (vx * vx + vy * vy > 0.0004f)
        {
            gfx::lineBegin(GX_LINES, 2, 16);
            gfx::vertex(cx, cy, 0x22D3EEFFu);
            gfx::vertex(tipX, tipY, 0x22D3EEFFu);
        }
        gfx::fillEllipse(tipX, tipY, 3.5f * xs, 3.5f, 0x050C12E8u);
        gfx::fillEllipse(tipX, tipY, 2.5f * xs, 2.5f, 0x22D3EEFFu);

        float tx = cardX + ui::style::w(90.0f);
        float headScale = ui::style::scaleForCap(compact ? 7.2f : 8.0f);
        float bodyScale = ui::style::scaleForCap(compact ? 6.4f : 7.0f);
        float rowStep = compact ? 15.0f : 20.0f;
        float row0 = topY + (compact ? 10.0f : 14.0f);
        drawShadowText(tx, ui::style::textYForCap(row0, headScale), "CUSTOM DI", 0xFFD300FFu, headScale);
        char coords[24];
        snprintf(coords, sizeof(coords), "X %+d  Y %+d",
                 static_cast<int>(set.cpuTables.customDiX), static_cast<int>(set.cpuTables.customDiY));
        drawShadowText(tx, ui::style::textYForCap(row0 + rowStep, bodyScale), coords, 0xE2EEF4FFu, bodyScale);
        drawShadowText(tx, ui::style::textYForCap(row0 + rowStep * 2.0f, bodyScale), "A: Pin  B: Cancel", 0x9CB4C4FFu, bodyScale);
    }

    void diReset()
    {
        if (s_net == NULL)
        {
            return;
        }
        s_net->compass.active = false;
        s_net->compass.inHitlag = false;
        for (int i = 0; i < game::kMaxFighters; i++)
        {
            s_net->trails[i].n = 0;
            s_net->cstick[i].entryId = -1;
            s_net->cstick[i].lastHitlag = 0;
            s_net->cstick[i].prevCx = 0;
            s_net->cstick[i].prevCy = 0;
            s_net->cstick[i].valid = false;
        }
    }

    void diAttach()
    {
        freeNetPtr(s_net);
        s_net = allocNetArray<DiNet>(1);
        diReset();
        if (s_net != NULL)
        {
            memset(&s_net->pickerDebug, 0, sizeof(s_net->pickerDebug));
            s_net->pickerDebug.magic = 0x544D4450u; // "TMDP"
        }
        g_diTrace = 0;
        typedef bool (*RayFn)(Vec3f*, Vec3f*, Vec3f*, Vec3f*, bool, grCollision*, bool, u8);
        const u32* code = reinterpret_cast<const u32*>(static_cast<RayFn>(&stRayCheck));
        s_rays = true;
        for (u32 i = 0; i < sizeof(kRayWords) / 4; i++)
        {
            s_rays = s_rays && (kRayWords[i] == 0 || code[i] == kRayWords[i]);
        }
        if (s_net != NULL)
        {
            TM_LOG("di: trace byte at %08x; picker block at %08x; stage rays %s\n", &g_diTrace, &s_net->pickerDebug,
                   s_rays ? "ok" : "not as expected: off");
        }
    }

    void diDetach()
    {
        diReset();
        freeNetPtr(s_net);
    }
}
}
