---
# SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
# SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
# SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later

layout: ../layouts/MarkdownLayout.astro
pathname: uninstall
lang: en
title: Uninstall BGOBS
description: Remove the BGOBS plugin from OBS Studio on Windows, macOS, or Ubuntu.
---

<p class="eyebrow">Removal guide</p>

# Uninstall BGOBS

Close OBS Studio before removing plugin files. Delete only the BGOBS paths shown
for your installation; do not remove the parent OBS Studio directories.

## Windows

For a standard installation, delete:

```text
C:\ProgramData\obs-studio\plugins\bgobs
```

For portable OBS Studio, delete the `bgobs` plugin directories you copied under
that portable installation. Their exact root depends on where the portable
package is stored.

## macOS

If BGOBS was installed for the current user, remove:

```text
~/Library/Application Support/obs-studio/plugins/bgobs.plugin
```

An administrator-installed package may instead place the plugin under the
system Library. Use the installer receipt or package documentation to identify
the exact BGOBS path before deleting it.

## Ubuntu

If BGOBS was installed from its official Debian package:

```sh
sudo apt remove bgobs
```

Restart OBS Studio after removal.
