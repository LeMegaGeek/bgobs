// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "obs-config-utils.hpp"
#include "../plugin-support.h"

#include <obs-module.h>
#include <util/config-file.h>
#include <filesystem>
#include <util/platform.h>

void create_config_folder()
{
	char *config_folder_path = obs_module_config_path(NULL);
	if (!config_folder_path) {
		obs_log(LOG_ERROR, "Failed to get config folder path");
		return;
	}
	os_mkdirs(config_folder_path);
	bfree(config_folder_path);
}

int getConfig(config_t **config)
{
	create_config_folder(); // ensure the config folder exists

	// Get the config file
	char *config_file_path = obs_module_config_path("config.ini");
	if (!config_file_path) {
		obs_log(LOG_ERROR, "Failed to get config file path");
		return OBS_BGREMOVAL_CONFIG_FAIL;
	}

	int ret = config_open(config, config_file_path, CONFIG_OPEN_ALWAYS);
	if (ret != CONFIG_SUCCESS) {
		obs_log(LOG_INFO, "Failed to open config file %s", config_file_path);
		bfree(config_file_path);
		return OBS_BGREMOVAL_CONFIG_FAIL;
	}

	bfree(config_file_path);
	return OBS_BGREMOVAL_CONFIG_SUCCESS;
}

int getFlagFromConfig(const char *name, bool *returnValue, bool defaultValue)
{
	// Get the config file
	config_t *config;
	if (getConfig(&config) != OBS_BGREMOVAL_CONFIG_SUCCESS) {
		*returnValue = defaultValue;
		return OBS_BGREMOVAL_CONFIG_FAIL;
	}

	if (!config_has_user_value(config, "config", name)) {
		config_set_bool(config, "config", name, defaultValue);
		*returnValue = defaultValue;
		if (config_save(config) != CONFIG_SUCCESS) {
			obs_log(LOG_ERROR, "Failed to save default config value %s", name);
			config_close(config);
			return OBS_BGREMOVAL_CONFIG_FAIL;
		}
	} else {
		*returnValue = config_get_bool(config, "config", name);
	}
	config_close(config);

	return OBS_BGREMOVAL_CONFIG_SUCCESS;
}

int setFlagInConfig(const char *name, const bool value)
{
	// Get the config file
	config_t *config;
	if (getConfig(&config) != OBS_BGREMOVAL_CONFIG_SUCCESS) {
		return OBS_BGREMOVAL_CONFIG_FAIL;
	}

	config_set_bool(config, "config", name, value);
	if (config_save(config) != CONFIG_SUCCESS) {
		obs_log(LOG_ERROR, "Failed to save config value %s", name);
		config_close(config);
		return OBS_BGREMOVAL_CONFIG_FAIL;
	}
	config_close(config);

	return OBS_BGREMOVAL_CONFIG_SUCCESS;
}
