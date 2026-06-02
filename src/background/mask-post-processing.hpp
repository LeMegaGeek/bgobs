// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BACKGROUND_MASK_POST_PROCESSING_HPP
#define BACKGROUND_MASK_POST_PROCESSING_HPP

#include <opencv2/core.hpp>

namespace background_removal {

struct MaskPostProcessingSettings {
	bool enableThreshold = true;
	float threshold = 0.5f;
	float edgeSoftness = 0.0f;
	float foregroundCleanup = 0.0f;
	float contourFilter = 0.05f;
	float smoothContour = 0.5f;
	float feather = 0.0f;
	int maskExpansion = 0;
};

struct TemporalMaskSmoothingSettings {
	float temporalSmoothFactor = 0.0f;
	bool protectForeground = true;
};

struct ImageGuidedMaskRefinementSettings {
	float edgeRefinement = 0.0f;
};

cv::Mat postProcessForegroundMask(const cv::Mat &foregroundMask, const cv::Size &targetSize,
				  const MaskPostProcessingSettings &settings);

cv::Mat smoothTemporalBackgroundMask(const cv::Mat &currentBackgroundMask, const cv::Mat &previousBackgroundMask,
				     const TemporalMaskSmoothingSettings &settings);

cv::Mat refineBackgroundMaskWithImage(const cv::Mat &backgroundMask, const cv::Mat &sourceImage,
				      const ImageGuidedMaskRefinementSettings &settings);

} // namespace background_removal

#endif // BACKGROUND_MASK_POST_PROCESSING_HPP
