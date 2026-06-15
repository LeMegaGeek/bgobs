/*
 * SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *cacam_usb_source_getname(void *unused);
void *cacam_usb_source_create(obs_data_t *settings, obs_source_t *source);
void cacam_usb_source_destroy(void *data);
uint32_t cacam_usb_source_get_width(void *data);
uint32_t cacam_usb_source_get_height(void *data);
void cacam_usb_source_defaults(obs_data_t *settings);
obs_properties_t *cacam_usb_source_properties(void *data);
void cacam_usb_source_update(void *data, obs_data_t *settings);
void cacam_usb_source_activate(void *data);
void cacam_usb_source_deactivate(void *data);

#ifdef __cplusplus
}
#endif
