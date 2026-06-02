// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "background/mask-post-processing.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <iostream>

namespace {

bool expectPixel(const cv::Mat &image, int row, int col, int expected, const char *label)
{
	const int actual = image.at<uint8_t>(row, col);
	if (actual == expected) {
		return true;
	}

	std::cerr << label << ": expected " << expected << ", got " << actual << "\n";
	return false;
}

bool expectRange(const cv::Mat &image, int row, int col, int minValue, int maxValue, const char *label)
{
	const int actual = image.at<uint8_t>(row, col);
	if (actual >= minValue && actual <= maxValue) {
		return true;
	}

	std::cerr << label << ": expected value in [" << minValue << ", " << maxValue << "], got " << actual << "\n";
	return false;
}

background_removal::MaskPostProcessingSettings baseSettings()
{
	background_removal::MaskPostProcessingSettings settings;
	settings.enableThreshold = true;
	settings.threshold = 0.5f;
	settings.edgeSoftness = 0.0f;
	settings.foregroundCleanup = 0.0f;
	settings.contourFilter = 0.0f;
	settings.smoothContour = 0.0f;
	settings.feather = 0.0f;
	settings.maskExpansion = 0;
	return settings;
}

} // namespace

int main()
{
	cv::Mat foreground(1, 5, CV_8UC1);
	foreground.at<uint8_t>(0, 0) = 0;
	foreground.at<uint8_t>(0, 1) = 64;
	foreground.at<uint8_t>(0, 2) = 128;
	foreground.at<uint8_t>(0, 3) = 192;
	foreground.at<uint8_t>(0, 4) = 255;

	auto settings = baseSettings();
	cv::Mat hardMask = background_removal::postProcessForegroundMask(foreground, foreground.size(), settings);
	bool success = true;
	success &= expectPixel(hardMask, 0, 0, 255, "hard background low confidence");
	success &= expectPixel(hardMask, 0, 1, 255, "hard background below threshold");
	success &= expectPixel(hardMask, 0, 2, 0, "hard foreground at threshold");
	success &= expectPixel(hardMask, 0, 3, 0, "hard foreground above threshold");
	success &= expectPixel(hardMask, 0, 4, 0, "hard foreground high confidence");

	settings.edgeSoftness = 0.5f;
	cv::Mat softMask = background_removal::postProcessForegroundMask(foreground, foreground.size(), settings);
	success &= expectPixel(softMask, 0, 0, 255, "soft background outside transition");
	success &= expectRange(softMask, 0, 2, 1, 254, "soft alpha inside transition");
	success &= expectPixel(softMask, 0, 4, 0, "soft foreground outside transition");

	cv::Mat resizedMask = background_removal::postProcessForegroundMask(foreground, cv::Size(10, 3), settings);
	if (resizedMask.size() != cv::Size(10, 3)) {
		std::cerr << "resize target: expected 10x3, got " << resizedMask.cols << "x" << resizedMask.rows
			  << "\n";
		success = false;
	}

	settings = baseSettings();
	settings.enableThreshold = false;
	cv::Mat noThresholdMask =
		background_removal::postProcessForegroundMask(foreground, foreground.size(), settings);
	success &= expectPixel(noThresholdMask, 0, 0, 255, "no-threshold background");
	success &= expectPixel(noThresholdMask, 0, 2, 127, "no-threshold alpha inversion");
	success &= expectPixel(noThresholdMask, 0, 4, 0, "no-threshold foreground");

	cv::Mat foregroundWithSpeck = cv::Mat(5, 5, CV_8UC1, cv::Scalar(255));
	foregroundWithSpeck.row(0).setTo(0);
	foregroundWithSpeck.at<uint8_t>(2, 2) = 0;
	settings = baseSettings();
	settings.contourFilter = 0.1f;
	cv::Mat filteredSpeckMask = background_removal::postProcessForegroundMask(foregroundWithSpeck,
										  foregroundWithSpeck.size(), settings);
	success &= expectPixel(filteredSpeckMask, 0, 0, 255, "large background component");
	success &= expectPixel(filteredSpeckMask, 2, 2, 0, "small background component");

	cv::Mat foregroundWithHole = cv::Mat(7, 7, CV_8UC1, cv::Scalar(255));
	foregroundWithHole.at<uint8_t>(3, 3) = 0;
	settings = baseSettings();
	settings.foregroundCleanup = 0.15f;
	cv::Mat cleanedHoleMask =
		background_removal::postProcessForegroundMask(foregroundWithHole, foregroundWithHole.size(), settings);
	success &= expectPixel(cleanedHoleMask, 3, 3, 0, "foreground cleanup removes background pinhole");

	cv::Mat previousBackground(1, 2, CV_8UC1);
	previousBackground.at<uint8_t>(0, 0) = 64;
	previousBackground.at<uint8_t>(0, 1) = 192;
	cv::Mat currentBackground(1, 2, CV_8UC1);
	currentBackground.at<uint8_t>(0, 0) = 192;
	currentBackground.at<uint8_t>(0, 1) = 64;
	background_removal::TemporalMaskSmoothingSettings temporalSettings;
	temporalSettings.temporalSmoothFactor = 0.8f;
	cv::Mat temporalMask = background_removal::smoothTemporalBackgroundMask(currentBackground, previousBackground,
										temporalSettings);
	success &= expectRange(temporalMask, 0, 0, 95, 105, "temporal smoothing protects foreground from loss");
	success &= expectRange(temporalMask, 0, 1, 68, 78, "temporal smoothing recovers foreground quickly");

	cv::Mat hardBoundaryMask(1, 5, CV_8UC1);
	hardBoundaryMask.at<uint8_t>(0, 0) = 0;
	hardBoundaryMask.at<uint8_t>(0, 1) = 0;
	hardBoundaryMask.at<uint8_t>(0, 2) = 255;
	hardBoundaryMask.at<uint8_t>(0, 3) = 255;
	hardBoundaryMask.at<uint8_t>(0, 4) = 255;
	background_removal::ImageGuidedMaskRefinementSettings refinementSettings;
	refinementSettings.edgeRefinement = 1.0f;

	cv::Mat flatColorImage(1, 5, CV_8UC3, cv::Scalar(64, 64, 64));
	cv::Mat flatRefinedMask =
		background_removal::refineBackgroundMaskWithImage(hardBoundaryMask, flatColorImage, refinementSettings);
	success &= expectRange(flatRefinedMask, 0, 1, 80, 130, "edge refinement softens weak visual edge");
	success &= expectRange(flatRefinedMask, 0, 2, 135, 180, "edge refinement blends weak visual edge");

	cv::Mat strongEdgeImage(1, 5, CV_8UC3, cv::Scalar(255, 255, 255));
	strongEdgeImage.at<cv::Vec3b>(0, 0) = cv::Vec3b(0, 0, 0);
	strongEdgeImage.at<cv::Vec3b>(0, 1) = cv::Vec3b(0, 0, 0);
	cv::Mat strongEdgeRefinedMask = background_removal::refineBackgroundMaskWithImage(
		hardBoundaryMask, strongEdgeImage, refinementSettings);
	success &= expectRange(strongEdgeRefinedMask, 0, 1, 0, 5, "edge refinement preserves strong visual edge");
	success &= expectRange(strongEdgeRefinedMask, 0, 2, 250, 255, "edge refinement preserves foreground side");

	return success ? 0 : 1;
}
