/*
 * SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdarg.h>

/* Model tests do not start OBS, so provide the logging backend used by the
 * shared plugin-support implementation without loading libobs.dll. */
void blogva(int log_level, const char *format, va_list args)
{
	(void)log_level;
	(void)format;
	(void)args;
}
