// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mask-post-processing.hpp"

#include <bgobs_core.h>

#include <opencv2/core/version.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

namespace bgobs {
namespace {

struct PixelColor {
	int b;
	int g;
	int r;
};

int makeOddKernelSize(float size)
{
	int kernelSize = (int)size;
	kernelSize += kernelSize % 2 == 0 ? 1 : 0;
	return std::max(kernelSize, 1);
}

cv::Mat resizeMaskToTarget(const cv::Mat &mask, const cv::Size &targetSize, int interpolation)
{
	cv::Mat resizedMask;
	if (mask.size() == targetSize) {
		mask.copyTo(resizedMask);
	} else {
		cv::resize(mask, resizedMask, targetSize, 0.0, 0.0, interpolation);
	}
	return resizedMask;
}

PixelColor colorAt(const cv::Mat &image, int row, int col)
{
	if (image.type() == CV_8UC4) {
		const cv::Vec4b pixel = image.at<cv::Vec4b>(row, col);
		return {pixel[0], pixel[1], pixel[2]};
	}

	const cv::Vec3b pixel = image.at<cv::Vec3b>(row, col);
	return {pixel[0], pixel[1], pixel[2]};
}

float colorDistanceSquared(PixelColor first, PixelColor second)
{
	const int db = first.b - second.b;
	const int dg = first.g - second.g;
	const int dr = first.r - second.r;
	return (float)(db * db + dg * dg + dr * dr);
}

void smoothMaskContour(cv::Mat &backgroundMask, int kernelSize)
{
#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 7)
	cv::stackBlur(backgroundMask, backgroundMask, cv::Size(kernelSize, kernelSize));
#else
	cv::blur(backgroundMask, backgroundMask, cv::Size(kernelSize, kernelSize));
#endif
}

void cleanMaskSpecklesWithRust(cv::Mat &backgroundMask, float foregroundCleanup, float backgroundCleanup)
{
	const float clampedCleanup = std::clamp(foregroundCleanup, 0.0f, 1.0f);
	const float clampedBackgroundCleanup = std::clamp(backgroundCleanup, 0.0f, 1.0f);
	if (clampedCleanup <= 0.0f && clampedBackgroundCleanup <= 0.0f) {
		return;
	}

	CV_Assert(backgroundMask.type() == CV_8UC1);

	const cv::Mat inputMask = backgroundMask.isContinuous() ? backgroundMask : backgroundMask.clone();
	cv::Mat cleanedMask(inputMask.size(), CV_8UC1);
	const size_t maskSize = inputMask.total();

	const bgobs_spatial_mask_cleanup_settings rustSettings = {
		(size_t)inputMask.cols,
		(size_t)inputMask.rows,
		clampedCleanup,
		clampedBackgroundCleanup,
	};

	const bgobs_mask_status status = bgobs_write_clean_background_mask(
		inputMask.ptr<uint8_t>(), maskSize, rustSettings, cleanedMask.ptr<uint8_t>(), maskSize);
	CV_Assert(status == BGOBS_MASK_STATUS_OK);

	cleanedMask.copyTo(backgroundMask);
}

cv::Mat makeBackgroundMaskWithRust(const cv::Mat &foregroundMask, bool enableThreshold, float threshold,
				   float edgeSoftness)
{
	CV_Assert(foregroundMask.type() == CV_8UC1);

	const cv::Mat foregroundMaskContinuous = foregroundMask.isContinuous() ? foregroundMask
									       : foregroundMask.clone();
	cv::Mat backgroundMask(foregroundMaskContinuous.size(), CV_8UC1);
	const size_t maskSize = foregroundMaskContinuous.total();

	const bgobs_mask_post_processing_settings rustSettings = {
		(uint8_t)(enableThreshold ? 1 : 0),
		threshold,
		edgeSoftness,
	};

	const bgobs_mask_status status =
		bgobs_write_background_mask_from_foreground(foregroundMaskContinuous.ptr<uint8_t>(), maskSize,
							    rustSettings, backgroundMask.ptr<uint8_t>(), maskSize);
	CV_Assert(status == BGOBS_MASK_STATUS_OK);

	return backgroundMask;
}

cv::Mat makeSoftBackgroundMask(const cv::Mat &foregroundMask, float threshold, float edgeSoftness)
{
	return makeBackgroundMaskWithRust(foregroundMask, true, threshold, edgeSoftness);
}

void filterSmallBackgroundComponents(cv::Mat &backgroundMask, float contourFilter)
{
	if (contourFilter <= 0.0f || contourFilter >= 1.0f) {
		return;
	}

	cv::Mat componentMask = backgroundMask > 128;
	cv::Mat labels;
	cv::Mat stats;
	cv::Mat centroids;
	const int componentCount = cv::connectedComponentsWithStats(componentMask, labels, stats, centroids, 8, CV_32S);
	cv::Mat filteredComponentMask = cv::Mat::zeros(backgroundMask.size(), CV_8UC1);
	const double contourSizeThreshold = (double)(backgroundMask.total()) * contourFilter;

	for (int component = 1; component < componentCount; component++) {
		if ((double)stats.at<int>(component, cv::CC_STAT_AREA) > contourSizeThreshold) {
			filteredComponentMask.setTo(255, labels == component);
		}
	}

	backgroundMask.setTo(0, filteredComponentMask == 0);
}

} // namespace

cv::Mat postProcessForegroundMask(const cv::Mat &foregroundMask, const cv::Size &targetSize,
				  const MaskPostProcessingSettings &settings)
{
	cv::Mat resizedForegroundMask = resizeMaskToTarget(foregroundMask, targetSize, cv::INTER_LINEAR);

	cv::Mat backgroundMask;
	if (settings.enableThreshold) {
		backgroundMask =
			makeSoftBackgroundMask(resizedForegroundMask, settings.threshold, settings.edgeSoftness);
		cleanMaskSpecklesWithRust(backgroundMask, settings.foregroundCleanup,
					  settings.foregroundCleanup * 0.5f);
		filterSmallBackgroundComponents(backgroundMask, settings.contourFilter);

		if (settings.smoothContour > 0.0f) {
			const int kernelSize = makeOddKernelSize(3.0f + 11.0f * settings.smoothContour);
			smoothMaskContour(backgroundMask, kernelSize);

			if (settings.edgeSoftness <= 0.0f) {
				backgroundMask = backgroundMask > 128;
			}
		}

		if (settings.maskExpansion > 0) {
			cv::erode(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1), settings.maskExpansion);
		} else if (settings.maskExpansion < 0) {
			cv::dilate(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1),
				   -settings.maskExpansion);
		}

		if (settings.feather > 0.0f) {
			const int kernelSize = makeOddKernelSize(40.0f * settings.feather);
			cv::dilate(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1), kernelSize / 3);
			cv::boxFilter(backgroundMask, backgroundMask, backgroundMask.depth(),
				      cv::Size(kernelSize, kernelSize));
		}
	} else {
		backgroundMask = makeBackgroundMaskWithRust(resizedForegroundMask, false, settings.threshold,
							    settings.edgeSoftness);
	}

	return backgroundMask;
}

cv::Mat smoothTemporalBackgroundMask(const cv::Mat &currentBackgroundMask, const cv::Mat &previousBackgroundMask,
				     const TemporalMaskSmoothingSettings &settings)
{
	if (currentBackgroundMask.empty() || previousBackgroundMask.empty() ||
	    currentBackgroundMask.size() != previousBackgroundMask.size() || currentBackgroundMask.type() != CV_8UC1 ||
	    previousBackgroundMask.type() != CV_8UC1) {
		return currentBackgroundMask.clone();
	}

	const float factor = std::clamp(settings.temporalSmoothFactor, 0.0f, 1.0f);
	if (factor <= 0.0f || factor >= 1.0f) {
		return currentBackgroundMask.clone();
	}

	const cv::Mat currentBackgroundMaskContinuous =
		currentBackgroundMask.isContinuous() ? currentBackgroundMask : currentBackgroundMask.clone();
	const cv::Mat previousBackgroundMaskContinuous =
		previousBackgroundMask.isContinuous() ? previousBackgroundMask : previousBackgroundMask.clone();
	cv::Mat smoothedMask(currentBackgroundMaskContinuous.size(), CV_8UC1);
	const size_t maskSize = currentBackgroundMaskContinuous.total();

	const bgobs_temporal_mask_smoothing_settings rustSettings = {
		factor,
		(uint8_t)(settings.protectForeground ? 1 : 0),
	};

	const bgobs_mask_status status =
		bgobs_write_smooth_temporal_background_mask(currentBackgroundMaskContinuous.ptr<uint8_t>(), maskSize,
							    previousBackgroundMaskContinuous.ptr<uint8_t>(), maskSize,
							    rustSettings, smoothedMask.ptr<uint8_t>(), maskSize);
	CV_Assert(status == BGOBS_MASK_STATUS_OK);

	return smoothedMask;
}

cv::Mat refineBackgroundMaskWithImage(const cv::Mat &backgroundMask, const cv::Mat &sourceImage,
				      const ImageGuidedMaskRefinementSettings &settings)
{
	const float strength = std::clamp(settings.edgeRefinement, 0.0f, 1.0f);
	if (strength <= 0.0f || backgroundMask.empty() || sourceImage.empty() || backgroundMask.type() != CV_8UC1 ||
	    backgroundMask.size() != sourceImage.size() ||
	    (sourceImage.type() != CV_8UC3 && sourceImage.type() != CV_8UC4)) {
		return backgroundMask.clone();
	}

	const int radius = std::clamp((int)std::lround(1.0f + 2.0f * strength), 1, 3);
	const float sigmaColor = std::max(8.0f, 34.0f - 24.0f * strength);
	const float sigmaColorScale = 2.0f * sigmaColor * sigmaColor;
	const float sigmaSpatial = std::max(1.0f, (float)radius * 0.75f);
	const float sigmaSpatialScale = 2.0f * sigmaSpatial * sigmaSpatial;

	cv::Mat gradient;
	cv::morphologyEx(backgroundMask, gradient, cv::MORPH_GRADIENT, cv::Mat());
	cv::Mat edgeBand = gradient > 0;
	if (radius > 1) {
		cv::dilate(edgeBand, edgeBand, cv::Mat(), cv::Point(-1, -1), radius - 1);
	}

	cv::Mat refinedMask = backgroundMask.clone();
	for (int row = 0; row < backgroundMask.rows; row++) {
		for (int col = 0; col < backgroundMask.cols; col++) {
			if (edgeBand.at<uint8_t>(row, col) == 0) {
				continue;
			}

			const PixelColor centerColor = colorAt(sourceImage, row, col);
			float weightedMask = 0.0f;
			float totalWeight = 0.0f;
			for (int dy = -radius; dy <= radius; dy++) {
				const int sampleRow = std::clamp(row + dy, 0, backgroundMask.rows - 1);
				for (int dx = -radius; dx <= radius; dx++) {
					const int sampleCol = std::clamp(col + dx, 0, backgroundMask.cols - 1);
					const float spatialDistance = (float)(dx * dx + dy * dy);
					const float spatialWeight = std::exp(-spatialDistance / sigmaSpatialScale);
					const float colorWeight = std::exp(
						-colorDistanceSquared(centerColor,
								      colorAt(sourceImage, sampleRow, sampleCol)) /
						sigmaColorScale);
					const float weight = spatialWeight * colorWeight;
					weightedMask +=
						(float)backgroundMask.at<uint8_t>(sampleRow, sampleCol) * weight;
					totalWeight += weight;
				}
			}

			if (totalWeight > 0.0f) {
				const float guidedValue = weightedMask / totalWeight;
				const float blended = (float)backgroundMask.at<uint8_t>(row, col) * (1.0f - strength) +
						      guidedValue * strength;
				refinedMask.at<uint8_t>(row, col) =
					(uint8_t)std::clamp((int)std::lround(blended), 0, 255);
			}
		}
	}

	return refinedMask;
}

} // namespace bgobs
