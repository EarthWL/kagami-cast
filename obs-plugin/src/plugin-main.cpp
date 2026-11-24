// SPDX-License-Identifier: GPL-3.0-or-later
#include <obs-module.h>
#include <plugin-support.h>

#include "kagami-source.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Kagami Cast: AirPlay mirroring from iPhone/iPad as an OBS source";
}

bool obs_module_load(void)
{
	kagami_register_source();
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
