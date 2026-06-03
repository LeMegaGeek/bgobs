// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BGOBS_CORE_H
#define BGOBS_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bgobs_mask_post_processing_settings {
	uint8_t enable_threshold;
	float threshold;
	float edge_softness;
} bgobs_mask_post_processing_settings;

typedef struct bgobs_temporal_mask_smoothing_settings {
	float temporal_smooth_factor;
	uint8_t protect_foreground;
} bgobs_temporal_mask_smoothing_settings;

typedef enum bgobs_mask_status {
	BGOBS_MASK_STATUS_OK = 0,
	BGOBS_MASK_STATUS_NULL_POINTER = 1,
	BGOBS_MASK_STATUS_OUTPUT_LENGTH_MISMATCH = 2,
	BGOBS_MASK_STATUS_TEMPORAL_LENGTH_MISMATCH = 3,
} bgobs_mask_status;

bgobs_mask_status bgobs_write_background_mask_from_foreground(const uint8_t *foreground_mask, size_t foreground_len,
							      bgobs_mask_post_processing_settings settings,
							      uint8_t *background_mask, size_t background_len);

bgobs_mask_status bgobs_write_smooth_temporal_background_mask(const uint8_t *current_background_mask,
							      size_t current_len,
							      const uint8_t *previous_background_mask,
							      size_t previous_len,
							      bgobs_temporal_mask_smoothing_settings settings,
							      uint8_t *smoothed_mask, size_t smoothed_len);

#ifdef __cplusplus
}
#endif

#endif // BGOBS_CORE_H
