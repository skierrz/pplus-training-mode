#include "main/settings.h"

#include <string.h>

#include "main/cpu/actions.h"
#include "main/cpu/lab_data.h"
#include "main/game/overlays.h"
#include "main/storage/areas.h"
#include "main/ui/lab_menu.h"
#include "main/ui/pages/cpu_rows.h"

namespace tm {
namespace main {
    static Settings* s_settings = NULL;
    // Used only if no block was handed over (never expected while attached).
    static Settings s_fallback;

    static u8 clampU8(u8 v, u8 hi)
    {
        return v > hi ? hi : v;
    }

    void sanitizeSettings(Settings& s)
    {
        cpu::CpuTables& t = s.cpuTables;
        t.ready = t.ready != 0 ? 1 : 0;
        static const u8 kActionMax[3] = {ui::pages::kCounterGroundSize - 1, ui::pages::kCounterAirSize - 1,
                                         ui::pages::kCounterShieldSize - 1};
        for (int i = 0; i < cpu::kAdvancedHits; i++)
        {
            cpu::AdvancedHit& h = t.hits[i];
            h.logic = clampU8(h.logic, cpu::kLogicCustom);
            for (int k = 0; k < 3; k++)
            {
                h.action[k] = clampU8(h.action[k], kActionMax[k]);
                h.delay[k] = clampU8(h.delay[k], 99);
            }
        }
        if (t.poolReady == 0)
        {
            cpu::poolTablesDefaults(t);
        }
        else
        {
            t.poolReady = 1;
            for (int k = 0; k < cpu::kListCount; k++)
            {
                cpu::poolSanitize(k, t.poolBits[k]);
            }
            if (t.customDiX < -100) t.customDiX = -100;
            else if (t.customDiX > 100) t.customDiX = 100;
            if (t.customDiY < -100) t.customDiY = -100;
            else if (t.customDiY > 100) t.customDiY = 100;
        }

        // Group kGroupJumps is marked unavailable in the menu, so clampAll skips it.
        for (int side = 0; side < kSideCount; side++)
        {
            for (int g = 0; g < kOverlayGroups; g++)
            {
                s.overlay[side].groups[g] = clampU8(s.overlay[side].groups[g], game::kOverlayChoiceCount - 1);
            }
        }

        struct U8Clamp {
            u16 offset;
            u8 lo;
            u8 hi;
        };
#define TM_CLAMP(f, lo, hi) {offsetof(Settings, f), lo, hi}
        static const U8Clamp kClamps[] = {
            TM_CLAMP(general.actionLog, 0, kActionLogTargetCount - 1),
            TM_CLAMP(controls.menuStyle, 0, kMenuStyleCount - 1),
            TM_CLAMP(display.heatmap, 0, 3),
            TM_CLAMP(display.heatmapBounds, 0, 1),
            TM_CLAMP(display.heatmapOpacity, 20, 100),
            TM_CLAMP(display.heatmapColorFrames, 1, 30),
            TM_CLAMP(display.grabVisualizer, 0, 3),
            TM_CLAMP(display.psaInspector, 0, kPsaTargetCount - 1),
            TM_CLAMP(recording.startOnFirstInput, 0, 1),
            TM_CLAMP(stageOptions.hazards, 0, kStageHazardCount - 1),
            TM_CLAMP(stageOptions.platformMode, 0, kStagePlatMaxChoices - 1),
            TM_CLAMP(stageOptions.fodLeft, 0, kFodHeightCount - 1),
            TM_CLAMP(stageOptions.fodRight, 0, kFodHeightCount - 1),
            TM_CLAMP(osd.preset, 0, kOsdPresetCount - 1),
            TM_CLAMP(osd.techSparkle, 0, kTechSparkleCount - 1),
            TM_CLAMP(cpu.options.tdi, 0, cpu::kTdiCount - 1),
            TM_CLAMP(cpu.options.shieldTiming, 0, cpu::kShieldTimingCount - 1),
            TM_CLAMP(comboTrainer.fixedDamageMode, 0, kComboDmgModeCount - 1),
            TM_CLAMP(comboTrainer.resetP1, 0, 1),
            TM_CLAMP(comboTrainer.resetDelay, 0, kComboDelayCount - 1),
            TM_CLAMP(comboTrainer.postHitstunEscape, 0, kComboEscapeCount - 1),
            TM_CLAMP(comboTrainer.recoveryMixup, 0, kRecMixupCount - 1),
            TM_CLAMP(techInvis.mode, 0, kTechInvisCount - 1),
            TM_CLAMP(techInvis.preTechDrift, 0, kPreTechDriftCount - 1),
        };
#undef TM_CLAMP
        u8* raw = reinterpret_cast<u8*>(&s);
        for (u32 i = 0; i < sizeof(kClamps) / sizeof(kClamps[0]); i++)
        {
            u8& v = raw[kClamps[i].offset];
            if (v < kClamps[i].lo)
            {
                v = kClamps[i].lo;
            }
            else if (v > kClamps[i].hi)
            {
                v = kClamps[i].hi;
            }
        }

        s.comboTrainer.enabledMeters &= kMeterAll;
    }

    Settings& settings()
    {
        return s_settings != NULL ? *s_settings : s_fallback;
    }

    void resetSettings()
    {
        Settings& s = settings();
        memset(&s, 0, sizeof(Settings));
        s.magic = kSettingsMagic;
        s.version = kSettingsVersion;
        s.size = sizeof(Settings);
        // Every value a menu row shows takes that row's default.
        ui::applyMenuDefaults();
        cpu::tablesDefaults(s.cpuTables);
    }

#pragma dont_inline on
    static bool finishReset(bool ok)
    {
        if (ok)
        {
            ui::clampSettings();
            sanitizeSettings(settings());
        }
        return ok;
    }
#pragma dont_inline reset

    // Exported for callers outside the main module (e.g. extra UI shells).
#pragma force_active on
    u32 resetSettingsAreas(u32 areaMask)
    {
        Settings& s = settings();
        u8* base = reinterpret_cast<u8*>(&s);
        u32 done = 0;
        u32 bit = 2u;
        for (int id = 1; id <= storage::kAreaLast; id++, bit <<= 1)
        {
            u16 off, sz;
            if ((areaMask & bit) == 0 || !storage::areaBounds(id, off, sz))
            {
                continue;
            }
            memset(base + off, 0, sz);
            ui::applyMenuDefaultRange(off, sz);
            done |= bit;
        }
        if (done & storage::areaBit(storage::kAreaCpuTables))
        {
            cpu::tablesDefaults(s.cpuTables);
        }
        finishReset(done != 0);
        return done;
    }

    bool resetSettingsArea(int areaId)
    {
        if (static_cast<u32>(areaId - 1) >= static_cast<u32>(storage::kAreaLast))
        {
            return false;
        }
        return resetSettingsAreas(storage::areaBit(areaId)) != 0;
    }

    bool resetSettingsPage(int pageId)
    {
        return finishReset(ui::applyMenuDefaultPage(pageId));
    }

    bool resetSettingsOption(int pageId, int row)
    {
        return finishReset(ui::applyMenuDefaultOption(pageId, row));
    }
#pragma force_active reset

    bool loadSettings(u8* block)
    {
        s_settings = reinterpret_cast<Settings*>(block);
        Settings& s = settings();
        bool reset = s.magic != kSettingsMagic || s.version != kSettingsVersion || s.size != sizeof(Settings);
        if (reset)
        {
            resetSettings();
        }
        finishReset(true);
        return reset;
    }

    // Intangibility (NoReaction) holds the CPU at 0%.
    static bool cpuAtZero(const Settings& s)
    {
        return s.cpu.options.intangible == cpu::kIntangibleNoReaction && s.cpu.options.control == cpu::kControlLab;
    }

    u16 percentSetting(int side)
    {
        const Settings& s = settings();
        if (side == kSideCpu)
        {
            return cpuAtZero(s) ? 0 : s.cpu.percent;
        }
        return s.general.playerPercent;
    }

    bool lockSetting(int side)
    {
        const Settings& s = settings();
        return side == kSideCpu ? s.cpu.lockPercent || cpuAtZero(s) : s.general.lockPlayerPercent != 0;
    }
}
}
