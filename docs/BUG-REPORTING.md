# Reporting bugs

Open an issue at <https://github.com/LeMegaGeek/bgobs/issues> after checking for
an existing report. For security vulnerabilities, follow
[SECURITY.md](../SECURITY.md) instead of opening a public issue.

Include:

- a concise description;
- exact steps to reproduce;
- expected and actual behavior;
- BGOBS and OBS Studio versions;
- operating system and architecture;
- relevant camera, USB device, CPU, or GPU details;
- whether the problem also occurs with a new OBS scene collection;
- screenshots or a short recording when they help;
- an OBS log covering the failure.

In OBS, use **Help → Log Files → Upload Current Log File** or **View Current Log**.
On Windows, logs are also normally under `%APPDATA%\obs-studio\logs`. The
following screenshot shows the log menu on Windows:

![OBS log menu on Windows](logs_location_windows.png)

Before publishing a log, remove stream keys, authentication headers, usernames,
home-directory paths, Android serial numbers, IP addresses, and any other data
you do not want to disclose. Do not attach a full OBS profile or scene
collection unless it has been sanitized.

<!--
SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>

SPDX-License-Identifier: GPL-3.0-or-later
-->
