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
const char *PLUGIN_VERSION = "0.3.8";

void bgobs_logv(int log_level, const char *format, va_list args)
{
	size_t length = 4 + strlen(PLUGIN_NAME) + strlen(format);

	char *template = malloc(length + 1);

	snprintf(template, length + 1, "[%s] %s", PLUGIN_NAME, format);
	blogva(log_level, template, args);
	free(template);
}

void obs_log(int log_level, const char *format, ...)
{
	va_list(args);
	va_start(args, format);
	bgobs_logv(log_level, format, args);
	va_end(args);
}
