/*
 * SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cacam-usb-source.hpp"

struct obs_source_info cacam_usb_source_info = {
	.id = "bgobs_cacam_usb_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC_VIDEO,
	.get_name = cacam_usb_source_getname,
	.create = cacam_usb_source_create,
	.destroy = cacam_usb_source_destroy,
	.get_width = cacam_usb_source_get_width,
	.get_height = cacam_usb_source_get_height,
	.get_defaults = cacam_usb_source_defaults,
	.get_properties = cacam_usb_source_properties,
	.update = cacam_usb_source_update,
	.activate = cacam_usb_source_activate,
	.deactivate = cacam_usb_source_deactivate,
};
