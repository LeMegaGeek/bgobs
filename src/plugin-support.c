/*
 * SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
 * SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
 * SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plugin-support.h"

extern void blogva(int log_level, const char *format, va_list args);

const char *PLUGIN_NAME = "bgobs";
const char *PLUGIN_VERSION = "0.3.1";

void obs_log(int log_level, const char *format, ...)
{
	size_t length = 4 + strlen(PLUGIN_NAME) + strlen(format);

	char *template = malloc(length + 1);

	snprintf(template, length, "[%s] %s", PLUGIN_NAME, format);

	va_list(args);

	va_start(args, format);
	blogva(log_level, template, args);
	va_end(args);

	free(template);
}
