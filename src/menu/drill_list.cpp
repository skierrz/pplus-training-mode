#include "menu/drill_list.h"

#include <cstdio>
#include <gf/gf_pad_status.h>
#ifdef __MWERKS__
#include <GX/GXGeometry.h>
#include <GX/GXHardware.h>
#endif

#include "extra/scenario_presets_core.h"
#include "main/gfx/draw.h"
#include "main/gfx/text.h"
#include "main/ui/hud_layout.h"
#include "main/ui/style.h"
#include "menu/browser.h"
#include "menu/game/select.h"
#include "menu/game/training.h"
#include "menu/osd_quick.h"
#include "menu/session.h"
#include "menu/sound.h"
#include "shared/drill_session.h"
#include "shared/game/pad_map.h"
#include "shared/interface.h"
#include "shared/log.h"

namespace tm {
namespace menu {
namespace list {
    enum {
        kPorts = iface::kPadCount, // 0-3 GameCube, 4-7 Wii
        kAllButtons = 0xFFFFFFFF,
        kStickDeadZone = 40,
        kRepeatStart = ui::style::kRepeatStart,
        kRepeatEvery = ui::style::kRepeatEvery,
        kVisibleRows = 10
    };

    enum LauncherRowKind {
        kRowLab,
        kRowScenario,
        kRowRecordings,
        kRowSkillDrill
    };

    struct LauncherRow {
        const char* name;
        u8 kind;
        u8 targetId; // extra::ScenarioId (kRowScenario) or drills::DrillId (kRowSkillDrill)
        const char* desc[3];
        const char* chipA;
        const char* chipB;
        const char* caption;
    };

    // Row indices referenced by section tables below.
    enum {
        kIdxLab = 0,
        kIdxLCancel = 1,
        kIdxWavedash = 2,
        kIdxLedgedash = 3,
        kIdxComboLab = 4,
        kIdxTechchase = 5,
        kIdxEdgeguard = 6,
        kIdxShieldPressure = 7,
        kIdxPowershield = 8,
        kIdxShieldDrop = 9,
        kIdxLedgetech = 10,
        kIdxSlideOff = 11,
        kIdxGrabDefense = 12,
        kIdxMultiHitSdi = 13,
        kIdxRar = 14,
        kIdxDacus = 15,
        kIdxBReverse = 16,
        kIdxFloatHover = 17,
        kIdxCancelSprint = 18,
        kIdxFlowCourse = 19,
        kIdxReactionLab = 20,
        kIdxTechchaseEscape = 21,
        kIdxRecoverySweetspot = 22,
        kIdxFloorhug = 23,
        kIdxSnakeRecovery = 24,
        kIdxRecordings = 25
    };

    // Intuitive All Modes order: Training Lab -> Core Fundamentals ->
    // Defensive & Situational -> P+ Advanced Tech -> P+ Challenges ->
    // Character Trainers -> Browse Recordings.
    static const LauncherRow kAllRows[] = {
        {
            "Training Lab",
            kRowLab,
            0,
            {
                "Open practice on any stage with",
                "savestates, CPU controls, frame",
                "data, hitboxes, and recordings."
            },
            "Any Fighter",
            "Any Stage",
            "In-match Start menu"
        },
        {
            "L-Cancel",
            kRowSkillDrill,
            drills::kDrillLCancel,
            {
                "Hit P+'s 7-frame L-cancel window",
                "on whiff, hit, or shield. Flags",
                "Auto L-Cancel when active."
            },
            "Any Fighter",
            "Free Practice",
            "L+Up in-match: 10/20-Rep Set"
        },
        {
            "Wavedash / Waveland",
            kRowSkillDrill,
            drills::kDrillWavedash,
            {
                "Practice frame-1 wavedashes and",
                "platform wavelands with angle,",
                "slide, and miss timing feedback."
            },
            "Any Fighter",
            "Free Practice",
            "L+Up in-match: 10/20-Rep Set"
        },
        {
            "Ledgedash",
            kRowSkillDrill,
            drills::kDrillLedgedash,
            {
                "Drill P+ ledgedashes, GALINT,",
                "Tourney Winner avoidance, and",
                "the 5-ledge-grab invuln limit."
            },
            "Any Fighter",
            "Free Practice",
            "L+Up in-match: 10/20-Rep Set"
        },
        {
            "Combo Lab",
            kRowScenario,
            extra::kScenarioComboLab,
            {
                "Practice follow-ups with random",
                "DI, techs, and P+ AI recovery.",
                "Resets after escape window."
            },
            "Any Fighter",
            "Any Stage",
            "Starts on match load"
        },
        {
            "Techchase",
            kRowScenario,
            extra::kScenarioTechchase,
            {
                "React to random tech rolls and",
                "getup options after knockdowns.",
                "Resets after each exchange."
            },
            "Any Fighter",
            "Any Stage",
            "Starts on match load"
        },
        {
            "Edgeguard",
            kRowScenario,
            extra::kScenarioEdgeguard,
            {
                "CPU recovers from offstage with",
                "P+ AI mixups and survival DI.",
                "Resets after KO or safe land."
            },
            "Any Fighter",
            "Any Stage",
            "Starts on match load"
        },
        {
            "Shield Pressure / OoS",
            kRowScenario,
            extra::kScenarioShieldPressure,
            {
                "Drill offense against CPU shield",
                "counters or defend & punish OoS.",
                "Switch Role in Scenarios menu."
            },
            "Any Fighter",
            "Any Stage",
            "Starts on match load"
        },
        {
            "Powershield",
            kRowSkillDrill,
            drills::kDrillPowershield,
            {
                "Land P+'s 2-frame physical or",
                "projectile powershields and",
                "5-frame PS-Drop punishes."
            },
            "Any Fighter",
            "Projectile CPU",
            "L+Up in-match: 10/20-Rep Set"
        },
        {
            "Shield Drop",
            kRowScenario,
            extra::kScenarioShieldDrop,
            {
                "Start on a side platform above",
                "the CPU to drill shield drops.",
                "Uses the floor on flat stages."
            },
            "Any Fighter",
            "Platform Stage",
            "Starts on match load"
        },
        {
            "Ledgetech",
            kRowScenario,
            extra::kScenarioLedgetech,
            {
                "Recover into the CPU's edgeguard",
                "attack to drill wall techs and",
                "consistent ledge timing."
            },
            "Any Fighter",
            "Wall Stage",
            "Starts on match load"
        },
        {
            "Slide-Off",
            kRowScenario,
            extra::kScenarioSlideOff,
            {
                "Start near a platform edge as",
                "the CPU attacks from below.",
                "DI off the edge to act sooner."
            },
            "Any Fighter",
            "Platform Stage / PS2",
            "Starts on match load"
        },
        {
            "Grab Defense / Throw DI",
            kRowScenario,
            extra::kScenarioGrabEscape,
            {
                "Mash out of CPU grabs with live",
                "efficiency or drill throw DI",
                "and your own throw timings."
            },
            "Any Fighter",
            "Any Stage",
            "Starts on match load"
        },
        {
            "P+ Multi-Hit SDI",
            kRowScenario,
            extra::kScenarioMultiHitSdi,
            {
                "CPU repeats multi-hit attacks so",
                "you can drill Smash DI escapes.",
                "Change move in the Start menu."
            },
            "Any Fighter",
            "Any Stage",
            "Starts on match load"
        },
        {
            "RAR",
            kRowSkillDrill,
            drills::kDrillRar,
            {
                "Execute Reverse Aerial Rush turn",
                "jumps and frame-1 back aerials",
                "retaining forward dash momentum."
            },
            "Any Fighter",
            "Free Practice",
            "L+Up in-match: 10/20-Rep Set"
        },
        {
            "DACUS",
            kRowSkillDrill,
            drills::kDrillDacus,
            {
                "Cancel dash attack frames 2-3",
                "into sliding up-smash, including",
                "hitlag Gatling DACUS timing."
            },
            "Any Fighter",
            "Free Practice",
            "L+Up in-match: 10/20-Rep Set"
        },
        {
            "B-Reverse / Wavebounce",
            kRowSkillDrill,
            drills::kDrillBReverse,
            {
                "Drill Turnaround-B, Frame-5",
                "B-Reverse momentum flips, and",
                "combined Wavebounce spacing."
            },
            "Any Fighter",
            "Free Practice",
            "L+Up in-match: 10/20-Rep Set"
        },
        {
            "Float / Hover & DJC",
            kRowSkillDrill,
            drills::kDrillHoverFloat,
            {
                "Practice Peach Float Cancels,",
                "Mewtwo Hover Cancels, and",
                "instant Double-Jump Cancels."
            },
            "Peach/Mewtwo/DJC",
            "Free Practice",
            "L+Up in-match: 10/20-Rep Set"
        },
        {
            "Cancel Sprint",
            kRowSkillDrill,
            drills::kDrillCancelSprint,
            {
                "Chain fighter-specific jump,",
                "hover, or special cancels",
                "with tight frame-delta scoring."
            },
            "Cancel Fighters",
            "Free / Set / 60s",
            "L+Up: Free / 10 / 20 / 60s"
        },
        {
            "Movement Flow Course",
            kRowSkillDrill,
            drills::kDrillFlowCourse,
            {
                "Link dash dance, wavedash, and",
                "platform waveland chains across",
                "the stage without losing flow."
            },
            "Any Fighter",
            "Free / Set / 60s",
            "L+Up: Free / 10 / 20 / 60s"
        },
        {
            "Situational Reaction Lab",
            kRowSkillDrill,
            drills::kDrillReactionLab,
            {
                "React to randomized visual cues",
                "with jump, shield, spotdodge,",
                "or attack within the frame cap."
            },
            "Any Fighter",
            "Free / Set / 60s",
            "L+Up: Free / 10 / 20 / 60s"
        },
        {
            "Universal Techchase Escape",
            kRowSkillDrill,
            drills::kDrillTechchaseEscape,
            {
                "Escape knockdowns with crisp",
                "tech-in-place, tech rolls, and",
                "clean mixup variety tracking."
            },
            "Any Fighter",
            "Free / Set / 60s",
            "L+Up: Free / 10 / 20 / 60s"
        },
        {
            "Universal Recovery Sweetspot",
            kRowSkillDrill,
            drills::kDrillRecoverySweetspot,
            {
                "Snap cleanly to the ledge inside",
                "live 3D sweetspot boundary lines",
                "from offstage recovery routes."
            },
            "Any Fighter",
            "3D Ledge Guides",
            "L+Up: Free / 10 / 20 / 60s"
        },
        {
            "Floorhug / ASDI-Down Tech",
            kRowSkillDrill,
            drills::kDrillFloorhug,
            {
                "Hold down through grounded hits",
                "to ASDI-down and punish fast",
                "on P+ landing and hitstun frames."
            },
            "Any Fighter",
            "Free / Set / 60s",
            "L+Up: Free / 10 / 20 / 60s"
        },
        {
            "Snake Infinite Recovery",
            kRowSkillDrill,
            drills::kDrillSnakeRecovery,
            {
                "Duelist - Cypher & C4 recovery chain",
                "Chain Cypher lift, C4 self-hit,",
                "and safe re-ledge recovery reps."
            },
            "Recommended: Snake",
            "Free / Set / 60s",
            "L+Up: Free / 10 / 20 / 60s"
        },
        {
            "Browse Recordings",
            kRowRecordings,
            0,
            {
                "Load or delete exported .tmr",
                "replays saved on your SD card.",
                "Launches the matchup directly."
            },
            "SD Card",
            ".tmr Replay",
            "Loads match directly"
        }
    };

    static const u8 kAllModeIndices[] = {
        kIdxLab,        kIdxLCancel,      kIdxWavedash,    kIdxLedgedash,  kIdxComboLab,
        kIdxTechchase,  kIdxEdgeguard,    kIdxShieldPressure, kIdxPowershield, kIdxShieldDrop,
        kIdxLedgetech,  kIdxSlideOff,     kIdxGrabDefense, kIdxMultiHitSdi, kIdxRar,
        kIdxDacus,      kIdxBReverse,     kIdxFloatHover,  kIdxCancelSprint, kIdxFlowCourse,
        kIdxReactionLab, kIdxTechchaseEscape, kIdxRecoverySweetspot, kIdxFloorhug, kIdxSnakeRecovery,
        kIdxRecordings
    };
    static const u8 kOffenseIndices[] = {
        kIdxComboLab, kIdxTechchase, kIdxEdgeguard, kIdxShieldPressure, kIdxGrabDefense
    };
    static const u8 kDefenseIndices[] = {
        kIdxShieldPressure, kIdxPowershield, kIdxShieldDrop, kIdxLedgetech,
        kIdxSlideOff, kIdxGrabDefense, kIdxMultiHitSdi
    };
    // 8 single-page rows (zero scrolling; kVisibleRows == 10).
    static const u8 kTechPracticeIndices[] = {
        kIdxLCancel, kIdxWavedash, kIdxLedgedash, kIdxPowershield,
        kIdxRar, kIdxDacus, kIdxBReverse, kIdxFloatHover
    };
    // 6 single-page rows (zero scrolling; room for Combo-Break Rush at merge).
    static const u8 kChallengesIndices[] = {
        kIdxCancelSprint, kIdxFlowCourse, kIdxReactionLab,
        kIdxTechchaseEscape, kIdxRecoverySweetspot, kIdxFloorhug
    };
    // Character-specific P+ trainers.
    static const u8 kCharTrainersIndices[] = {
        kIdxSnakeRecovery
    };

    struct LauncherSection {
        const char* name;
        const u8* indices;
        int count;
    };

    static const LauncherSection kSections[] = {
        {"All Modes", kAllModeIndices, static_cast<int>(sizeof(kAllModeIndices) / sizeof(kAllModeIndices[0]))},
        {"Offense", kOffenseIndices, static_cast<int>(sizeof(kOffenseIndices) / sizeof(kOffenseIndices[0]))},
        {"Defense", kDefenseIndices, static_cast<int>(sizeof(kDefenseIndices) / sizeof(kDefenseIndices[0]))},
        {"Tech Practice", kTechPracticeIndices,
         static_cast<int>(sizeof(kTechPracticeIndices) / sizeof(kTechPracticeIndices[0]))},
        {"Challenges", kChallengesIndices,
         static_cast<int>(sizeof(kChallengesIndices) / sizeof(kChallengesIndices[0]))},
        {"Character Trainers", kCharTrainersIndices,
         static_cast<int>(sizeof(kCharTrainersIndices) / sizeof(kCharTrainersIndices[0]))}
    };
    enum { kSectionCount = static_cast<int>(sizeof(kSections) / sizeof(kSections[0])) };

    static const LauncherRow& rowAt(const LauncherSection& sec, int idx)
    {
        return kAllRows[sec.indices[idx]];
    }

    using namespace ui::style;

    // ---- Boot menu palette (Native Project+ slanted teal banner & metallic craft) ----
    static const gfx::Rgba kBackdropDim = 0x050C10A8;
    static const gfx::Rgba kBannerTopCol = 0x36CFC2FF;
    static const gfx::Rgba kBannerBotCol = 0x1C9288FF;
    static const gfx::Rgba kBannerSheen = 0x9AF8EEB0;
    static const gfx::Rgba kBannerInk = 0x041B1AFF;
    static const gfx::Rgba kRowFillTop = 0x11242EE0;
    static const gfx::Rgba kRowFillBot = 0x0A161CE0;
    static const gfx::Rgba kRowSheen = 0x3A687A40;
    static const gfx::Rgba kRowEdge = 0x24424CFF;
    static const gfx::Rgba kCardFillTop = 0x10242EF2;
    static const gfx::Rgba kCardFillBot = 0x09151CF2;
    static const gfx::Rgba kCardTitle = 0xFFFFFFFF;
    static const gfx::Rgba kCardBody = 0xD8E6ECFF;
    static const gfx::Rgba kCardMuted = 0x9FB4BDFF;
    static const gfx::Rgba kChipTop = 0x2A4F5EFF;
    static const gfx::Rgba kChipBot = 0x162E38FF;
    static const gfx::Rgba kBadgeImportTop = 0xD6E0E6FF;
    static const gfx::Rgba kBadgeImportBot = 0x8E9EA8FF;
    static const gfx::Rgba kBadgeOsdTop = 0x4ED6E4FF;
    static const gfx::Rgba kBadgeOsdBot = 0x1B8C98FF;

    // ---- Boot menu geometry (640x480 2D space; text/glyphs inside 5% CRT safe area) ----
    static const float kBannerTop = 32.0f;
    static const float kBannerBottom = 82.0f;
    static const float kBannerRuleH = 2.0f;
    static const float kBannerRightTop = 498.0f;
    static const float kBannerRightBot = 472.0f;
    static const float kBannerTitleX = 40.0f;
    static const float kBannerTitleCap = 16.0f;
    static const float kBannerPillLeftX = 250.0f;
    static const float kBannerPillW = 24.0f;
    static const float kBannerPillH = 18.0f;
    static const float kBannerPillCap = 9.5f;
    static const float kBannerSecSlotL = 278.0f;
    static const float kBannerSecSlotR = 434.0f;
    static const float kBannerSecCap = 10.0f;
    static const float kBannerPillRightX = 438.0f;

    static const float kColLeft = 36.0f;
    static const float kColBarRight = 294.0f;
    static const float kColScrollLeft = 297.0f;
    static const float kColScrollW = 7.0f;
    static const float kColRight = 304.0f;
    static const float kColTop = 100.0f;
    static const float kColRowH = 26.0f;
    static const float kColPitch = 30.0f;
    static const float kColTextX = 52.0f;

    static const float kCardLeft = 320.0f;
    static const float kCardRight = 604.0f;
    static const float kCardTop = 100.0f;
    static const float kCardBottom = 404.0f;
    static const float kCardBorder = 4.0f;
    static const float kCardPadX = 16.0f;
    static const float kCardTitleCap = 14.5f;
    static const float kCardBodyCap = 10.5f;
    static const float kCardMetaCap = 9.5f;
    static const float kCardChipCap = 9.5f;
    static const float kCardChipH = 24.0f;
    static const float kCardChipPadX = 8.0f;
    static const float kCardChipGap = 8.0f;

    static const float kStripTop = 424.0f;
    static const float kStripH = 56.0f;
    static const float kStripCenterY = 442.0f;
    static const float kBadgeH = 18.0f;
    static const float kBadgeCap = 9.5f;
    static const float kBadgeLabelCap = 10.0f;

    struct Raw {
        u32 held;
        u32 pressed;
        u32 type;
        s8 stickX, stickY;
        bool connected;
    };

    struct Repeat {
        u16 held;
        u16 next;
    };

    enum {
        kTestMagic = 0x544D4D4E // "TMMN"
    };

    // Fixed layout: tests write a MatchSetup and set `request` to 1 to start
    // that Training match directly from the character select (no menu row or
    // controller binding).
    struct TestBlock {
        u32 magic;   // 0x00 kTestMagic
        u32 request; // 0x04 1 = start match; cleared to 0 when handled
        u32 result;  // 0x08 0 = none, 1 = started, 2 = refused
        u8 p1Kind;   // 0x0C gmCharacterKind (default Mario 0x00)
        u8 p1Icon;   // 0x0D CSS icon id (default Mario 0x00)
        u8 cpuKind;  // 0x0E gmCharacterKind (default Fox 0x07)
        u8 cpuIcon;  // 0x0F CSS icon id (default Fox 0x07)
        u16 stage;   // 0x10 srStageKind (default Final Destination 0x02)
        u8 port;     // 0x12 controller port 0-7 (default 0)
        u8 reserved; // 0x13
    };
    static TestBlock s_test;

    static Raw s_sys[kPorts];
    static u32 s_releaseMask[kPorts];  // buttons hidden until released
    static bool s_stickWait[kPorts];   // sticks hidden until back to neutral
    static bool s_waitRelease[kPorts]; // each port must go neutral before acting
    static bool s_open = false;
    static bool s_starting = false;   // a recording match is being started
    static bool s_rebuilding = false; // the character select is being built again
    static bool s_exiting = false;    // leaving CSS cleanly back to Solo menu
    static int s_owner = 0;
    static int s_page = 0;
    static int s_row = 0;
    static int s_scroll = 0;
    static Repeat s_repeat[2];
    static bool s_fresh = false; // no presses on the first capture after attach

    static u32 activeType()
    {
        return (s_owner >= 0 && s_owner < kPorts && s_sys[s_owner].connected)
                   ? s_sys[s_owner].type
                   : static_cast<u32>(gfPadType::GCC);
    }

    static const LauncherSection& currentSection()
    {
        return kSections[s_page];
    }

    static const LauncherRow& current()
    {
        return rowAt(currentSection(), s_row);
    }

    static bool stickNeutral(const gfPadStatus& pad)
    {
        int v[4] = {pad.m_stickX, pad.m_stickY, pad.m_subStickX, pad.m_subStickY};
        for (int i = 0; i < 4; i++)
        {
            if (v[i] > kStickDeadZone || v[i] < -kStickDeadZone)
            {
                return false;
            }
        }
        return true;
    }

    // Removes `mask` from every button word of the record, so neither the
    // buttons nor their edges reach whatever reads it next.
    static void hide(gfPadStatus& pad, u32 mask, bool sticks)
    {
        if (mask != 0)
        {
            pad.m_buttonsCurrentFrame.bits &= ~mask;
            pad.m_buttonsCurrentFrame2.bits &= ~mask;
            pad.m_buttonsHeld.bits &= ~mask;
            pad.m_buttonsPressedThisFrame.bits &= ~mask;
            pad.m_buttonsReleasedThisFrame.bits &= ~mask;
            pad.m_buttonsPressedThisFrame2.bits &= ~mask;
            if (mask & game::kPadL)
            {
                pad.m_lTriggerAnalog = 0;
            }
            if (mask & game::kPadR)
            {
                pad.m_rTriggerAnalog = 0;
            }
        }
        if (sticks)
        {
            pad.m_stickX = 0;
            pad.m_stickY = 0;
            pad.m_subStickX = 0;
            pad.m_subStickY = 0;
        }
    }

    static int repeat(Repeat& r, bool down)
    {
        if (!down)
        {
            r.held = 0;
            return 0;
        }
        r.held++;
        if (r.held == 1)
        {
            r.next = kRepeatStart + 1;
            return 1;
        }
        if (r.held < r.next)
        {
            return 0;
        }
        r.next = r.held + kRepeatEvery;
        return 1;
    }

    static void keepRowVisible()
    {
        if (s_row < s_scroll)
        {
            s_scroll = s_row;
        }
        if (s_row >= s_scroll + kVisibleRows)
        {
            s_scroll = s_row - kVisibleRows + 1;
        }
    }

    static void rebuildIfNeeded()
    {
        if (!select::needsRebuild(drills::kDrillNone))
        {
            return;
        }
        const char* why = select::reopen();
        if (why != NULL)
        {
            TM_LOG("drills: character select not rebuilt: %s\n", why);
            return;
        }
        s_rebuilding = true;
        TM_LOG("drills: character select rebuilt\n");
    }

    static void remember()
    {
        session().page = static_cast<u8>(s_page);
        session().row = static_cast<u8>(s_row);
    }

    static bool rawNeutral(const Raw& in)
    {
        return in.held == 0 && in.stickX <= kStickDeadZone && in.stickX >= -kStickDeadZone
               && in.stickY <= kStickDeadZone && in.stickY >= -kStickDeadZone;
    }

    static void resetWaitRelease()
    {
        for (int i = 0; i < kPorts; i++)
        {
            s_waitRelease[i] = true;
        }
    }

    static void open(int port, const char* how)
    {
        s_open = true;
        s_owner = (port >= 0 && port < kPorts) ? port : 0;
        resetWaitRelease();
        s_page = session().page < kSectionCount ? session().page : 0;
        s_row = session().row < currentSection().count ? session().row : 0;
        s_scroll = 0;
        keepRowVisible();
        s_repeat[0].held = s_repeat[1].held = 0;
        gfx::forgetWidths();
        TM_LOG("drills: list opened (%s) for port %d, row %d %s\n", how, s_owner + 1, s_row, current().name);
    }

    // Whatever is held when the list goes away stays hidden from the
    // character select until it is let go.
    static void holdBackHeld()
    {
        for (int i = 0; i < kPorts; i++)
        {
            s_releaseMask[i] = s_sys[i].held;
            s_stickWait[i] = true;
        }
    }

    static void close(const char* why)
    {
        s_open = false;
        session().labChosen = 1;
        remember();
        holdBackHeld();
        TM_LOG("drills: list closed (%s)\n", why);
    }

    static void selectRow(u32 controllerType)
    {
        const LauncherRow& r = current();
        if (r.kind == kRowLab)
        {
            session().pendingScenario = 0;
            session().drill = drills::kDrillNone;
            close("Training Lab");
            sound(game::kSoundConfirm);
            rebuildIfNeeded();
            return;
        }
        if (r.kind == kRowScenario)
        {
            session().pendingScenario = r.targetId;
            session().drill = drills::kDrillNone;
            TM_LOG("drills: scenario %u (%s) armed\n", r.targetId, r.name);
            close(r.name);
            sound(game::kSoundConfirm);
            rebuildIfNeeded();
            return;
        }
        if (r.kind == kRowSkillDrill)
        {
            session().drill = r.targetId;
            session().pendingScenario = 0;
            session().drillRan = 0;
            TM_LOG("drills: tech practice %u (%s) armed\n", r.targetId, r.name);
            close(r.name);
            sound(game::kSoundConfirm);
            rebuildIfNeeded();
            return;
        }
        if (r.kind == kRowRecordings)
        {
            sound(game::kSoundConfirm);
            browser::open(s_owner, controllerType);
            return;
        }
        sound(game::kSoundError);
    }

    static void changePage(int delta)
    {
        s_page = (s_page + delta + kSectionCount) % kSectionCount;
        s_row = 0;
        s_scroll = 0;
        s_repeat[0].held = s_repeat[1].held = 0;
        gfx::forgetWidths();
        TM_LOG("drills: page %s\n", currentSection().name);
        sound(game::kSoundMove);
    }

    static void moveRow(int delta)
    {
        int next = s_row + delta;
        if (next < 0 || next >= currentSection().count)
        {
            return;
        }
        s_row = next;
        keepRowVisible();
        TM_LOG("drills: row %d %s\n", s_row, current().name);
        sound(game::kSoundMove);
    }

    static bool osdQuickPressed(const Raw& in)
    {
        if (in.type == gfPadType::GCC || in.type == gfPadType::WII_CLASSIC)
        {
            return (in.pressed & game::kPadY) != 0;
        }
        if (in.type == gfPadType::NUNCHUK)
        {
            return (in.pressed & game::kPadZ) != 0;
        }
        if (in.type == gfPadType::WIIMOTE)
        {
            return (in.held & game::kPadWiiMinus) != 0 && (in.held & game::kPadWiiA) != 0
                   && (in.pressed & (game::kPadWiiMinus | game::kPadWiiA)) != 0;
        }
        return false;
    }

    static void step()
    {
        for (int i = 0; i < kPorts; i++)
        {
            if (!s_sys[i].connected)
            {
                s_waitRelease[i] = true;
            }
            else if (s_waitRelease[i] && rawNeutral(s_sys[i]))
            {
                s_waitRelease[i] = false;
            }
        }
        // Any connected controller (GameCube 0-3 or Wii 4-7) may take the list
        // over when the current owner is disconnected or idle, never while the
        // owner is active.
        bool ownerActive = s_sys[s_owner].connected && !rawNeutral(s_sys[s_owner]);
        if (!ownerActive)
        {
            for (int i = 0; i < kPorts; i++)
            {
                if (i == s_owner || !s_sys[i].connected || s_waitRelease[i] || rawNeutral(s_sys[i]))
                {
                    continue;
                }
                u32 oldType = activeType();
                s_owner = i;
                s_waitRelease[s_owner] = true;
                s_repeat[0].held = s_repeat[1].held = 0;
                if (s_sys[s_owner].type != oldType)
                {
                    gfx::forgetWidths();
                }
                if (browser::isOpen())
                {
                    browser::setControllerType(s_sys[s_owner].type);
                }
                if (osdQuick::isOpen())
                {
                    osdQuick::setControllerType(s_sys[s_owner].type);
                }
                TM_LOG("drills: list handed to port %d\n", s_owner + 1);
                break;
            }
        }
        const Raw& in = s_sys[s_owner];
        if (browser::isOpen())
        {
            if (!in.connected || s_waitRelease[s_owner])
            {
                return;
            }
            browser::Input bi = {in.held, in.pressed, in.stickX, in.stickY, in.type};
            browser::step(s_owner, bi, s_starting);
            if (s_starting)
            {
                session().pendingScenario = 0;
                session().drill = drills::kDrillNone;
                session().labChosen = 0;
                remember();
            }
            else if (!browser::isOpen())
            {
                resetWaitRelease();
                gfx::forgetWidths();
            }
            return;
        }
        if (osdQuick::isOpen())
        {
            if (!in.connected || s_waitRelease[s_owner])
            {
                return;
            }
            osdQuick::Input oi = {in.held, in.pressed, in.stickX, in.stickY, in.type};
            osdQuick::step(s_owner, oi);
            if (!osdQuick::isOpen())
            {
                resetWaitRelease();
                gfx::forgetWidths();
            }
            return;
        }
        if (!in.connected || s_waitRelease[s_owner])
        {
            s_repeat[0].held = s_repeat[1].held = 0;
            return;
        }
        if (osdQuickPressed(in))
        {
            sound(game::kSoundConfirm);
            osdQuick::open(s_owner, in.type);
            resetWaitRelease();
            return;
        }
        const game::PadMap& map = game::padMap(in.type);
        bool pagePrev = (in.pressed & map.pagePrev) != 0 && (in.held & map.pageNext) == 0;
        bool pageNext = (in.pressed & map.pageNext) != 0 && (in.held & map.pagePrev) == 0;
        if (pagePrev)
        {
            changePage(-1);
            return;
        }
        if (pageNext)
        {
            changePage(1);
            return;
        }
        bool up = (in.held & map.up) || in.stickY > kStickDeadZone;
        bool down = (in.held & map.down) || in.stickY < -kStickDeadZone;
        int moveUp = repeat(s_repeat[0], up && !down);
        int moveDown = repeat(s_repeat[1], down && !up);
        if (moveUp)
        {
            moveRow(-1);
        }
        if (moveDown)
        {
            moveRow(1);
        }
        if (in.pressed & map.back)
        {
            // Natural Nintendo B hierarchy: B on Boot Menu exits cleanly to Brawl's Solo menu.
            const char* why = training::exitToSolo();
            if (why == NULL)
            {
                s_open = false;
                s_exiting = true;
                session().pendingScenario = 0;
                session().drill = drills::kDrillNone;
                session().labChosen = 0;
                remember();
                holdBackHeld();
                sound(game::kSoundBack);
                TM_LOG("drills: exiting to Solo (%s)\n", map.backName);
                return;
            }
            close(map.backName);
            sound(game::kSoundBack);
            return;
        }
        if (in.pressed & map.alt)
        {
            sound(game::kSoundConfirm);
            browser::open(s_owner, in.type);
            return;
        }
        if (in.pressed & map.confirm)
        {
            selectRow(in.type);
        }
    }

    static void checkReopen()
    {
        if (!training::selectRunning())
        {
            return;
        }
        for (int i = 0; i < kPorts; i++)
        {
            if (!s_sys[i].connected)
            {
                continue;
            }
            const game::PadMap& map = game::padMap(s_sys[i].type);
            if (game::reopenPressed(map, s_sys[i].held, s_sys[i].pressed))
            {
                open(i, map.reopenName);
                holdBackHeld();
                sound(game::kSoundOpen);
                return;
            }
            // Natural B step-back from CSS to Boot Menu when P1 has no placed
            // coin to recall, is not dragging CPU's coin, and no dropdown is open.
            if ((s_sys[i].pressed & map.back) != 0 && select::canBackToBootMenu(i))
            {
                select::clearBHoldCounter();
                open(i, map.backName);
                holdBackHeld();
                sound(game::kSoundOpen);
                return;
            }
        }
    }

    static void capture(const gfPadStatus* pads)
    {
        for (int i = 0; i < kPorts; i++)
        {
            bool connected = game::padConnected(i, pads[i]);
            u32 type = connected ? static_cast<u32>(pads[i].m_controllerType) : 0;
            bool same = connected && s_sys[i].connected && type == s_sys[i].type;
            u32 held = connected ? pads[i].m_buttonsCurrentFrame.bits : 0;
            s_sys[i].pressed = (s_fresh || !same) ? 0 : held & ~s_sys[i].held;
            s_sys[i].held = held;
            s_sys[i].type = type;
            s_sys[i].stickX = connected ? pads[i].m_stickX : 0;
            s_sys[i].stickY = connected ? pads[i].m_stickY : 0;
            s_sys[i].connected = connected;
            if (!same)
            {
                s_waitRelease[i] = true;
                s_releaseMask[i] = 0;
                if (i == s_owner)
                {
                    gfx::forgetWidths();
                    if (browser::isOpen())
                    {
                        browser::setControllerType(type);
                    }
                    if (osdQuick::isOpen())
                    {
                        osdQuick::setControllerType(type);
                    }
                }
            }
        }
        s_fresh = false;
    }

    static void applyHiding(gfPadStatus* pads)
    {
        bool all = s_open || s_starting || s_rebuilding || s_exiting;
        for (int i = 0; i < kPorts; i++)
        {
            if (all)
            {
                hide(pads[i], kAllButtons, true);
            }
            else
            {
                hide(pads[i], s_releaseMask[i], s_stickWait[i]);
            }
        }
    }

    static void handleTestRequest()
    {
        if (s_test.request != 1 || s_starting || s_rebuilding || s_exiting)
        {
            return;
        }
        s_test.request = 0;
        training::MatchSetup setup = {s_test.p1Kind, s_test.p1Icon, s_test.cpuKind, s_test.cpuIcon, s_test.stage};
        const char* why = training::startMatch(setup, s_test.port);
        if (why != NULL)
        {
            s_test.result = 2;
            TM_LOG("drills: test start refused: %s\n", why);
            return;
        }
        s_test.result = 1;
        s_starting = true;
        session().pendingScenario = 0;
        session().drill = drills::kDrillNone;
        session().labChosen = 0;
        session().drillsStarted++;
        if (s_open)
        {
            remember();
        }
        TM_LOG("drills: starting test match (p1 %u, cpu %u, stage %#x, start %u)\n", setup.p1Kind, setup.cpuKind,
               setup.stage, session().drillsStarted);
    }

    void onPads(u32 phase, gfPadStatus* pads)
    {
        if (phase == iface::kPadsSystem)
        {
            capture(pads);
            for (int i = 0; i < kPorts; i++)
            {
                s_releaseMask[i] &= s_sys[i].held;
                if (s_stickWait[i] && (!s_sys[i].connected || stickNeutral(pads[i])))
                {
                    s_stickWait[i] = false;
                }
            }
            handleTestRequest();
            if (s_open && !s_starting && !s_exiting)
            {
                step();
            }
            else if (!s_starting && !s_rebuilding && !s_exiting)
            {
                checkReopen();
            }
        }
        applyHiding(pads);
    }

    // ---- Boot menu drawing helpers ----

    static void fillSlantedGradV(float x0, float x1Top, float x1Bot, float y0, float y1, gfx::Rgba topCol,
                                 gfx::Rgba botCol)
    {
#ifdef __MWERKS__
        gfx::prepare();
        GXBegin(GX_QUADS, GX_VTXFMT1, 4);
        gfx::vertex(x0, y0, topCol);
        gfx::vertex(x1Top, y0, topCol);
        gfx::vertex(x1Bot, y1, botCol);
        gfx::vertex(x0, y1, botCol);
#else
        gfx::vertex(x0, y0, topCol);
        gfx::vertex(x1Top, y0, topCol);
        gfx::vertex(x1Bot, y1, botCol);
        gfx::vertex(x0, y1, botCol);
#endif
    }

    static void drawBanner(u32 type)
    {
        fillSlantedGradV(0.0f, cx(kBannerRightTop), cx(kBannerRightBot), kBannerTop, kBannerBottom, kBannerTopCol,
                         kBannerBotCol);
        fillSlantedGradV(0.0f, cx(kBannerRightTop), cx(kBannerRightTop - 1.0f), kBannerTop, kBannerTop + 2.0f,
                         kBannerSheen, kBannerSheen);
        fillSlantedGradV(0.0f, cx(kBannerRightBot), cx(kBannerRightBot - 1.0f), kBannerBottom,
                         kBannerBottom + kBannerRuleH, kBannerInk, kBannerInk);

        float midY = (kBannerTop + kBannerBottom) * 0.5f;
        float titleScale = displayScaleForCap(kBannerTitleCap);
        gfx::displayText(cx(kBannerTitleX), displayYForCap(midY - kBannerTitleCap * 0.5f, titleScale), "TRAINING MODE",
                         kLabTitleTop, kLabTitleBot, titleScale, titleScale, 1.4f, kBannerInk);

        bool wiiShoulder = type == gfPadType::WIIMOTE || type == gfPadType::NUNCHUK;
        const char* prevGlyph = wiiShoulder ? "-" : "L";
        const char* nextGlyph = wiiShoulder ? "+" : "R";

        float pillScale = scaleForCap(kBannerPillCap);
        float pillY = midY - kBannerPillH * 0.5f;
        float pillW = w(kBannerPillW);
        float pillTextY = textYForCap(midY - kBannerPillCap * 0.5f, pillScale);

        float prevW = gfx::textWidth(prevGlyph, pillScale);
        float nextW = gfx::textWidth(nextGlyph, pillScale);
        float leftPillX = cx(kBannerPillLeftX);
        float rightPillX = cx(kBannerPillRightX);
        drawGlossyPillBadge(leftPillX, pillY, pillW, kBannerPillH, kLabBtnLightTop, kLabBtnLightBot);
        drawGlossyPillBadge(rightPillX, pillY, pillW, kBannerPillH, kLabBtnLightTop, kLabBtnLightBot);
        gfx::text(leftPillX + (pillW - prevW) * 0.5f, pillTextY, prevGlyph, kLabBtnDarkText, pillScale);
        gfx::text(rightPillX + (pillW - nextW) * 0.5f, pillTextY, nextGlyph, kLabBtnDarkText, pillScale);

        const char* secName = currentSection().name;
        float secScale = displayScaleForCap(kBannerSecCap);
        float secW = gfx::displayTextWidth(secName, secScale);
        float slotCenter = (cx(kBannerSecSlotL) + cx(kBannerSecSlotR)) * 0.5f;
        float slotLimit = cx(kBannerSecSlotR) - cx(kBannerSecSlotL);
        float secSx = secW > slotLimit && secW > 0.0f ? secScale * slotLimit / secW : secScale;
        float drawW = secW > slotLimit ? slotLimit : secW;
        gfx::displayText(slotCenter - drawW * 0.5f, displayYForCap(midY - kBannerSecCap * 0.5f, secScale), secName,
                         kBannerInk, kBannerInk, secSx, secScale, 0.0f, 0);
    }

    static void drawLeftColumn()
    {
        const LauncherSection& sec = currentSection();
        float scale = scaleForCap(kRowCap);
        float capOffset = (kColRowH - kRowCap) * 0.5f;
        float left = cx(kColLeft);
        float width = w(kColBarRight - kColLeft);
        float textX = cx(kColTextX);
        float textMaxW = cx(kColBarRight) - w(8.0f) - textX;

        for (int r = 0; r < kVisibleRows && s_scroll + r < sec.count; r++)
        {
            int idx = s_scroll + r;
            bool selected = idx == s_row;
            float y = kColTop + r * kColPitch;
            if (selected)
            {
                drawBevelBar(left, y, width, kColRowH, kLabGoldTop, kLabGoldBot, kLabGoldSheen, kLabGoldEdge);
                drawCursorTri(cx(kColLeft + 4.0f), y + kColRowH * 0.5f, 8.5f);
            }
            else
            {
                drawBevelBar(left, y, width, kColRowH, kRowFillTop, kRowFillBot, kRowSheen, kLabBevelDark);
            }

            const char* name = rowAt(sec, idx).name;
            float nw = boldWidth(name, scale);
            float sx = nw > textMaxW && nw > 0.0f ? scale * textMaxW / nw : scale;
            boldText(textX, textYForCap(y + capOffset, scale), name, selected ? kLabGoldInk : kText, sx, scale);
        }

        if (sec.count > kVisibleRows)
        {
            float sbLeft = cx(kColScrollLeft);
            float sbW = w(kColScrollW);
            float sbH = (kVisibleRows - 1) * kColPitch + kColRowH;
            gfx::fillRect(sbLeft, kColTop, sbW, sbH, kLabDivider);
            float inner = sbH - 4.0f;
            float thumbH = inner * kVisibleRows / sec.count;
            if (thumbH < 18.0f)
            {
                thumbH = 18.0f;
            }
            int maxScroll = sec.count - kVisibleRows;
            int sc = s_scroll > maxScroll ? maxScroll : s_scroll;
            float thumbY = kColTop + 2.0f + (inner - thumbH) * sc / maxScroll;
            gfx::fillGradV(sbLeft + w(1.0f), thumbY, sbW - w(2.0f), thumbH, kLabTealTop, kLabTealBot);
        }
    }

    static float drawChip(float x, float y, const char* label, float maxW, bool highlight = false)
    {
        float scale = scaleForCap(kCardChipCap);
        float tw = gfx::textWidth(label, scale);
        float pad = w(kCardChipPadX);
        float chipW = tw + 2.0f * pad;
        float sx = scale;
        if (chipW > maxW && tw > 0.0f)
        {
            float avail = maxW - 2.0f * pad;
            sx = scale * avail / tw;
            chipW = maxW;
        }
        float bx = w(1.2f);
        gfx::Rgba topCol = highlight ? kLabGoldTop : kChipTop;
        gfx::Rgba botCol = highlight ? kLabGoldBot : kChipBot;
        gfx::Rgba txtCol = highlight ? kLabGoldInk : kText;
        fillStadium(x - bx, y - 1.0f, chipW + 2.0f * bx, kCardChipH + 2.0f, 0x05080CFF);
        fillStadium(x, y, chipW, kCardChipH, botCol);
        fillStadium(x + w(0.8f), y + 0.5f, chipW - w(1.6f), kCardChipH * 0.58f, topCol);
        fillStadium(x + chipW * 0.18f, y + 1.5f, chipW * 0.64f, 2.5f, 0x8EF5EA24);
        gfx::text(x + pad, textYForCap(y + (kCardChipH - kCardChipCap) * 0.5f, scale), label, txtCol, sx, scale);
        return x + chipW;
    }

    static void drawDetailsCard()
    {
        float left = cx(kCardLeft);
        float right = cx(kCardRight);
        float width = right - left;
        float height = kCardBottom - kCardTop;
        float bOut = w(2.0f);
        float bIn = w(2.0f);

        // Brawl double-framed metallic border: 2px black outer + 2px P+ teal inner.
        gfx::fillRect(left, kCardTop, width, 2.0f, kLabBorderColor);
        gfx::fillRect(left, kCardBottom - 2.0f, width, 2.0f, kLabBorderColor);
        gfx::fillRect(left, kCardTop + 2.0f, bOut, height - 4.0f, kLabBorderColor);
        gfx::fillRect(right - bOut, kCardTop + 2.0f, bOut, height - 4.0f, kLabBorderColor);

        float il = left + bOut;
        float iw = width - 2.0f * bOut;
        float it = kCardTop + 2.0f;
        float ih = height - 4.0f;
        gfx::fillRect(il, it, iw, 2.0f, kLabAccent);
        gfx::fillRect(il, kCardBottom - 4.0f, iw, 2.0f, kLabAccent);
        gfx::fillRect(il, it + 2.0f, bIn, ih - 4.0f, kLabAccent);
        gfx::fillRect(il + iw - bIn, it + 2.0f, bIn, ih - 4.0f, kLabAccent);

        gfx::fillGradV(il + bIn, it + 2.0f, iw - 2.0f * bIn, ih - 4.0f, kCardFillTop, kCardFillBot);

        const LauncherRow& row = current();
        float contentX = cx(kCardLeft + kCardPadX);
        float contentW = w((kCardRight - kCardLeft) - 2.0f * kCardPadX);

        float titleCap = kCardTitleCap;
        float titleScale = displayScaleForCap(titleCap);
        float tw = gfx::displayTextWidth(row.name, titleScale);
        float tsx = titleScale;
        if (tw > contentW && tw > 0.0f)
        {
            float ratio = contentW / tw;
            if (ratio < 0.88f)
            {
                float uniform = ratio / 0.88f;
                titleCap *= uniform;
                titleScale *= uniform;
                tsx = titleScale * 0.88f;
            }
            else
            {
                tsx = titleScale * ratio;
            }
        }
        gfx::displayText(contentX, displayYForCap(120.0f + (kCardTitleCap - titleCap) * 0.5f, titleScale), row.name,
                         kLabTitleTop, kLabTitleBot, tsx, titleScale, 1.2f, kLabTitleEdge);

        gfx::fillRect(contentX, 148.0f, contentW, 2.0f, kRowEdge);

        float bodyScale = scaleForCap(kCardBodyCap);
        for (int i = 0; i < 3; i++)
        {
            const char* line = row.desc[i];
            if (line == NULL || line[0] == '\0')
            {
                continue;
            }
            float lw = gfx::textWidth(line, bodyScale);
            float lsx = lw > contentW && lw > 0.0f ? bodyScale * contentW / lw : bodyScale;
            gfx::text(contentX, textYForCap(164.0f + i * 18.0f, bodyScale), line, kCardBody, lsx, bodyScale);
        }

        float metaScale = scaleForCap(kCardMetaCap);
        gfx::text(contentX, textYForCap(242.0f, metaScale), "Setup", kCardMuted, metaScale);

        float chipScale = scaleForCap(kCardChipCap);
        float wA = gfx::textWidth(row.chipA, chipScale) + 2.0f * w(kCardChipPadX);
        float wB = gfx::textWidth(row.chipB, chipScale) + 2.0f * w(kCardChipPadX);
        float gap = w(kCardChipGap);
        float captionY = 302.0f;
        bool highlightChipA = (row.kind == kRowSkillDrill && row.targetId == drills::kDrillSnakeRecovery);
        if (wA + gap + wB <= contentW)
        {
            float chipX = drawChip(contentX, 260.0f, row.chipA, contentW, highlightChipA);
            drawChip(chipX + gap, 260.0f, row.chipB, contentX + contentW - (chipX + gap));
        }
        else
        {
            drawChip(contentX, 256.0f, row.chipA, contentW, highlightChipA);
            drawChip(contentX, 286.0f, row.chipB, contentW);
            captionY = 318.0f;
        }

        if (row.caption != NULL && row.caption[0] != '\0')
        {
            float capScale = scaleForCap(kCardChipCap);
            float cw = gfx::textWidth(row.caption, capScale);
            float csx = cw > contentW && cw > 0.0f ? capScale * contentW / cw : capScale;
            gfx::text(contentX, textYForCap(captionY, capScale), row.caption, kCardMuted, csx, capScale);
        }

        if (row.kind == kRowSkillDrill && row.targetId > 0 && row.targetId < drills::kBestSlots)
        {
            u16 best = session().best[row.targetId];
            char bestText[20];
            if (best == 0)
            {
                snprintf(bestText, sizeof(bestText), "Best: --");
            }
            else
            {
                snprintf(bestText, sizeof(bestText), "Best: %u", static_cast<unsigned>(best));
            }
            float barY = captionY + 24.0f;
            float barH = 26.0f;
            drawBevelBar(contentX, barY, contentW, barH, kRowFillTop, kRowFillBot, kRowSheen, kLabBevelDark);
            float bScale = scaleForCap(kCardChipCap);
            float textY = textYForCap(barY + (barH - kCardChipCap) * 0.5f, bScale);
            gfx::text(contentX + w(10.0f), textY, "Best Streak", kCardMuted, bScale);
            float bw = boldWidth(bestText, bScale);
            boldText(contentX + contentW - w(10.0f) - bw, textY, bestText, best > 0 ? kLabFocusGold : kCardMuted,
                     bScale, bScale);
        }

        if (s_starting)
        {
            float startScale = scaleForCap(kRowCap);
            boldText(contentX, textYForCap(368.0f, startScale), "Starting match...", kLabFocusGold, startScale,
                     startScale);
        }
    }

    static float drawControlBadge(float x, const char* glyph, bool round, gfx::Rgba topCol, gfx::Rgba botCol,
                                  gfx::Rgba fg, const char* label)
    {
        float glyphScale = scaleForCap(kBadgeCap);
        float labelScale = scaleForCap(kBadgeLabelCap);
        float gw = gfx::textWidth(glyph, glyphScale);
        float badgeW = round ? w(kBadgeH) : (gw + w(10.0f));
        float minW = w(kBadgeH);
        if (badgeW < minW)
        {
            badgeW = minW;
        }
        float badgeY = kStripCenterY - kBadgeH * 0.5f;
        if (round)
        {
            drawGlossyRoundBadge(x + badgeW * 0.5f, kStripCenterY, kBadgeH * 0.5f, topCol, botCol);
        }
        else
        {
            drawGlossyPillBadge(x, badgeY, badgeW, kBadgeH, topCol, botCol);
        }
        gfx::text(x + (badgeW - gw) * 0.5f, textYForCap(kStripCenterY - kBadgeCap * 0.5f, glyphScale), glyph, fg,
                  glyphScale);
        float lx = x + badgeW + w(6.0f);
        float endX = gfx::text(lx, textYForCap(kStripCenterY - kBadgeLabelCap * 0.5f, labelScale), label, kText,
                               labelScale);
        return endX + w(15.0f);
    }

    static void drawFooterStrip(u32 type)
    {
        gfx::fillGradV(0.0f, kStripTop, 640.0f, kStripH, kLabFooterTopCol, kLabFooterBotCol);
        gfx::fillRect(0.0f, kStripTop, 640.0f, 2.0f, kRowEdge);

        const char* confirmGlyph = type == gfPadType::WIIMOTE ? "2" : "A";
        const char* backGlyph = type == gfPadType::WIIMOTE ? "1" : "B";
        const char* recGlyph = type == gfPadType::WIIMOTE ? "A" : (type == gfPadType::NUNCHUK ? "C" : "X");
        const char* osdGlyph = type == gfPadType::WIIMOTE ? "-+A" : (type == gfPadType::NUNCHUK ? "Z" : "Y");
        const char* sectionGlyph = (type == gfPadType::WIIMOTE || type == gfPadType::NUNCHUK) ? "-/+" : "L/R";
        bool recRound = type == gfPadType::WIIMOTE;
        bool osdRound = type == gfPadType::NUNCHUK;

        float x = cx(kColLeft);
        x = drawControlBadge(x, confirmGlyph, true, kLabBtnATop, kLabBtnABot, kCardTitle, "Select");
        x = drawControlBadge(x, backGlyph, true, kLabBtnBTop, kLabBtnBBot, kCardTitle, "Back");
        x = drawControlBadge(x, recGlyph, recRound, kBadgeImportTop, kBadgeImportBot, kLabBtnDarkText, "Recordings");
        x = drawControlBadge(x, osdGlyph, osdRound, kBadgeOsdTop, kBadgeOsdBot, kLabBtnDarkText, "OSDs");
        drawControlBadge(x, sectionGlyph, false, kLabBtnLightTop, kLabBtnLightBot, kLabBtnDarkText, "Section");
    }

    static void draw()
    {
        float scale = scaleForCap(kRowCap);
        if (gfx::lineHeight(scale) <= 0.0f)
        {
            return;
        }
        u32 type = activeType();
        gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, kBackdropDim);
        drawBanner(type);
        drawLeftColumn();
        drawDetailsCard();
        drawFooterStrip(type);
    }

    static u32 preferredPadType()
    {
        for (int i = 0; i < 4; i++)
        {
            if (s_sys[i].connected)
            {
                return gfPadType::GCC;
            }
        }
        if (s_owner >= 0 && s_owner < kPorts && s_sys[s_owner].connected)
        {
            return s_sys[s_owner].type;
        }
        for (int i = 4; i < kPorts; i++)
        {
            if (s_sys[i].connected)
            {
                return s_sys[i].type;
            }
        }
        return gfPadType::GCC;
    }

    const char* reopenHint()
    {
        return game::padMap(preferredPadType()).reopenHint;
    }

    static const char* activeModeTag()
    {
        u8 sc = session().pendingScenario;
        if (sc > 0 && sc < extra::kScenarioCount)
        {
            static const char* const kScenTags[extra::kScenarioCount] = {
                "TRAINING LAB", "COMBO LAB", "TECHCHASE", "EDGEGUARD", "SHIELD PRESSURE",
                "SHIELD DROP",  "LEDGETECH", "SLIDE-OFF", "GRAB DEFENSE", "MULTI-HIT SDI"
            };
            return kScenTags[sc];
        }
        u8 dr = session().drill;
        if (dr >= drills::kDrillLedgedash && dr < drills::kDrillIdCount)
        {
            static const char* const kDrillTags[drills::kDrillIdCount] = {
                "TRAINING LAB",      "LEDGEDASH",         "WAVEDASH",      "L-CANCEL",
                "FLOAT / HOVER",     "POWERSHIELD",       "RAR",           "DACUS",
                "B-REVERSE",         "CANCEL SPRINT",     "MOVEMENT FLOW", "REACTION LAB",
                "TECHCHASE ESCAPE",  "RECOVERY SWEETSPOT","FLOORHUG TECH", "SNAKE RECOVERY",
                "TRAINING LAB",      "COMBO-BREAK RUSH"
            };
            return kDrillTags[dr];
        }
        return "TRAINING LAB";
    }

    // CRT-safe Brawl/P+ metallic CSS mode pill in the dark teal band (y 58..82)
    // above the character grid (y >= 89): double-framed dark chrome, metallic
    // bevel, zero generic left-accent bars.
    static void drawPending()
    {
        float scale = scaleForCap(8.5f);
        if (gfx::lineHeight(scale) <= 0.0f)
        {
            return;
        }
        const game::PadMap& map = game::padMap(preferredPadType());
        const char* mode = activeModeTag();
        char rightTxt[32];
        snprintf(rightTxt, sizeof(rightTxt), "%s / %s: Modes", map.backName, map.reopenName);

        float modeW = boldWidth(mode, scale);
        float sepW = boldWidth(" | ", scale);
        float hintW = boldWidth(rightTxt, scale);
        float padX = w(10.0f);
        float pillW = modeW + sepW + hintW + 2.0f * padX;
        float pillH = 22.0f;
        float topY = 58.0f;
        float rightX = rx(604.0f);
        float leftX = rightX - pillW;
        float bx = w(2.0f);

        // Double-framed dark chrome + P+ teal inner frame + metallic bevel fill.
        gfx::fillRect(leftX, topY, pillW, pillH, kLabBorderColor);
        gfx::fillRect(leftX + bx, topY + 2.0f, pillW - 2.0f * bx, pillH - 4.0f, kLabAccent);
        drawBevelBar(leftX + 2.0f * bx, topY + 4.0f, pillW - 4.0f * bx, pillH - 8.0f, kLabHeaderTop, kLabHeaderBot,
                     kLabBevelLight, kLabBevelDark);

        float ty = textYForCap(topY + (pillH - 8.5f) * 0.5f, scale);
        float x = leftX + padX;
        boldText(x, ty, mode, kLabFocusGold, scale, scale);
        x += modeW;
        boldText(x, ty, " | ", kLabMutedText, scale, scale);
        x += sepW;
        boldText(x, ty, rightTxt, kText, scale, scale);
    }

    void onRender(u32)
    {
        browser::update();
        osdQuick::update();
        select::update();
        if (s_exiting)
        {
            return;
        }
        if (!s_open && !s_starting)
        {
            if (training::selectRunning())
            {
                gfx::begin();
                drawPending();
                gfx::end();
            }
            return;
        }
        gfx::begin();
        if (browser::isOpen())
        {
            browser::draw();
        }
        else if (osdQuick::isOpen())
        {
            osdQuick::draw();
        }
        else
        {
            draw();
        }
        gfx::end();
    }

    void attach()
    {
        gfx::setWidescreen(training::widescreen());
        gfx::forgetWidths();
        s_open = false;
        s_starting = false;
        s_rebuilding = false;
        s_exiting = false;
        s_fresh = true;
        s_test.magic = kTestMagic;
        s_test.request = 0;
        s_test.result = 0;
        s_test.p1Kind = 0x00;
        s_test.p1Icon = 0x00;
        s_test.cpuKind = 0x07;
        s_test.cpuIcon = 0x07;
        s_test.stage = 0x02;
        s_test.port = 0;
        s_test.reserved = 0;
        TM_LOG("drills: test block at %08x\n", &s_test);
        browser::attach();
        osdQuick::attach();
        for (int i = 0; i < kPorts; i++)
        {
            s_sys[i].held = 0;
            s_sys[i].pressed = 0;
            s_sys[i].type = 0;
            s_sys[i].stickX = 0;
            s_sys[i].stickY = 0;
            s_sys[i].connected = false;
            s_releaseMask[i] = 0;
            s_stickWait[i] = false;
            s_waitRelease[i] = false;
        }
        bool fromDrill = session().drillRan != 0;
        bool fromMatch = session().matchRan != 0;
        if (fromDrill || fromMatch)
        {
            session().drill = drills::kDrillNone;
            session().pendingScenario = 0;
        }
        session().drillRan = 0;
        session().matchRan = 0;
        if (!session().labChosen || fromDrill || fromMatch)
        {
            int port = training::controllingPort();
            if (port >= 0)
            {
                open(port, "auto");
            }
            else
            {
                TM_LOG("drills: list stays closed (no controlling pad)\n");
            }
        }
        else
        {
            TM_LOG("drills: list stays closed (Training Lab chosen)\n");
        }
        select::attach(drills::kDrillNone);
    }

    void detach()
    {
        int code = training::exitCode();
        select::detach(code == 1 && !s_starting);
        if (!s_starting && code == 0)
        {
            session().labChosen = 0;
            session().drill = drills::kDrillNone;
            session().pendingScenario = 0;
            session().matchRan = 0;
        }
        osdQuick::detach();
        browser::detach();
        TM_LOG("drills: detached, exit code %d\n", code);
        s_open = false;
        s_starting = false;
        s_rebuilding = false;
        s_exiting = false;
    }
}
}
}
