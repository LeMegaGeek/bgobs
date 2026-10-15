// SPDX-License-Identifier: GPL-3.0-or-later

#include <obs-module.h>

#include "models/ModelRVM.hpp"

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "usage: rvm-inference-test MODEL\n";
		return EXIT_FAILURE;
	}

	try {
		Ort::Env env(OrtLoggingLevel::ORT_LOGGING_LEVEL_ERROR, "bgobs-rvm-test");
		Ort::SessionOptions options;
		options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		// Keep the session owned by a unique_ptr as the plugin does.
		const std::filesystem::path modelPath(argv[1]);
		std::unique_ptr<Ort::Session> sessionPtr =
			std::make_unique<Ort::Session>(env, modelPath.c_str(), options);

		ModelRVM model;
		std::vector<Ort::AllocatedStringPtr> inputNames;
		std::vector<Ort::AllocatedStringPtr> outputNames;
		std::vector<std::vector<int64_t>> inputDims;
		std::vector<std::vector<int64_t>> outputDims;
		model.populateInputOutputNames(sessionPtr, inputNames, outputNames);
		if (!model.populateInputOutputShapes(sessionPtr, inputDims, outputDims)) {
			return EXIT_FAILURE;
		}
		model.configureForFrame(inputDims, outputDims, 320, 192, 0.5f);

		std::vector<std::vector<float>> inputValues;
		std::vector<std::vector<float>> outputValues;
		std::vector<Ort::Value> inputTensors;
		std::vector<Ort::Value> outputTensors;
		model.prepareTensorBuffers(inputDims, outputDims, outputValues, inputValues, inputTensors,
					   outputTensors);

		cv::Mat rgb(192, 320, CV_32FC3, cv::Scalar(96.0f, 128.0f, 160.0f));
		cv::Mat chw;
		model.prepareInputToNetwork(rgb, chw);
		model.loadInputToTensor(chw, 320, 192, inputValues);
		std::vector<const char *> rawInputs;
		for (auto &name : inputNames)
			rawInputs.push_back(name.get());
		std::vector<const char *> rawOutputs;
		for (auto &name : outputNames)
			rawOutputs.push_back(name.get());
		sessionPtr->Run(Ort::RunOptions{nullptr}, rawInputs.data(), inputTensors.data(), inputTensors.size(),
					 rawOutputs.data(), outputTensors.data(), outputTensors.size());

		cv::Mat alpha = model.getNetworkOutput(outputDims, outputValues);
		model.postprocessOutput(alpha);
		if (alpha.rows != 192 || alpha.cols != 320 || alpha.type() != CV_32FC1) {
			return EXIT_FAILURE;
		}
		for (int row = 0; row < alpha.rows; ++row) {
			for (int col = 0; col < alpha.cols; ++col) {
				const float value = alpha.at<float>(row, col);
				if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
					return EXIT_FAILURE;
				}
			}
		}

		// Recycle recurrent states once, then change the source size. This is
		// the sequence used by the worker when a camera changes resolution.
		model.assignOutputToInput(outputValues, inputValues);
		for (size_t i = 1; i < 5; ++i)
			inputDims[i] = outputDims[i];
		model.configureForFrame(inputDims, outputDims, 320, 192, 0.5f);
		model.prepareTensorBuffers(inputDims, outputDims, outputValues, inputValues, inputTensors,
					   outputTensors);
		model.loadInputToTensor(chw, 320, 192, inputValues);
		sessionPtr->Run(Ort::RunOptions{nullptr}, rawInputs.data(), inputTensors.data(), inputTensors.size(),
					 rawOutputs.data(), outputTensors.data(), outputTensors.size());

		model.resetRecurrentState();
		model.configureForFrame(inputDims, outputDims, 640, 360, 0.25f);
		model.prepareTensorBuffers(inputDims, outputDims, outputValues, inputValues, inputTensors,
					   outputTensors);
		cv::Mat largerRgb(360, 640, CV_32FC3, cv::Scalar(96.0f, 128.0f, 160.0f));
		model.prepareInputToNetwork(largerRgb, chw);
		model.loadInputToTensor(chw, 640, 360, inputValues);
		sessionPtr->Run(Ort::RunOptions{nullptr}, rawInputs.data(), inputTensors.data(), inputTensors.size(),
					 rawOutputs.data(), outputTensors.data(), outputTensors.size());
		alpha = model.getNetworkOutput(outputDims, outputValues);
		model.postprocessOutput(alpha);
		if (alpha.rows != 360 || alpha.cols != 640 || alpha.type() != CV_32FC1) {
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
