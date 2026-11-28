#include "extra/menu_summaries.h"

#include <cstdio>

#include "extra/olimar_pikmin_logic.h"
#include "main/cpu/actions.h"
#include "main/cpu/view.h"
#include "main/game/fighters.h"
#include "main/record/record.h"
#include "main/settings.h"
#include "main/ui/pages/pages.h"

// Live right-aligned value summaries for Lab Menu submenu rows (extra module ->
// ExtraHooks::submenuSummary), keeping every string <= 15 chars so Rail & Tabs
// never squeeze below kMinSqueeze on 4:3 or 16:9.
namespace tm {
namespace extra {
    namespace {
        const char* copyStr(char* buf, u32 cap, const char* s)
        {
            snprintf(buf, cap, "%s", s);
            return buf;
        }

        const char* activeOrOff(char* buf, u32 cap, int count, const char* zero = "All Off")
        {
            if (count <= 0)
            {
                return copyStr(buf, cap, zero);
            }
            snprintf(buf, cap, "%d Active", count);
            return buf;
        }

        int countBits32(u32 v)
        {
            int n = 0;
            while (v != 0u)
            {
                n += static_cast<int>(v & 1u);
                v >>= 1;
            }
            return n;
        }

        int countOsdToggles(const main::Settings& s)
        {
            const main::OsdSettings& o = s.osd;
            const u8 flags[20] = {
                o.wavedash, o.lCancel, o.ledgedashInfo, o.actOos, o.frameAdvantage, o.frameAdvantageOnHit,
                o.dashback, o.powershield, o.actOoLag, o.actOoAirborne, o.actOoHitstun, o.jumpCancel,
                o.fastfall, o.boostGrab, o.sdi, o.lockoutTimers, o.comboCounter, o.grabBreakout,
                o.itemThrow, s.cpu.options.techTrackerOsd
            };
            int n = 0;
            for (int i = 0; i < 20; i++)
            {
                if (flags[i] != 0) n++;
            }
            return n;
        }

        int countOverlayActive(const main::OverlaySettings& ov)
        {
            int n = 0;
            for (int g = 0; g < main::kOverlayGroups; g++)
            {
                if (ov.groups[g] != 0) n++;
            }
            return n;
        }

        int countInfoRows(const main::InfoSettings& info)
        {
            int n = 0;
            for (int r = 0; r < main::kInfoRows; r++)
            {
                if (info.rows[r] != 0) n++;
            }
            return n;
        }

        const char* summarizeOverlays(const main::OverlaySettings& ov, char* buf, u32 cap)
        {
            if (ov.preset == 1)
            {
                return copyStr(buf, cap, "Standard");
            }
            return activeOrOff(buf, cap, countOverlayActive(ov), "Off");
        }

        const char* summarizeInfo(const main::InfoSettings& info, char* buf, u32 cap)
        {
            static const char* const kPresets[5] = {NULL, "Basic", "Ledge", "Combat", "Frame Data"};
            int rows = countInfoRows(info);
            if (rows <= 0)
            {
                return copyStr(buf, cap, "Off");
            }
            if (info.preset > 0 && info.preset < 5)
            {
                return copyStr(buf, cap, kPresets[info.preset]);
            }
            snprintf(buf, cap, "%d Rows", rows);
            return buf;
        }

        const char* summarizeHudOverlays(const main::Settings& s, char* buf, u32 cap)
        {
            int n = 0;
            if (countOverlayActive(s.overlay[0]) > 0) n++;
            if (countOverlayActive(s.overlay[1]) > 0) n++;
            if ((s.comboTrainer.enabledMeters & main::kMeterAll) != 0) n++;
            if (s.general.ledgeHud != 0) n++;
            return activeOrOff(buf, cap, n, "All Off");
        }

        bool techWeightsCustom(const cpu::CpuOptions& c)
        {
            if (c.missTechWait != 0) return true;
            for (int i = 0; i < 4; i++)
            {
                if (c.techChance[i] != 25 || c.getupChance[i] != 25) return true;
            }
            return false;
        }

        const char* summarizeCounters(const main::Settings& s, char* buf, u32 cap)
        {
            const cpu::CpuOptions& c = s.cpu.options;
            const cpu::CpuTables& t = s.cpuTables;
            if (c.adaptiveMode != cpu::kAdaptiveOff) return copyStr(buf, cap, "Adaptive");
            if (c.multiReversal != 0) return copyStr(buf, cap, "Reversals");
            for (int h = 0; h < cpu::kAdvancedHits; h++)
            {
                if (t.hits[h].logic != cpu::kLogicDefault) return copyStr(buf, cap, "Per-Hit");
            }
            if (c.counterGround == 0 && c.counterAir == 0 && c.counterShield == 0)
            {
                return copyStr(buf, cap, "Off");
            }
            return copyStr(buf, cap, "Active");
        }

        int activeCpuFtKind()
        {
            game::FighterRef ref;
            if (game::findFighter(true, ref))
            {
                int k = cpu::fighterKind(ref.entryId);
                if (k >= 0) return k;
            }
            return cpu::kKindAny;
        }

        const char* summarizePool(const main::Settings& s, char* buf, u32 cap)
        {
            int checkKind = activeCpuFtKind();
            int counts[3] = {0, 0, 0};
            for (int c = 0; c < 3; c++)
            {
                const u8* bits = s.cpuTables.poolBits[c];
                int total = (bits[0] & 1u) != 0 ? 1 : 0;
                int limit = cpu::actionCount(c);
                if (limit > 64) limit = 64;
                for (int idx = 1; idx < limit; idx++)
                {
                    if ((bits[idx >> 3] & (1u << (idx & 7))) != 0 && cpu::actionFor(c, idx, checkKind) != NULL)
                    {
                        total++;
                    }
                }
                counts[c] = total;
            }
            snprintf(buf, cap, "G%d A%d S%d", counts[0], counts[1], counts[2]);
            return buf;
        }
    }

    const char* menuSubmenuSummary(u8 targetPageId, char* buf, u32 cap)
    {
        if (buf == NULL || cap == 0u)
        {
            return NULL;
        }
        buf[0] = '\0';
        const main::Settings& s = main::settings();
        const cpu::CpuOptions& c = s.cpu.options;

        switch (targetPageId)
        {
        case ui::pages::P_CONTROLS:
            return copyStr(buf, cap, s.controls.menuStyle == main::kMenuStyleTabs ? "Top Tabs" : "Category Rail");

        case ui::pages::P_COLLISION:
        {
            bool heat = s.display.heatmap != 0;
            bool grab = s.display.grabVisualizer != 0;
            bool env = s.display.envCollision != 0;
            int n = (heat ? 1 : 0) + (grab ? 1 : 0) + (env ? 1 : 0);
            if (n > 1) return activeOrOff(buf, cap, n, "Off");
            if (heat) return copyStr(buf, cap, "Heatmap");
            if (grab) return copyStr(buf, cap, "Grab Boxes");
            if (env) return copyStr(buf, cap, "Stage Coll");
            return copyStr(buf, cap, "Off");
        }

        case ui::pages::P_HUD_OVERLAYS:
            return summarizeHudOverlays(s, buf, cap);

        case ui::pages::P_THEME:
        {
            if (s.osd.bgColor == main::kOsdBgTeal && s.osd.bgAlpha == main::kDefaultOsdAlpha
                && s.osd.menuAccent == main::kMenuAccentTeal && s.osd.infoBgColor == main::kInfoBgDefault
                && s.osd.infoBgAlpha == main::kInfoAlphaDefault
                && s.osd.colorblindMode == main::kColorblindOff)
            {
                return copyStr(buf, cap, "Default");
            }
            if (s.osd.menuAccent == main::kMenuAccentTeal)
            {
                return copyStr(buf, cap, "Custom");
            }
            static const char* const kAccents[main::kMenuAccentCount] = {
                "P+ Teal", "Brawl Navy", "Violet", "Crimson"
            };
            u8 acc = s.osd.menuAccent < main::kMenuAccentCount ? s.osd.menuAccent : 0;
            return copyStr(buf, cap, kAccents[acc]);
        }

        case ui::pages::P_DEBUG:
            if (s.display.psaInspector == main::kPsaP1) return copyStr(buf, cap, "PSA: P1");
            if (s.display.psaInspector == main::kPsaCpu) return copyStr(buf, cap, "PSA: CPU");
            if (countInfoRows(s.info[0]) > 0 || countInfoRows(s.info[1]) > 0) return copyStr(buf, cap, "Info HUD");
            if (s.display.debugOverlay != 0) return copyStr(buf, cap, "Debug HUD");
            if (s.display.hud == 0 || s.display.models != 3 || s.display.stageModel == 0 || s.display.effects == 0)
            {
                return copyStr(buf, cap, "Custom");
            }
            return copyStr(buf, cap, "Off");

        case ui::pages::P_HMN_OVERLAYS:
            return summarizeOverlays(s.overlay[0], buf, cap);
        case ui::pages::P_CPU_OVERLAYS:
            return summarizeOverlays(s.overlay[1], buf, cap);

        case ui::pages::P_COMBO_METERS:
        {
            int n = countBits32(s.comboTrainer.enabledMeters & main::kMeterAll);
            if (n == 7) return copyStr(buf, cap, "All On");
            return activeOrOff(buf, cap, n, "Off");
        }

        case ui::pages::P_LEDGEDASH_HUD:
            return copyStr(buf, cap, s.general.ledgeHud != 0 ? "On" : "Off");

        case ui::pages::P_HMN_INFO:
            return summarizeInfo(s.info[0], buf, cap);
        case ui::pages::P_CPU_INFO:
            return summarizeInfo(s.info[1], buf, cap);

        case ui::pages::P_OSD_INDIVIDUAL:
            return activeOrOff(buf, cap, countOsdToggles(s));

        case ui::pages::P_CPU_DI:
        {
            static const char* const kTdi[cpu::kTdiCount] = {
                "Random", "In", "Out", "Survival", "None", "Combo",
                "Slight Rand", "Slight Away", "Down & Away", "Custom"
            };
            bool hasSdi = c.sdiAmount != 0;
            bool hasAsdi = c.asdi != cpu::kAsdiAuto;
            if (hasSdi && hasAsdi) return copyStr(buf, cap, "SDI + ASDI");
            if (hasSdi) return copyStr(buf, cap, c.tdi == cpu::kTdiNone ? "SDI" : "DI + SDI");
            if (hasAsdi && (c.tdi == cpu::kTdiRandom || c.tdi == cpu::kTdiNone)) return copyStr(buf, cap, "ASDI");
            u8 tdi = c.tdi < cpu::kTdiCount ? c.tdi : 0;
            return copyStr(buf, cap, kTdi[tdi]);
        }

        case ui::pages::P_CPU_COUNTERS:
            return summarizeCounters(s, buf, cap);

        case ui::pages::P_TECH:
        {
            static const char* const kTech[5] = {"Random", "In Place", "Away", "Toward", "Miss"};
            u8 t = c.tech < 5 ? c.tech : 0;
            bool extras = c.getup != cpu::kGetupRandom || s.techInvis.mode != 0 || c.techSound != 0
                          || c.techTrap != cpu::kTrapOff || c.techLockout != cpu::kLockoutEarliest
                          || s.techInvis.preTechDrift != 0 || techWeightsCustom(c);
            if (t == cpu::kTechRandom && extras)
            {
                if (s.techInvis.mode != 0 && c.getup == cpu::kGetupRandom && c.techSound == 0
                    && c.techTrap == cpu::kTrapOff && c.techLockout == cpu::kLockoutEarliest
                    && s.techInvis.preTechDrift == 0 && !techWeightsCustom(c))
                {
                    return copyStr(buf, cap, "Invis");
                }
                return copyStr(buf, cap, "Custom");
            }
            return copyStr(buf, cap, kTech[t]);
        }

        case ui::pages::P_TECH_WEIGHTS:
            return copyStr(buf, cap, techWeightsCustom(c) ? "Custom" : "Default");

        case ui::pages::P_CPU_DEFENSE:
        {
            const bool healthDef = (c.infiniteShields == cpu::kShieldsOff || c.shieldHealth == 60);
            const bool otherDef = healthDef && c.shieldAngle == cpu::kAngleNeutral
                && c.grabEscape == cpu::kMashNone && c.grabRelease == cpu::kReleaseGrounded
                && c.clatterEscape == cpu::kMashNone;
            if (otherDef && c.intangible == 0)
            {
                if (c.infiniteShields == cpu::kShieldsOff) return copyStr(buf, cap, "Off");
                if (c.infiniteShields == cpu::kShieldsUntilHit) return copyStr(buf, cap, "Until Hit");
                if (c.infiniteShields == cpu::kShieldsOn) return copyStr(buf, cap, "Infinite");
            }
            if (otherDef && c.intangible != 0 && c.infiniteShields == cpu::kShieldsUntilHit)
            {
                return copyStr(buf, cap, "No KB");
            }
            return copyStr(buf, cap, "Custom");
        }

        case ui::pages::P_COUNTER_POOL:
            return summarizePool(s, buf, cap);

        case ui::pages::P_ADVANCED_COUNTER:
        {
            int n = 0;
            for (int i = 0; i < cpu::kAdvancedHits; i++)
            {
                if (s.cpuTables.hits[i].logic != cpu::kLogicDefault) n++;
            }
            return activeOrOff(buf, cap, n, "Off");
        }

        case ui::pages::P_REVERSALS:
            return copyStr(buf, cap, c.multiReversal != 0 ? "On" : "Off");

        case ui::pages::P_SLOTS:
        {
            int used = 0;
            for (int side = 0; side < 2; side++)
            {
                for (int sl = 0; sl < 6; sl++)
                {
                    if (record::slotHasInputs(side, sl)) used++;
                }
            }
            if (used <= 0) return copyStr(buf, cap, "Empty");
            snprintf(buf, cap, "%d Saved", used);
            return buf;
        }

        case ui::pages::P_REC_MANAGE:
            if (s.recording.mirrored != 0) return copyStr(buf, cap, "Mirrored");
            if (s.recording.cpuCounter != 1 || s.recording.takeover != 0 || s.recording.startPaused != 0
                || s.recording.startOnFirstInput != 0)
            {
                return copyStr(buf, cap, "Custom");
            }
            return copyStr(buf, cap, "Default");

        case ui::pages::P_COMBO_TRAINER:
            if (s.comboTrainer.enabled == 0) return copyStr(buf, cap, "Off");
            return copyStr(buf, cap, s.comboTrainer.recoveryTrainer != 0 ? "Recovery" : "On");

        case ui::pages::P_OLIMAR:
        {
            const main::FighterMechanicsSettings& fm = s.fighterMechanics;
            u8 mode = olimarDpadMode(fm);
            if (mode == main::kOlimarDpadPluck) return copyStr(buf, cap, "D-Pad Pluck");
            if (mode == main::kOlimarDpadPresets) return copyStr(buf, cap, "Presets");
            if (olimarEffectivePullMask(fm) != main::kOlimarPullPoolAll || olimarWhistleColor(fm) != 0u
                || fm.olimarMaturity != 0 || (fm.olimarColorMatAndMode & 0x03FFu) != 0u
                || (fm.olimarStartLineup & 0x0FFFu) != 0u)
            {
                return copyStr(buf, cap, "Custom");
            }
            return copyStr(buf, cap, "Default");
        }

        default:
            return NULL;
        }
    }
}
}
