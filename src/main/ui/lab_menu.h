#pragma once

#include "main/ui/menu.h"

// The lab menu: its pages (ui/pages/) over main::Settings.
namespace tm {
namespace ui {
    const MenuDef& labMenu();

    void clampSettings();
    // Sets every setting a menu row shows to that row's default.
    void applyMenuDefaults();
    bool applyMenuDefaultOption(int pageId, int row);
    bool applyMenuDefaultPage(int pageId);
    void applyMenuDefaultRange(u16 offset, u16 size);
    // Loaded settings: values the menu no longer offers (dropped choices,
    // in the rows and in the Advanced Counter pages) go to the defaults.
    void retireDroppedValues();
}
}
