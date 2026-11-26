#pragma once

#include <types.h>

#include "main/cpu/cpu_settings.h"
#include "main/cpu/fighter_input.h"

// The ten Advanced Counter pages, kept in the settings block
// (main::Settings::cpuTables). The Advanced Counter page edits one hit at a
// time through its fields in CpuOptions.
namespace tm {
namespace cpu {
    // Hit 1..kAdvancedHits.
    const AdvancedHit& advancedHit(int hit);
    // Part of the settings' defaults (main::resetSettings), so a reset and the
    // card's "nothing changed" view agree.
    void advancedHitsDefaults(CpuTables& t);
    void tablesDefaults(CpuTables& t);
    void poolTablesDefaults(CpuTables& t);
    // At attach: the pages' defaults after a reset; the page shows hit 1.
    void advancedAttach();
}
}
