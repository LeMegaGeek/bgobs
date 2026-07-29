// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugin-info.hpp"

#include <obs-module.h>

#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>

#include "consts.h"
#include "plugin-support.h"

namespace {

std::string file_url(const char *path)
{
	std::ostringstream encoded;
	encoded << "file://";

	std::string normalized(path);
	for (char &character : normalized) {
		if (character == '\\') {
			character = '/';
		}
	}
	if (normalized.empty() || normalized.front() != '/') {
		encoded << '/';
	}

	encoded << std::uppercase << std::hex;
	for (const unsigned char character : normalized) {
		if (std::isalnum(character) || character == '-' || character == '_' || character == '.' ||
		    character == '~' || character == '/' || character == ':') {
			encoded << character;
		} else {
			encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(character);
		}
	}

	return encoded.str();
}

} // namespace

std::string bgobs_plugin_info_html()
{
	std::string info = "<p align=\"center\">";
	char *logo_path = obs_module_file(PLUGIN_LOGO_PATH);
	if (logo_path) {
		info += "<img src=\"" + file_url(logo_path) + "\" width=\"64\" height=\"64\" /><br />";
		bfree(logo_path);
	}

	info += "<a href=\"";
	info += PLUGIN_WEBSITE_URL;
	info += "\"><b>BGOBS</b></a> (";
	info += PLUGIN_VERSION;
	info += ") by <a href=\"https://github.com/LeMegaGeek\">LeMegaGeek</a></p>";
	return info;
}
