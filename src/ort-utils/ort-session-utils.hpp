// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ORT_SESSION_UTILS_H
#define ORT_SESSION_UTILS_H

#include <opencv2/core/types.hpp>

#include "../FilterData.hpp"

#define OBS_BGREMOVAL_ORT_SESSION_ERROR_FILE_NOT_FOUND 1
#define OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_MODEL 2
#define OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_INPUT_OUTPUT 3
#define OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP 5
#define OBS_BGREMOVAL_ORT_SESSION_SUCCESS 0

int createOrtSession(filter_data *tf);

bool runFilterModelInference(filter_data *tf, const cv::Mat &imageBGRA, cv::Mat &output);

/**
 * Run inference while preserving the model's native floating-point output.
 * RVM uses this path so its soft alpha is not quantized to 8-bit before the
 * OBS compositor. Other models continue to use the legacy 8-bit wrapper.
 */
bool runFilterModelInferenceFloat(filter_data *tf, const cv::Mat &imageBGRA, cv::Mat &output,
				  cv::Mat *reconstructedForegroundBGRA = nullptr);

/** Reset recurrent state held by a temporal model, if any. */
void resetFilterModelState(filter_data *tf);

#endif /* ORT_SESSION_UTILS_H */
