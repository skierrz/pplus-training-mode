#include "main/extra.h"
#include "main/ui/pages/cpu_rows.h"
#include "main/ui/pages/pages.h"

namespace tm {
namespace ui {
namespace pages {
    static bool comboActive()
    {
        return cpuLabControl() && main::extraLoaded();
    }

    static bool comboOptionEnabled()
    {
        return comboActive() && main::settings().comboTrainer.enabled != 0;
    }

    static bool comboAnyResetEnabled()
    {
        const main::ComboTrainerSettings& c = main::settings().comboTrainer;
        return comboOptionEnabled() && (c.fixPosition != 0 || c.recoveryTrainer != 0);
    }

    static bool comboFixPosEligible()
    {
        return comboOptionEnabled() && main::settings().comboTrainer.recoveryTrainer == 0;
    }

    static bool comboFixPosChildEnabled()
    {
        const main::ComboTrainerSettings& c = main::settings().comboTrainer;
        return comboFixPosEligible() && c.fixPosition != 0;
    }

    static bool comboRandGroundEnabled()
    {
        return comboFixPosChildEnabled() && main::settings().comboTrainer.randomizePosition != 0;
    }

    static bool comboFixedDmgEnabled()
    {
        return comboAnyResetEnabled() && main::settings().comboTrainer.randomizeDamage == 0;
    }

#define TM_COMBO_TOGGLE(name, f, def, en) \
    TM_ROW(::tm::ui::kKindToggle, TM_HALF(f), TM_FIELD(f), 0, 1, def, name, NULL, NULL, en)
#define TM_COMBO_INT(name, f, lo, hi, def, format, en) \
    TM_ROW(::tm::ui::kKindInt, TM_HALF(f) | TM_SIGNED(lo), TM_FIELD(f), lo, hi, def, name, format, NULL, en)
#define TM_COMBO_BIT(name, f, bit) \
    TM_ROW(::tm::ui::kKindToggle, ::tm::ui::kFlagBit, TM_FIELD(f), (bit), 1, 1, name, NULL, NULL, comboOptionEnabled)

    static const Option kRows[] = {
        TM_COMBO_TOGGLE("Enabled", comboTrainer.enabled, 0, comboActive),
        TM_COMBO_TOGGLE("Noclip Reset", comboTrainer.noclip, 0, comboAnyResetEnabled),
        TM_COMBO_TOGGLE("Fix Position", comboTrainer.fixPosition, 0, comboFixPosEligible),
        TM_COMBO_TOGGLE("Random Position", comboTrainer.randomizePosition, 0, comboFixPosChildEnabled),
        TM_COMBO_TOGGLE("Random On Ground", comboTrainer.randOnGround, 0, comboRandGroundEnabled),
        TM_COMBO_TOGGLE("Random Damage", comboTrainer.randomizeDamage, 0, comboAnyResetEnabled),
        TM_COMBO_INT("Fixed Damage", comboTrainer.fixedDamage, 0, 999, 0, "%d%%", comboFixedDmgEnabled),
        TM_COMBO_INT("Timer Adjust", comboTrainer.comboTimerAdjustment, -60, 60, 0, "%+df", comboFixPosChildEnabled),
        TM_COMBO_TOGGLE("Recovery Trainer", comboTrainer.recoveryTrainer, 0, comboOptionEnabled),
        TM_MENU_IF("Floating Meters", P_COMBO_METERS, comboOptionEnabled)
    };

    static const Option kMeterRows[] = {
        TM_COMBO_BIT("Hitlag", comboTrainer.enabledMeters, main::kMeterHitlag),
        TM_COMBO_BIT("Hitstun", comboTrainer.enabledMeters, main::kMeterHitstun),
        TM_COMBO_BIT("Shieldstun", comboTrainer.enabledMeters, main::kMeterShieldstun),
        TM_COMBO_BIT("DI Ring", comboTrainer.enabledMeters, main::kMeterDi),
        TM_COMBO_BIT("IASA", comboTrainer.enabledMeters, main::kMeterIasa),
        TM_COMBO_BIT("Ledge Intang", comboTrainer.enabledMeters, main::kMeterLedgeIntang),
        TM_COMBO_BIT("Intangibility", comboTrainer.enabledMeters, main::kMeterIntang)
    };

#undef TM_COMBO_TOGGLE
#undef TM_COMBO_INT
#undef TM_COMBO_BIT

    extern const Page kComboTrainer = TM_PAGE("Combo Trainer", kRows);
    extern const Page kComboMeters = TM_PAGE("Floating Meters", kMeterRows);
}
}
}
