// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FILTERDATA_H
#define FILTERDATA_H

#include <obs-module.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

#include "models/Model.hpp"
#include "ort-utils/ORTModelData.hpp"

/**
  * @brief The filter_data struct
  *
  * This struct is used to store the base data needed for ORT filters.
  *
*/
struct filter_data : public ORTModelData, public std::enable_shared_from_this<filter_data> {
	std::string useGPU;
	uint32_t numThreads = 0;
	std::string modelSelection;
	std::unique_ptr<Model> model;
	// RVM is a full-resolution matting model. The ratio controls only its
	// internal low-resolution backbone; the alpha output remains source-sized.
	std::atomic<float> rvmDownsampleRatio{0.25f};

	obs_source_t *source = nullptr;
	gs_texrender_t *texrender = nullptr;
	gs_stagesurf_t *stagesurface = nullptr;

	cv::Mat inputBGRA;
	uint64_t inputFrameSequence = 0;
	std::chrono::steady_clock::time_point inputFrameCapturedAt{};
	cv::Mat studioBackgroundBGRA;
	cv::Mat studioSourceRGB;
	cv::Mat studioBackgroundRGB;
	cv::Mat studioSourceResizedRGB;
	cv::Mat studioBackgroundResizedRGB;
	cv::Mat studioSourceFloat;
	cv::Mat studioBackgroundFloat;
	cv::Mat studioSourceCHW;
	cv::Mat studioBackgroundCHW;
	std::mutex studioBackgroundMutex;

	std::atomic<bool> isDisabled{false};

	std::mutex inputBGRALock;
	std::mutex outputLock;

#if _WIN32
	std::wstring modelFilepath;
#else
	std::string modelFilepath;
#endif
};

#endif /* FILTERDATA_H */
