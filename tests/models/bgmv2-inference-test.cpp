// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <obs-module.h>

#include "models/ModelBackgroundMattingV2.hpp"

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

int main(int argc, char **argv)
{
	if (argc != 2 && argc != 4) {
		std::cerr << "usage: bgmv2-inference-test MODEL [WIDTH HEIGHT]\n";
		return EXIT_FAILURE;
	}

	try {
		Ort::Env env(OrtLoggingLevel::ORT_LOGGING_LEVEL_ERROR, "bgobs-bgmv2-test");
		Ort::SessionOptions options;
		options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		options.SetInterOpNumThreads(4);
		options.SetIntraOpNumThreads(4);
		const std::filesystem::path modelPath(argv[1]);
		std::unique_ptr<Ort::Session> session = std::make_unique<Ort::Session>(env, modelPath.c_str(), options);

		ModelBackgroundMattingV2 model;
		std::vector<Ort::AllocatedStringPtr> inputNames;
		std::vector<Ort::AllocatedStringPtr> outputNames;
		std::vector<std::vector<int64_t>> inputDims;
		std::vector<std::vector<int64_t>> outputDims;
		model.populateInputOutputNames(session, inputNames, outputNames);
		if (!model.populateInputOutputShapes(session, inputDims, outputDims) || inputNames.size() != 2 ||
		    outputNames.size() != 2) {
			return EXIT_FAILURE;
		}

		const int width = argc == 4 ? std::stoi(argv[2]) : 640;
		const int height = argc == 4 ? std::stoi(argv[3]) : 360;
		if (width <= 0 || height <= 0) {
			return EXIT_FAILURE;
		}
		model.configureForFrame(inputDims, outputDims, width, height);
		std::vector<std::vector<float>> outputValues;
		std::vector<std::vector<float>> inputValues;
		std::vector<Ort::Value> inputTensors;
		std::vector<Ort::Value> outputTensors;
		model.allocateTensorBuffers(inputDims, outputDims, outputValues, inputValues, inputTensors, outputTensors);

		cv::Mat sourceRGB(height, width, CV_32FC3);
		cv::Mat backgroundRGB(height, width, CV_32FC3, cv::Scalar(0.15f, 0.35f, 0.55f));
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				sourceRGB.at<cv::Vec3f>(y, x) = cv::Vec3f(static_cast<float>(x) / width,
									      static_cast<float>(y) / height, 0.4f);
			}
		}
		cv::Mat sourceCHW, backgroundCHW;
		hwc_to_chw(sourceRGB, sourceCHW);
		hwc_to_chw(backgroundRGB, backgroundCHW);
		model.loadStudioFrames(sourceCHW, backgroundCHW, inputValues);

		constexpr int warmupCount = 3;
		for (int warmup = 0; warmup < warmupCount; ++warmup) {
			model.runNetworkInference(session, inputNames, outputNames, inputTensors, outputTensors);
		}
		std::vector<float> samplesMs;
		constexpr int measuredCount = 30;
		for (int sample = 0; sample < measuredCount; ++sample) {
			const auto started = std::chrono::steady_clock::now();
			model.runNetworkInference(session, inputNames, outputNames, inputTensors, outputTensors);
			samplesMs.push_back(std::chrono::duration<float, std::milli>(
						std::chrono::steady_clock::now() - started)
						.count());
		}
		cv::Mat alpha = model.getNetworkOutput(outputDims, outputValues);
		model.postprocessOutput(alpha);
		cv::Mat foreground = model.getForegroundOutput(outputDims, outputValues);
		if (alpha.rows != height || alpha.cols != width || alpha.type() != CV_32FC1 ||
		    foreground.rows != height || foreground.cols != width || foreground.type() != CV_32FC3 ||
		    !cv::checkRange(alpha, true, nullptr) || !cv::checkRange(foreground, true, nullptr)) {
			std::cerr << "unexpected Background Matting V2 output\n";
			return EXIT_FAILURE;
		}
		std::sort(samplesMs.begin(), samplesMs.end());
		const float averageMs = std::accumulate(samplesMs.begin(), samplesMs.end(), 0.0f) / samplesMs.size();
		const size_t p95Index = (samplesMs.size() * 95 + 99) / 100 - 1;
		std::cout << "Background Matting V2 CPU inference at " << width << 'x' << height << ": " << averageMs
			  << " ms average, " << samplesMs[p95Index] << " ms p95, " << samplesMs.back()
			  << " ms max of " << measuredCount << " (after " << warmupCount << " warmups)\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
