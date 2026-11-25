#pragma once

// Single source of truth for the plugin's identity and version.
#define TM_PLUGIN_NAME "TrainingMode"
#define TM_PLUGIN_AUTHOR "skier"
#define TM_VERSION "1.0.1"
// What the screen shows, so a screenshot says which build it is: short
// enough to clear the notice and the recording bar on 4:3. TM_VERSION
// stays numeric for Syringe.
#define TM_VERSION_MARK "P+TM v1.0.1-pre3"

// C++03 compile-time assertion (MWCC 4.2 has no static_assert): an array of
// negative size fails to compile.
#define TM_STATIC_ASSERT(cond, name) typedef char name[(cond) ? 1 : -1]

// PluginMeta::NAME and ::AUTHOR are char[20], including the terminating NUL.
TM_STATIC_ASSERT(sizeof(TM_PLUGIN_NAME) <= 20, tm_plugin_name_fits_in_PluginMeta);
TM_STATIC_ASSERT(sizeof(TM_PLUGIN_AUTHOR) <= 20, tm_plugin_author_fits_in_PluginMeta);
