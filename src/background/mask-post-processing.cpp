// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mask-post-processing.hpp"

#include <opencv2/core/version.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>

namespace background_removal {
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

void cleanForegroundHoles(cv::Mat &backgroundMask, float foregroundCleanup)
{
	const float clampedCleanup = std::clamp(foregroundCleanup, 0.0f, 1.0f);
	if (clampedCleanup <= 0.0f) {
		return;
	}

	const int kernelSize = makeOddKernelSize(1.0f + 8.0f * clampedCleanup);
	cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kernelSize, kernelSize));
	cv::morphologyEx(backgroundMask, backgroundMask, cv::MORPH_OPEN, kernel);
}

cv::Mat makeHardBackgroundMask(const cv::Mat &foregroundMask, float threshold)
{
	const uint8_t thresholdValue = (uint8_t)(std::clamp(threshold, 0.0f, 1.0f) * 255.0f);
	return foregroundMask < thresholdValue;
}

cv::Mat makeSoftBackgroundMask(const cv::Mat &foregroundMask, float threshold, float edgeSoftness)
{
	const float clampedThreshold = std::clamp(threshold, 0.0f, 1.0f);
	const float clampedSoftness = std::clamp(edgeSoftness, 0.0f, 1.0f);

	if (clampedSoftness <= 0.0f) {
		return makeHardBackgroundMask(foregroundMask, clampedThreshold);
	}

	const float lower = std::clamp(clampedThreshold - clampedSoftness * 0.5f, 0.0f, 1.0f);
	const float upper = std::clamp(clampedThreshold + clampedSoftness * 0.5f, 0.0f, 1.0f);
	if (upper <= lower) {
		return makeHardBackgroundMask(foregroundMask, clampedThreshold);
	}

	cv::Mat foregroundFloat;
	foregroundMask.convertTo(foregroundFloat, CV_32F, 1.0 / 255.0);

	cv::Mat foregroundAlpha = (foregroundFloat - lower) / (upper - lower);
	cv::max(foregroundAlpha, 0.0, foregroundAlpha);
	cv::min(foregroundAlpha, 1.0, foregroundAlpha);

	cv::Mat smoothStepScale = 3.0 - 2.0 * foregroundAlpha;
	foregroundAlpha = foregroundAlpha.mul(foregroundAlpha).mul(smoothStepScale);

	cv::Mat backgroundMask;
	cv::Mat backgroundFloat = 1.0 - foregroundAlpha;
	backgroundFloat.convertTo(backgroundMask, CV_8U, 255.0);
	return backgroundMask;
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
		cleanForegroundHoles(backgroundMask, settings.foregroundCleanup);
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
		cv::subtract(cv::Scalar(255), resizedForegroundMask, backgroundMask);
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

	const float recoverForegroundFactor =
		settings.protectForeground ? std::clamp(0.65f + 0.35f * factor, factor, 0.98f) : factor;
	const float loseForegroundFactor = settings.protectForeground ? std::clamp(0.35f * factor, 0.05f, factor)
								      : factor;

	cv::Mat smoothedMask(currentBackgroundMask.size(), currentBackgroundMask.type());
	for (int row = 0; row < currentBackgroundMask.rows; row++) {
		const uint8_t *current = currentBackgroundMask.ptr<uint8_t>(row);
		const uint8_t *previous = previousBackgroundMask.ptr<uint8_t>(row);
		uint8_t *smoothed = smoothedMask.ptr<uint8_t>(row);
		for (int col = 0; col < currentBackgroundMask.cols; col++) {
			const float updateFactor = current[col] > previous[col] ? loseForegroundFactor
										: recoverForegroundFactor;
			const float blended =
				(float)current[col] * updateFactor + (float)previous[col] * (1.0f - updateFactor);
			smoothed[col] = (uint8_t)std::clamp((int)std::lround(blended), 0, 255);
		}
	}

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

} // namespace background_removal
