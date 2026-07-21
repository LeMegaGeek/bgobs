/*
 * SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
 * SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
 * SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <obs-module.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "plugin-support.h"

#include "update-checker/update-checker.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

extern struct obs_source_info bgobs_background_filter_info;
extern struct obs_source_info enhance_filter_info;
extern struct obs_source_info cacam_usb_source_info;

static void log_module_paths(void)
{
#ifdef _WIN32
	HMODULE module = NULL;
	char module_path[MAX_PATH] = {0};
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			       (LPCSTR)&obs_module_load, &module) &&
	    GetModuleFileNameA(module, module_path, MAX_PATH) > 0) {
		obs_log(LOG_INFO, "Loaded module path: %s", module_path);
	}
#endif

	char *manifest_path = obs_module_file("manifest.json");
	if (manifest_path) {
		obs_log(LOG_INFO, "Data manifest path: %s", manifest_path);
		bfree(manifest_path);
	}

	char *config_path = obs_module_config_path(NULL);
	if (config_path) {
		obs_log(LOG_INFO, "Config path: %s", config_path);
		bfree(config_path);
	}
}

bool obs_module_load(void)
{
	obs_register_source(&bgobs_background_filter_info);
	obs_register_source(&enhance_filter_info);
	obs_register_source(&cacam_usb_source_info);
	obs_log(LOG_INFO, "BGOBS loaded successfully (version %s, author LeMegaGeek)", PLUGIN_VERSION);
	log_module_paths();

	check_update();

	return true;
}

void obs_module_unload()
{
	shutdown_update_checker();
	obs_log(LOG_INFO, "plugin unloaded");
}
