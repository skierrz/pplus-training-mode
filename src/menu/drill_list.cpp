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
#include "shared/version.h"

#ifndef TM_STAGE4C
#define TM_STAGE4C 1
#endif
#ifndef TM_BOOT_CANCEL_ENDURANCE
#define TM_BOOT_CANCEL_ENDURANCE 0
#endif
#ifndef TM_BOOT_EXTRA_CHALLENGES
#define TM_BOOT_EXTRA_CHALLENGES 0
#endif

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

    // Row indices referenced by section tables below (must stay 1:1 with kAllRows).
    enum {
        kIdxLab = 0,
        kIdxLCancel,
        kIdxWavedash,
        kIdxLedgedash,
        kIdxComboLab,
        kIdxTechchase,
        kIdxEdgeguard,
        kIdxShieldPressure,
        kIdxPowershield,
        kIdxShieldDrop,
        kIdxLedgetech,
        kIdxSlideOff,
        kIdxGrabDefense,
        kIdxMultiHitSdi,
        kIdxRar,
        kIdxDacus,
        kIdxBReverse,
        kIdxFloatHover,
#if TM_STAGE4C
        kIdxFloorhug,
#endif
        kIdxComboBreakRush
#if TM_STAGE4C
        , kIdxSnakeRecovery
#endif
#if TM_BOOT_CANCEL_ENDURANCE
        , kIdxCancelSprint
#endif
#if TM_BOOT_EXTRA_CHALLENGES
        , kIdxFlowCourse
        , kIdxReactionLab
        , kIdxTechchaseEscape
        , kIdxRecoverySweetspot
#endif
    };

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
            "Press Start in-match for Lab Menu"
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
            "In-match L + D-Up: Free / 10 / 20-Rep"
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
            "In-match L + D-Up: Free / 10 / 20-Rep"
        },
        {
            "Ledgedash",
            kRowSkillDrill,
            drills::kDrillLedgedash,
            {
                "GALINT = invincible frames left",
                "once actionable onstage. Tracks",
                "Tourney Winners & 5-grab limit."
            },
            "Any Fighter",
            "Free Practice",
            "In-match L+D-Up: Mode | D-Right: Side"
        },
        {
            "Combo Lab",
            kRowScenario,
            extra::kScenarioComboLab,
            {
                "Practice follow-ups with random",
                "DI (Directional Influence), techs,",
                "and P+ AI recovery auto-resets."
            },
            "Any Fighter",
            "Any Stage",
            "Press Start in-match to configure"
        },
        {
            "Techchase",
            kRowScenario,
            extra::kScenarioTechchase,
            {
                "React to random tech rolls and",
                "getup options after knockdowns.",
                "Tracks cover streak & reaction."
            },
            "Any Fighter",
            "Any Stage",
            "Press Start in-match to configure"
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
            "Press Start in-match to configure"
        },
        {
            "Shield Pressure / OoS",
            kRowScenario,
            extra::kScenarioShieldPressure,
            {
                "Attack a shielding CPU armed",
                "with OoS counters, or defend",
                "via Role: Offense / Defense."
            },
            "Any Fighter",
            "Offense / Defense",
            "Press Start in-match to configure"
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
            "In-match L + D-Up: Free / 10 / 20-Rep"
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
            "Press Start in-match to configure"
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
            "Press Start in-match to configure"
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
            "Press Start in-match to configure"
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
            "Press Start in-match to configure"
        },
        {
            "P+ Multi-Hit SDI",
            kRowScenario,
            extra::kScenarioMultiHitSdi,
            {
                "CPU repeats multi-hit attacks so",
                "you can drill Smash DI (SDI)",
                "escapes. Switch moves via Start."
            },
            "Any Fighter",
            "Any Stage",
            "Press Start in-match to configure"
        },
        {
            "RAR",
            kRowSkillDrill,
            drills::kDrillRar,
            {
                "Reverse Aerial Rush: turnaround",
                "dash jump into frame-1 back air",
                "retaining forward dash momentum."
            },
            "Any Fighter",
            "Free Practice",
            "In-match L + D-Up: Free / 10 / 20-Rep"
        },
        {
            "DACUS",
            kRowSkillDrill,
            drills::kDrillDacus,
            {
                "Dash Attack Cancelled Up Smash:",
                "cancel dash attack frames 2-3,",
                "including hitlag Gatling timing."
            },
            "Any Fighter",
            "Free Practice",
            "In-match L + D-Up: Free / 10 / 20-Rep"
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
            "In-match L + D-Up: Free / 10 / 20-Rep"
        },
        {
            "Float / Hover & DJC",
            kRowSkillDrill,
            drills::kDrillHoverFloat,
            {
                "Practice Peach & Ganon Floats,",
                "Mewtwo Hovers, and Double-Jump",
                "Cancels (DJC)."
            },
            "6 Float / DJC Cast",
            "Free Practice",
            "In-match L + D-Up: Free / 10 / 20-Rep"
        },
#if TM_STAGE4C
        {
            "CC / ASDI-Down",
            kRowSkillDrill,
            drills::kDrillFloorhug,
            {
                "Crouch Cancel (CC) & Auto Smash",
                "DI Down (ASDI-Down): hold down",
                "on grounded hits to counter fast."
            },
            "Any Fighter",
            "Free / Set / 60s",
            "In-match L + D-Up: Free / 10 / 20 / 60s"
        },
#endif
        {
            "Break the Targets!",
            kRowSkillDrill,
            drills::kDrillComboBreakRush,
            {
                "Break Brawl targets across the",
                "stage for x1-x8 chains in Timed",
                "or Free mode."
            },
            "Any Fighter",
            "Brawl Targets",
            "In-match L + D-Up: Free / 30s / 60s"
        }
#if TM_STAGE4C
        , {
            "Snake C4 Recovery",
            kRowSkillDrill,
            drills::kDrillSnakeRecovery,
            {
                "Practice Snake's infinite Cypher",
                "and C4 self-hit recovery chains",
                "back to ledge from deep offstage."
            },
            "Snake Only",
            "Free / Set / 60s",
            "In-match L + D-Up: Free / 10 / 20 / 60s"
        }
#endif
#if TM_BOOT_CANCEL_ENDURANCE
        , {
            "Cancel Practice: Endurance",
            kRowSkillDrill,
            drills::kDrillCancelSprint,
            {
                "Chain fighter-specific jump,",
                "hover, or special cancels",
                "with tight frame-delta scoring."
            },
            "Cancel Fighters",
            "Free / Set / 60s",
            "In-match L + D-Up: Free / 10 / 20 / 60s"
        }
#endif
#if TM_BOOT_EXTRA_CHALLENGES
        , {
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
            "In-match L + D-Up: Free / 10 / 20 / 60s"
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
            "In-match L + D-Up: Free / 10 / 20 / 60s"
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
            "In-match L + D-Up: Free / 10 / 20 / 60s"
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
            "In-match L + D-Up: Free / 10 / 20 / 60s"
        }
#endif
    };

    // Locked 1.0 Boot Menu Architecture: 4 clean tabs, zero scrolling on default landing.
    static const u8 kQuickStartIndices[] = {
        kIdxLab, kIdxComboLab, kIdxTechchase, kIdxLCancel, kIdxWavedash, kIdxLedgedash
    };
    static const u8 kScenariosIndices[] = {
        kIdxComboLab,  kIdxTechchase,   kIdxEdgeguard, kIdxShieldPressure, kIdxShieldDrop,
        kIdxLedgetech, kIdxSlideOff,    kIdxGrabDefense, kIdxMultiHitSdi
#if TM_STAGE4C
        , kIdxFloorhug
#endif
    };
    static const u8 kTechPracticeIndices[] = {
        kIdxLCancel, kIdxWavedash, kIdxLedgedash, kIdxPowershield,
        kIdxRar,     kIdxDacus,    kIdxBReverse
    };
    static const u8 kMinigamesTechIndices[] = {
        kIdxComboBreakRush,
        kIdxFloatHover
#if TM_STAGE4C
        , kIdxSnakeRecovery
#endif
#if TM_BOOT_CANCEL_ENDURANCE
        , kIdxCancelSprint
#endif
#if TM_BOOT_EXTRA_CHALLENGES
        , kIdxFlowCourse
        , kIdxReactionLab
        , kIdxTechchaseEscape
        , kIdxRecoverySweetspot
#endif
    };

    struct LauncherSection {
        const char* name;
        const u8* indices;
        int count;
    };

    static const LauncherSection kSections[] = {
        {"Quick Start", kQuickStartIndices, static_cast<int>(sizeof(kQuickStartIndices) / sizeof(kQuickStartIndices[0]))},
        {"Scenarios", kScenariosIndices, static_cast<int>(sizeof(kScenariosIndices) / sizeof(kScenariosIndices[0]))},
        {"Tech Practice", kTechPracticeIndices,
         static_cast<int>(sizeof(kTechPracticeIndices) / sizeof(kTechPracticeIndices[0]))},
        {"Minigames & Tech", kMinigamesTechIndices,
         static_cast<int>(sizeof(kMinigamesTechIndices) / sizeof(kMinigamesTechIndices[0]))}
    };
    enum { kSectionCount = static_cast<int>(sizeof(kSections) / sizeof(kSections[0])) };

    static const LauncherRow& rowAt(const LauncherSection& sec, int idx)
    {
        return kAllRows[sec.indices[idx]];
    }

    using namespace ui::style;

    // ---- Boot menu palette (darker charcoal scrim covers CSS Max Combos cleanly) ----
    static const gfx::Rgba kBackdropDim = 0x05080DF4u;
    static const gfx::Rgba kBannerTopCol = kLabTealTop;
    static const gfx::Rgba kBannerBotCol = kLabTealBot;
    static const gfx::Rgba kBannerSheen = kLabTealSheen;
    static const gfx::Rgba kBannerInk = kLabActiveTabText;
    static const gfx::Rgba kRowFillTop = kLabRowSlateTop;
    static const gfx::Rgba kRowFillBot = kLabRowSlateBot;
    static const gfx::Rgba kRowEdge = kLabRowEdge;
    static const gfx::Rgba kCardFillTop = kLabCardTop;
    static const gfx::Rgba kCardFillBot = kLabCardBot;
    static const gfx::Rgba kCardBorderIn = kLabCardBorderIn;
    static const gfx::Rgba kCardTitle = kLabWhiteText;
    static const gfx::Rgba kCardBody = kLabDescText;
    static const gfx::Rgba kCardMuted = kLabMutedText;
    static const gfx::Rgba kBadgeImportTop = kLabBadgeImportTop;
    static const gfx::Rgba kBadgeImportBot = kLabBadgeImportBot;
    static const gfx::Rgba kBadgeOsdTop = kLabBadgeOsdTop;
    static const gfx::Rgba kBadgeOsdBot = kLabBadgeOsdBot;

    // ---- Boot menu geometry (640x480 2D space; Golden Ratio Option G1: 220:324 in 36..604) ----
    static const float kSlantTan = 0.445f; // ~24 deg stock P+ slant
    static const float kBannerTop = 26.0f;
    static const float kBannerBottom = 60.0f; // H = kFibBandH (34 = F9)
    static const float kBannerRuleH = kFibHair;
    static const float kBannerLeftTop = 166.0f;
    static const float kBannerLeftBot = 151.0f;
    static const float kBannerRightTop = 604.0f;
    static const float kBannerRightBot = 589.0f;
    static const float kBannerTitleX = 190.0f;
    static const float kBannerTitleCap = kTypeBody;
    static const float kBannerPillLeftX = 330.0f;
    static const float kBannerPillW = 22.0f;
    static const float kBannerPillH = 18.0f;
    static const float kBannerPillCap = kTypeMeta;
    static const float kBannerSecSlotL = 356.0f;
    static const float kBannerSecSlotR = 524.0f;
    static const float kBannerSecCap = kTypeMeta;
    static const float kBannerPillRightX = 528.0f;

    static const float kColLeft = 36.0f;
    static const float kColBarRight = 256.0f;   // W = 220
    static const float kColScrollLeft = 259.0f; // 3 px (F4) gap from bar
    static const float kColScrollW = kFibGap;   // 8 px (F6) track (259..267)
    static const float kColRight = 267.0f;
    static const float kColTop = 94.0f;
    static const float kColRowH = 26.0f;
    static const float kColPitch = 31.0f;       // kColRowH (26) + kFibTight (5)
    static const float kColTextX = 58.0f;       // clears red cursor triangle cleanly (04-mockup)

    static const float kCardLeft = 280.0f;      // 13 px (F7) gutter from scrollbar (267..280)
    static const float kCardRight = 604.0f;     // W = 324 (324 / 220 = 1.473)
    static const float kCardTop = 94.0f;
    static const float kCardBottom = 399.0f;    // flush with 10-row list bottom (94 + 9*31 + 26)
    static const float kCardBorder = kLabBorder;
    static const float kCardPadX = 16.0f;       // border (3) + kFibPad (13) -> contentW = 292
    static const float kCardTitleCap = kTypeHeading;
    static const float kCardBodyCap = kTypeBody;
    static const float kCardMetaCap = kTypeMeta;
    static const float kCardChipCap = kTypeMeta;

    static const float kStripTop = 422.0f;
    static const float kStripH = 58.0f;         // background extends to bottom; content inside 422..456 (SMPTE)
    static const float kStripCenterY = 439.0f;  // centered in 422..456 (H = 34 = F9)
    static const float kBadgeH = 18.0f;
    static const float kBadgeCap = kTypeMeta;
    static const float kBadgeLabelCap = kTypeMeta;

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

    // Budgeted 20-byte boot menu motion state (<= 24 B): 9f open + 6f stagger,
    // 5f non-blocking exit -> 6f CSS pill settle, chase60 cursor & scroll, 7f L/R slide.
    struct ListAnim {
        float barY;
        float slideX;
        float scrollY;
        u8 openTick;
        u8 closeTick;
        u8 pillTick;
        u8 modalExitTick;
        u8 valid;
        u8 hasScroll;
        u8 pad[2];
    };
    static ListAnim s_anim;

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
        s_anim.openTick = 9;
        s_anim.closeTick = 0;
        s_anim.slideX = 0.0f;
        s_anim.valid = 0;
        s_anim.hasScroll = 0;
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
        s_anim.openTick = 0;
        s_anim.closeTick = 5;
        s_anim.pillTick = 6;
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
        s_anim.slideX = delta > 0 ? 12.0f : -12.0f;
        s_anim.valid = 0;
        s_anim.hasScroll = 0;
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
                s_anim.modalExitTick = 4;
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
                s_anim.modalExitTick = 4;
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

    static void drawBanner(u32 type)
    {
        float bLeftTop = cx(kBannerLeftTop);
        float bLeftBot = cx(kBannerLeftBot);
        float bRightTop = cx(kBannerRightTop);
        float bRightBot = cx(kBannerRightBot);
        float slantPx = bRightTop - bRightBot;

        // Crisp dark outline + emerald-teal slanted ribbon seated right of CSS BACK & TRAINING.
        drawSlantGradV(bLeftTop - w(2.0f), bLeftBot - w(2.0f), bRightTop + w(2.0f), bRightBot + w(2.0f),
                       kBannerTop - 2.0f, kBannerBottom + kBannerRuleH, kLabBorderColor, kLabBorderColor);
        drawSlantGradV(bLeftTop, bLeftBot, bRightTop, bRightBot, kBannerTop, kBannerBottom, kBannerTopCol,
                       kBannerBotCol);
        drawSlantGradV(bLeftTop, bLeftTop - w(0.9f), bRightTop, bRightTop - w(0.9f), kBannerTop, kBannerTop + 2.0f,
                       kBannerSheen, kBannerSheen);
        drawDiagStripes(bRightTop - w(12.0f), kBannerTop, kBannerBottom, slantPx, w(5.0f), w(4.0f), kLabStripeCol);

        float midY = (kBannerTop + kBannerBottom) * 0.5f;
        float titleScale = displayScaleForCap(kBannerTitleCap);
        float titleMaxW = w(114.0f);
        float titleW = gfx::displayTextWidth("Training Mode", titleScale);
        float titleSx = titleW > titleMaxW && titleW > 0.0f ? titleScale * titleMaxW / titleW : titleScale;
        gfx::displayText(cx(kBannerTitleX), displayYForCap(midY - kBannerTitleCap * 0.5f, titleScale), "Training Mode",
                         kBannerInk, kBannerInk, titleSx, titleScale, 0.0f, 0);

        bool wiiShoulder = type == gfPadType::WIIMOTE || type == gfPadType::NUNCHUK;
        const char* prevGlyph = wiiShoulder ? "-" : "L";
        const char* nextGlyph = wiiShoulder ? "+" : "R";

        float pillScale = scaleForCap(kBannerPillCap);
        float pillY = midY - kBannerPillH * 0.5f;
        float pillW = gw(kBannerPillW);
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
        char secCount[8];
        snprintf(secCount, sizeof(secCount), "(%d/%d)", s_page + 1, kSectionCount);
        float secScale = displayScaleForCap(kBannerSecCap);
        float cntScale = scaleForCap(kTypeMeta);
        float secW = gfx::displayTextWidth(secName, secScale);
        float cntW = gfx::textWidth(secCount, cntScale);
        float gapPx = w(kFibPad);
        float totalW = secW + gapPx + cntW;
        float slotCenter = (cx(kBannerSecSlotL) + cx(kBannerSecSlotR)) * 0.5f;
        float slotLimit = cx(kBannerSecSlotR) - cx(kBannerSecSlotL);
        float availText = slotLimit - gapPx;
        float textSum = secW + cntW;
        float ratio = totalW > slotLimit && textSum > 0.0f ? availText / textSum : 1.0f;
        float drawSecW = secW * ratio;
        float drawCntW = cntW * ratio;
        float startX = slotCenter - (drawSecW + gapPx + drawCntW) * 0.5f;
        gfx::displayText(startX, displayYForCap(midY - kBannerSecCap * 0.5f, secScale), secName, kBannerInk,
                         kBannerInk, secScale * ratio, secScale, 0.0f, 0);
        gfx::text(startX + drawSecW + gapPx, textYForCap(midY - kTypeMeta * 0.5f, cntScale), secCount, kBannerInk,
                  cntScale * ratio, cntScale);

        // Version mark right-aligned in the dark band directly above the details card.
        float verScale = scaleForCap(kTypeMicro);
        float verW = boldWidth(TM_VERSION_MARK, verScale);
        boldText(cx(kCardRight) - verW, textYForCap(70.0f, verScale), TM_VERSION_MARK, kCardMuted, verScale, verScale);
    }

    static void drawLeftColLabels(const LauncherSection& sec, int drawStart, int drawEnd, float drawScrollY,
                                  const float* rowOff, float slidePx, float textX, float textMaxW, float scale,
                                  float capOffset, int passMode, float lensTop, float lensBot)
    {
        for (int idx = drawStart; idx < drawEnd; idx++)
        {
            float y = kColTop + static_cast<float>(idx) * kColPitch - drawScrollY;
            if (passMode == 2 && (y + kColRowH <= lensTop || y >= lensBot))
            {
                continue;
            }
            bool selected = passMode == 2 ? true : (passMode == 0 && idx == s_row);
            float rxOff = (passMode == 2 && s_anim.openTick == 0) ? slidePx : rowOff[idx - drawStart];
            const char* name = rowAt(sec, idx).name;
            float nw = boldWidth(name, scale);
            float sx = nw > textMaxW && nw > 0.0f ? scale * textMaxW / nw : scale;
            boldText(textX + rxOff, textYForCap(y + capOffset, scale), name, selected ? kLabGoldInk : kText, sx, scale);
        }
    }

    static void drawLeftColumn()
    {
        const LauncherSection& sec = currentSection();
        float scale = scaleForCap(kRowCap);
        float capOffset = (kColRowH - kRowCap) * 0.5f;
        float slant = kColRowH * kSlantTan;
        float x0Top = cx(kColLeft + slant);
        float x0Bot = cx(kColLeft);
        float x1Top = cx(kColBarRight);
        float x1Bot = cx(kColBarRight - slant);
        float padX = w(kFibHair);
        float dySlant = w(kFibHair * kSlantTan);
        float ix0Top = x0Top - dySlant + padX;
        float ix0Bot = x0Bot + dySlant + padX;
        float ix1Top = x1Top - dySlant - padX;
        float ix1Bot = x1Bot + dySlant - padX;
        float inSlantPx = w((kColRowH - 4.0f) * kSlantTan);
        float textX = cx(kColTextX);
        float textMaxW = cx(kColBarRight - 30.0f) - textX;
        float slidePx = w(s_anim.slideX);
        float openElapsed = s_anim.openTick > 0 ? static_cast<float>(9 - s_anim.openTick) : 9.0f;

        float targetScrollY = static_cast<float>(s_scroll) * kColPitch;
        s_anim.scrollY = s_anim.hasScroll ? chase60(s_anim.scrollY, targetScrollY, 0.52f) : targetScrollY;
        s_anim.hasScroll = 1;
        bool scrolling = (s_anim.scrollY < targetScrollY - 0.25f || s_anim.scrollY > targetScrollY + 0.25f);
        float drawScrollY = scrolling ? s_anim.scrollY : targetScrollY;

        int drawStart = s_scroll;
        int drawEnd = s_scroll + kVisibleRows;
        if (scrolling)
        {
            drawStart = static_cast<int>(drawScrollY / kColPitch) - 1;
            if (drawStart < 0)
            {
                drawStart = 0;
            }
            drawEnd = drawStart + kVisibleRows + 3;
        }
        if (drawEnd > sec.count)
        {
            drawEnd = sec.count;
        }
        enum { kMaxBootDraw = kVisibleRows + 4 };
        if (drawEnd - drawStart > kMaxBootDraw)
        {
            drawEnd = drawStart + kMaxBootDraw;
        }

        float rowOff[kMaxBootDraw];
        float selOff = slidePx;
        for (int idx = drawStart; idx < drawEnd; idx++)
        {
            int local = idx - drawStart;
            float staggerT = s_anim.openTick > 0 ? easeOutCubic((openElapsed - local * 0.45f) / 4.5f) : 1.0f;
            rowOff[local] = slidePx - w((1.0f - staggerT) * 10.0f);
            if (idx == s_row)
            {
                selOff = rowOff[local];
            }
        }

        bool hasBar = (s_row >= s_scroll && s_row < s_scroll + kVisibleRows && s_row < sec.count);
        bool barGliding = false;
        float by = 0.0f;
        if (hasBar)
        {
            float targetY = kColTop + (s_row - s_scroll) * kColPitch;
            s_anim.barY = s_anim.valid ? chase60(s_anim.barY, targetY, 0.52f) : targetY;
            s_anim.valid = 1;
            barGliding = (s_anim.barY < targetY - 0.25f || s_anim.barY > targetY + 0.25f);
            by = barGliding ? s_anim.barY : snapPx(targetY);
        }

        bool clipList = scrolling || s_anim.slideX != 0.0f || s_anim.openTick > 0;
        gfx::ScissorState listSc;
        bool listClipped = false;
        bool listVisible = true;
        if (clipList)
        {
            listClipped = gfx::pushScissor(cx(kColLeft - 2.0f), kColTop - 2.0f, cx(kColScrollLeft - 1.0f),
                                           kCardBottom + 2.0f, listSc);
            listVisible = listClipped;
        }

        if (listVisible)
        {
            // Draw unselected dark-slate plates first (capped <= 6f stagger on open, never blocks input).
            for (int idx = drawStart; idx < drawEnd; idx++)
            {
                float rxOff = rowOff[idx - drawStart];
                float y = kColTop + static_cast<float>(idx) * kColPitch - drawScrollY;
                drawSlantGradV(x0Top + rxOff, x0Bot + rxOff, x1Top + rxOff, x1Bot + rxOff, y, y + kColRowH,
                               kLabBorderColor, kLabBorderColor);
                drawSlantGradV(ix0Top + rxOff, ix0Bot + rxOff, ix1Top + rxOff, ix1Bot + rxOff, y + kFibHair,
                               y + kColRowH - kFibHair, kRowFillTop, kRowFillBot);
            }

            float barOff = (barGliding || scrolling) ? slidePx : selOff;
            if (hasBar && (barGliding || scrolling))
            {
                drawLeftColLabels(sec, drawStart, drawEnd, drawScrollY, rowOff, slidePx, textX, textMaxW, scale,
                                  capOffset, 1, 0.0f, 0.0f);
                drawSlantGradV(x0Top + barOff, x0Bot + barOff, x1Top + barOff, x1Bot + barOff, by, by + kColRowH,
                               kLabGoldEdge, kLabGoldEdge);
                drawSlantGradV(ix0Top + barOff, ix0Bot + barOff, ix1Top + barOff, ix1Bot + barOff, by + kFibHair,
                               by + kColRowH - kFibHair, kLabGoldTop, kLabGoldBot);
                drawDiagStripes(ix1Top + barOff - w(kFibTight), by + kFibHair, by + kColRowH - kFibHair, inSlantPx,
                                w(4.5f), w(3.5f), kLabStripeCol);
                drawCursorTri(cx(kColLeft + slant * 0.55f + 1.5f) + barOff, by + kColRowH * 0.5f, 8.0f);
                gfx::ScissorState lensSc;
                if (gfx::pushScissor(0.0f, by, 640.0f, by + kColRowH, lensSc))
                {
                    drawLeftColLabels(sec, drawStart, drawEnd, drawScrollY, rowOff, slidePx, textX, textMaxW, scale,
                                      capOffset, 2, by, by + kColRowH);
                    gfx::popScissor(lensSc);
                }
            }
            else
            {
                if (hasBar)
                {
                    drawSlantGradV(x0Top + barOff, x0Bot + barOff, x1Top + barOff, x1Bot + barOff, by, by + kColRowH,
                                   kLabGoldEdge, kLabGoldEdge);
                    drawSlantGradV(ix0Top + barOff, ix0Bot + barOff, ix1Top + barOff, ix1Bot + barOff, by + kFibHair,
                                   by + kColRowH - kFibHair, kLabGoldTop, kLabGoldBot);
                    drawDiagStripes(ix1Top + barOff - w(kFibTight), by + kFibHair, by + kColRowH - kFibHair, inSlantPx,
                                    w(4.5f), w(3.5f), kLabStripeCol);
                    drawCursorTri(cx(kColLeft + slant * 0.55f + 1.5f) + barOff, by + kColRowH * 0.5f, 8.0f);
                }
                drawLeftColLabels(sec, drawStart, drawEnd, drawScrollY, rowOff, slidePx, textX, textMaxW, scale,
                                  capOffset, 0, 0.0f, 0.0f);
            }
        }
        if (listClipped)
        {
            gfx::popScissor(listSc);
        }

        drawScrollBarFloat(kColScrollLeft, kColTop, (kVisibleRows - 1) * kColPitch + kColRowH, sec.count,
                           drawScrollY / kColPitch, kVisibleRows, kLabDivider, kLabTealTop, kLabTealBot);
    }

    static void drawDetailsCard()
    {
        float left = cx(kCardLeft);
        float right = cx(kCardRight);
        float width = right - left;
        float height = kCardBottom - kCardTop;
        float b = kCardBorder;
        float bx = w(b);

        // Smoked charcoal translucent card with unified 3 px (F4) dark outer frame + 2 px (F3) subtle inner hairline.
        drawFrameBorder(kCardLeft, kCardRight, kCardTop, kCardBottom, b, kLabBorderColor);
        float il = left + bx;
        float iw = width - 2.0f * bx;
        float it = kCardTop + b;
        float ih = height - 2.0f * b;
        gfx::fillGradV(il, it, iw, ih, kCardFillTop, kCardFillBot);
        gfx::fillRect(il, it, iw, kFibHair, kCardBorderIn);
        gfx::fillRect(il, kCardBottom - b - kFibHair, iw, kFibHair, kLabBevelDark);

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
        gfx::displayText(contentX, displayYForCap(114.0f + (kCardTitleCap - titleCap) * 0.5f, titleScale), row.name,
                         kLabTitleTop, kLabTitleBot, tsx, titleScale, 1.2f, kLabTitleEdge);

        gfx::fillRect(contentX, 138.0f, contentW, kFibHair, kRowEdge);

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
            gfx::text(contentX, textYForCap(154.0f + i * 20.0f, bodyScale), line, kCardBody, lsx, bodyScale);
        }

        // Clean flat SETUP metadata text (zero clickable-looking pill boxes).
        float metaScale = scaleForCap(kCardMetaCap);
        gfx::text(contentX, textYForCap(234.0f, metaScale), "Setup", kCardMuted, metaScale);

        float chipScale = scaleForCap(kCardChipCap);
        float wA = gfx::textWidth(row.chipA, chipScale);
        float wB = gfx::textWidth(row.chipB, chipScale);
        float metaGap = w(kFibPad);
        float availMeta = contentW - metaGap;
        float textSum = wA + wB;
        float ratioM = textSum > availMeta && textSum > 0.0f ? availMeta / textSum : 1.0f;
        float msx = chipScale * ratioM;
        bool highlightChipA = (row.kind == kRowSkillDrill
                               && (row.targetId == drills::kDrillSnakeRecovery
                                   || row.targetId == drills::kDrillHoverFloat));
        float metaY = textYForCap(252.0f, chipScale);
        gfx::text(contentX, metaY, row.chipA, highlightChipA ? kLabFocusGold : kCardBody, msx, chipScale);
        float sepX = contentX + wA * ratioM + metaGap * 0.5f - w(1.0f);
        gfx::fillRect(sepX, 251.0f, w(kFibHair), 11.0f, kCardMuted);
        gfx::text(contentX + wA * ratioM + metaGap, metaY, row.chipB, kCardBody, msx, chipScale);

        if (row.caption != NULL && row.caption[0] != '\0')
        {
            float capScale = scaleForCap(kCardChipCap);
            float cw = gfx::textWidth(row.caption, capScale);
            float csx = cw > contentW && cw > 0.0f ? capScale * contentW / cw : capScale;
            gfx::text(contentX, textYForCap(286.0f, capScale), row.caption, kCardMuted, csx, capScale);
        }

        int bestSlot = -1;
        if (row.kind == kRowSkillDrill && row.targetId > 0 && row.targetId < drills::kBestSlots)
        {
            bestSlot = row.targetId;
        }
        else if (row.kind == kRowScenario && (row.targetId & 0x7Fu) == extra::kScenarioTechchase)
        {
            bestSlot = extra::kTechBestSlot;
        }
        if (bestSlot >= 0)
        {
            u16 best = session().best[bestSlot];
            char bestText[16];
            if (best == 0)
            {
                snprintf(bestText, sizeof(bestText), "--");
            }
            else
            {
                snprintf(bestText, sizeof(bestText), "%u", static_cast<unsigned>(best));
            }
            float divY = 330.0f;
            gfx::fillRect(contentX, divY, contentW, kFibHair, kRowEdge);
            float bScale = scaleForCap(kTypeBody);
            float textY = textYForCap(divY + kFibPad, bScale);
            const char* bestLabel =
                (row.kind == kRowSkillDrill && row.targetId == drills::kDrillComboBreakRush) ? "Best Score"
                                                                                             : "Best Streak";
            gfx::text(contentX, textY, bestLabel, kCardBody, bScale);
            float bw = boldWidth(bestText, bScale);
            boldText(contentX + contentW - bw, textY, bestText, best > 0 ? kLabFocusGold : kCardMuted, bScale, bScale);
        }

        if (s_starting)
        {
            float startScale = scaleForCap(kRowCap);
            boldText(contentX, textYForCap(366.0f, startScale), "Starting match...", kLabFocusGold, startScale,
                     startScale);
        }
    }

    static void drawFooterStrip(u32 type)
    {
        gfx::fillGradV(0.0f, kStripTop, 640.0f, kStripH, kLabFooterTopCol, kLabFooterBotCol);
        gfx::fillRect(0.0f, kStripTop, 640.0f, 2.0f, kRowEdge);

        struct BootBadge {
            const char* glyph;
            bool round;
            gfx::Rgba topCol, botCol, fg;
            const char* label;
        };
        const BootBadge items[5] = {
            {type == gfPadType::WIIMOTE ? "2" : "A", true, kLabBtnATop, kLabBtnABot, kLabBtnDarkText, "Select"},
            {type == gfPadType::WIIMOTE ? "1" : "B", true, kLabBtnBTop, kLabBtnBBot, kCardTitle, "Back"},
            {type == gfPadType::WIIMOTE ? "A" : (type == gfPadType::NUNCHUK ? "C" : "X"), true, kBadgeImportTop,
             kBadgeImportBot, kLabBtnDarkText, "Recordings"},
            {type == gfPadType::WIIMOTE ? "-+A" : (type == gfPadType::NUNCHUK ? "Z" : "Y"),
             type != gfPadType::WIIMOTE, kBadgeOsdTop, kBadgeOsdBot, kLabBtnDarkText, "Quick Setup"},
            {(type == gfPadType::WIIMOTE || type == gfPadType::NUNCHUK) ? "-/+" : "L/R", false, kLabBtnLightTop,
             kLabBtnLightBot, kLabBtnDarkText, "Section"}
        };
        float glyphScale = scaleForCap(kBadgeCap);
        float labelScale = scaleForCap(kBadgeLabelCap);
        float posX[5], badgeW[5], glyphW[5];
        float curX = cx(kColLeft);
        for (int i = 0; i < 5; i++)
        {
            float gwPx = gfx::textWidth(items[i].glyph, glyphScale);
            float lwPx = gfx::textWidth(items[i].label, labelScale);
            float bw = items[i].round ? gw(kBadgeH) : (gwPx + gw(10.0f));
            float minW = items[i].round ? gw(kBadgeH) : gw(kBadgeH + 4.0f);
            if (bw < minW) bw = minW;
            posX[i] = curX;
            badgeW[i] = bw;
            glyphW[i] = gwPx;
            curX += bw + gw(kFibTight) + lwPx + gw(kFibPad);
        }
        float badgeY = kStripCenterY - kBadgeH * 0.5f;
        for (int i = 0; i < 5; i++)
        {
            if (items[i].round)
            {
                drawGlossyRoundBadge(posX[i] + badgeW[i] * 0.5f, kStripCenterY, kBadgeH * 0.5f, items[i].topCol,
                                     items[i].botCol);
            }
            else
            {
                drawGlossyPillBadge(posX[i], badgeY, badgeW[i], kBadgeH, items[i].topCol, items[i].botCol);
            }
        }
        float gy = textYForCap(kStripCenterY - kBadgeCap * 0.5f, glyphScale);
        float ly = textYForCap(kStripCenterY - kBadgeLabelCap * 0.5f, labelScale);
        for (int i = 0; i < 5; i++)
        {
            gfx::text(posX[i] + (badgeW[i] - glyphW[i]) * 0.5f, gy, items[i].glyph, items[i].fg, glyphScale);
            gfx::text(posX[i] + badgeW[i] + gw(kFibTight), ly, items[i].label, kText, labelScale);
        }
    }

    static void draw()
    {
        float scale = scaleForCap(kRowCap);
        if (gfx::lineHeight(scale) <= 0.0f)
        {
            return;
        }
        float openA = 1.0f;
        if (s_anim.openTick > 0)
        {
            openA = 0.25f + 0.75f * easeOutCubic(static_cast<float>(10 - s_anim.openTick) / 9.0f);
        }
        if (s_anim.slideX != 0.0f)
        {
            s_anim.slideX = chase60(s_anim.slideX, 0.0f, 0.42f);
        }
        u32 type = activeType();
        gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, scaleAlpha(kBackdropDim, openA));
        drawBanner(type);
        drawLeftColumn();
        drawDetailsCard();
        drawFooterStrip(type);
        if (s_anim.openTick > 0)
        {
            s_anim.openTick--;
        }
        if (s_anim.modalExitTick > 0)
        {
            float mt = easeOutQuart(static_cast<float>(5 - s_anim.modalExitTick) / 4.0f);
            gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, scaleAlpha(kLabModalBackdrop, (1.0f - mt) * 0.6f));
            s_anim.modalExitTick--;
        }
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
        u8 rawSc = session().pendingScenario;
        u8 sc = static_cast<u8>(rawSc & 0x7Fu);
        if (sc > 0 && sc < extra::kScenarioCount)
        {
            if (sc == extra::kScenarioShieldPressure && (rawSc & 0x80u) != 0u)
            {
                return "OOS REVERSAL";
            }
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
                "B-REVERSE",         "CANCEL PRACTICE: ENDURANCE", "MOVEMENT FLOW COURSE", "REACTION LAB",
                "TECHCHASE ESCAPE",  "RECOVERY SWEETSPOT","CC / ASDI-DOWN","SNAKE C4 RECOVERY",
                "TRAINING LAB",      "BREAK THE TARGETS!"
            };
            return kDrillTags[dr];
        }
        return "TRAINING LAB";
    }

    // Direction A smoked-charcoal CSS mode pill in the dark teal band (y 58..82)
    // above the character grid (y >= 89): crisp 2px frame border, flat smoked
    // gradient, kTypeMicro (8.0 px CRT floor) badges, clamped inside 5% safe.
    static void drawPending()
    {
        float scale = scaleForCap(8.5f);
        if (gfx::lineHeight(scale) <= 0.0f)
        {
            return;
        }
        const game::PadMap& map = game::padMap(preferredPadType());
        const char* mode = activeModeTag();
        float bScale = scaleForCap(kTypeMicro);

        float modeW = boldWidth(mode, scale);
        float backGw = gfx::textWidth(map.backName, bScale);
        float backBw = backGw + w(8.0f);
        if (backBw < w(15.0f))
        {
            backBw = w(15.0f);
        }
        float slashW = gfx::textWidth("/", bScale);
        float reopGw = gfx::textWidth(map.reopenName, bScale);
        float reopBw = reopGw + w(8.0f);
        float modesW = boldWidth("Modes", scale);

        float padX = w(10.0f);
        float divGap = w(7.0f);
        float divW = w(2.0f);
        float smGap = w(4.0f);
        float fixedW = padX + divGap + divW + divGap + backBw + smGap + slashW + smGap + reopBw + smGap + modesW
                       + padX;
        float rightX = 604.0f;
        float minLeftX = 172.0f; // clear of CSS BACK & TRAINING chrome and inside 5% CRT safe
        float maxModeW = (rightX - minLeftX) - fixedW;
        float modeDrawW = modeW > maxModeW && maxModeW > 1.0f ? maxModeW : modeW;
        float modeSx = modeW > modeDrawW && modeW > 0.0f ? scale * modeDrawW / modeW : scale;
        float pillW = fixedW + modeDrawW;
        float pillH = 22.0f;
        float pillDy = 0.0f;
        if (s_anim.pillTick > 0)
        {
            float pt = easeOutCubic(static_cast<float>(7 - s_anim.pillTick) / 6.0f);
            pillDy = snapPx((1.0f - pt) * -6.0f);
            s_anim.pillTick--;
        }
        float topY = 58.0f + pillDy;
        float leftX = snapPx(rightX - pillW);
        float bx = w(kFibHair);

        // Flat Direction A smoked plate with crisp 2px border (matching panel borders).
        gfx::fillRect(leftX, topY, pillW, pillH, kLabBorderColor);
        gfx::fillGradV(leftX + bx, topY + kFibHair, pillW - 2.0f * bx, pillH - 2.0f * kFibHair,
                       kLabHeaderTop, kLabHeaderBot);
        gfx::fillRect(leftX + bx, topY + kFibHair, pillW - 2.0f * bx, kFibHair, kLabBevelLight);

        float midY = topY + pillH * 0.5f;
        float ty = textYForCap(midY - 4.25f, scale);
        float bty = textYForCap(midY - kTypeMicro * 0.5f, bScale);
        float badgeH = 14.0f;
        float badgeY = midY - badgeH * 0.5f;

        float x = leftX + padX;
        boldText(x, ty, mode, kLabFocusGold, modeSx, scale);
        x += modeDrawW + divGap;

        gfx::fillRect(x, topY + 5.0f, divW, pillH - 10.0f, kLabDivider);
        x += divW + divGap;

        drawGlossyPillBadge(x, badgeY, backBw, badgeH, kLabBtnBTop, kLabBtnBBot);
        gfx::text(x + (backBw - backGw) * 0.5f, bty, map.backName, kCardTitle, bScale);
        x += backBw + smGap;

        gfx::text(x, bty, "/", kLabMutedText, bScale);
        x += slashW + smGap;

        drawGlossyPillBadge(x, badgeY, reopBw, badgeH, kLabBtnLightTop, kLabBtnLightBot);
        gfx::text(x + (reopBw - reopGw) * 0.5f, bty, map.reopenName, kLabBtnDarkText, bScale);
        x += reopBw + smGap;

        boldText(x, ty, "Modes", kText, scale, scale);
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
                if (s_anim.closeTick > 0)
                {
                    float ct = easeOutQuart(static_cast<float>(6 - s_anim.closeTick) / 5.0f);
                    gfx::fillRect(0.0f, 0.0f, 640.0f, 480.0f, scaleAlpha(kBackdropDim, 1.0f - ct));
                    s_anim.closeTick--;
                }
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
        memset(&s_anim, 0, sizeof(s_anim));
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
