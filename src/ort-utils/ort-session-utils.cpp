// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <onnxruntime_cxx_api.h>
#include <cpu_provider_factory.h>
#include <filesystem>

#ifdef HAVE_ONNXRUNTIME_DML_EP
#include <dml_provider_factory.h>
#endif

#if defined(__APPLE__)
#include <coreml_provider_factory.h>
#endif

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#endif // _WIN32

#include <obs-module.h>

#include "ort-session-utils.hpp"
#include "../consts.h"
#include "../models/ModelBackgroundMattingV2.hpp"
#include "../models/ModelRVM.hpp"
#include "../plugin-support.h"

#include <algorithm>
#include <cmath>

namespace {

bool runRvmInference(filter_data *tf, const cv::Mat &imageBGRA, cv::Mat &output, cv::Mat *reconstructedForegroundBGRA)
{
	auto *rvm = dynamic_cast<ModelRVM *>(tf->model.get());
	if (!rvm || imageBGRA.empty() || imageBGRA.type() != CV_8UC4) {
		return false;
	}

	const int sourceWidth = imageBGRA.cols;
	const int sourceHeight = imageBGRA.rows;
	rvm->configureForFrame(tf->inputDims, tf->outputDims, sourceWidth, sourceHeight, tf->rvmDownsampleRatio.load());
	rvm->prepareTensorBuffers(tf->inputDims, tf->outputDims, tf->outputTensorValues, tf->inputTensorValues,
				  tf->inputTensor, tf->outputTensor);

	// RVM expects RGB BCHW data in [0, 1] at the original source resolution.
	// Do not resize the source here: the model's downsample_ratio is applied
	// internally before its high-resolution guided refinement stage.
	cv::Mat imageRGB;
	cv::cvtColor(imageBGRA, imageRGB, cv::COLOR_BGRA2RGB);
	cv::Mat imageFloat;
	imageRGB.convertTo(imageFloat, CV_32F);
	cv::Mat preprocessedImage;
	rvm->prepareInputToNetwork(imageFloat, preprocessedImage);
	rvm->loadInputToTensor(preprocessedImage, static_cast<uint32_t>(sourceWidth),
			       static_cast<uint32_t>(sourceHeight), tf->inputTensorValues);

	rvm->runNetworkInference(tf->session, tf->inputNames, tf->outputNames, tf->inputTensor, tf->outputTensor);

	cv::Mat outputImage = rvm->getNetworkOutput(tf->outputDims, tf->outputTensorValues);
	rvm->postprocessOutput(outputImage);
	if (outputImage.empty() || outputImage.type() != CV_32FC1 || outputImage.cols != sourceWidth ||
	    outputImage.rows != sourceHeight) {
		obs_log(LOG_ERROR, "RVM returned an invalid alpha shape: %dx%d type %d (expected %dx%d CV_32FC1)",
			outputImage.cols, outputImage.rows, outputImage.type(), sourceWidth, sourceHeight);
		return false;
	}
	output = outputImage.clone();
	if (reconstructedForegroundBGRA) {
		cv::Mat foregroundRGB = rvm->getForegroundOutput(tf->outputDims, tf->outputTensorValues);
		if (foregroundRGB.empty() || foregroundRGB.type() != CV_32FC3 || foregroundRGB.cols != sourceWidth ||
		    foregroundRGB.rows != sourceHeight) {
			obs_log(LOG_ERROR, "RVM returned an invalid reconstructed foreground");
			return false;
		}
		cv::Mat foregroundRGB8;
		foregroundRGB.convertTo(foregroundRGB8, CV_8UC3, 255.0);
		cv::cvtColor(foregroundRGB8, *reconstructedForegroundBGRA, cv::COLOR_RGB2BGRA);
	}

	// Cycle the recurrent states. The first frame uses zero [1,1,1,1] inputs;
	// subsequent frames use the output state tensors at their native shapes.
	rvm->assignOutputToInput(tf->outputTensorValues, tf->inputTensorValues);
	for (size_t i = 1; i < 5 && i < tf->inputDims.size() && i < tf->outputDims.size(); ++i) {
		tf->inputDims[i] = tf->outputDims[i];
	}
	return true;
}

bool runBackgroundMattingV2Inference(filter_data *tf, const cv::Mat &imageBGRA, cv::Mat &output,
				     cv::Mat *reconstructedForegroundBGRA)
{
	auto *model = dynamic_cast<ModelBackgroundMattingV2 *>(tf->model.get());
	if (!model || imageBGRA.empty() || imageBGRA.type() != CV_8UC4) {
		return false;
	}

	cv::Mat backgroundBGRA;
	{
		std::lock_guard<std::mutex> lock(tf->studioBackgroundMutex);
		backgroundBGRA = tf->studioBackgroundBGRA;
	}
	if (backgroundBGRA.empty() || backgroundBGRA.size() != imageBGRA.size()) {
		return false;
	}

	// BMv2's HD export consumes source and background as RGB BCHW tensors in
	// [0, 1]. Keep HD untouched, cap larger sources, and upscale tiny inputs so
	// the export's fixed TopK operation has at least 5,000 spatial candidates.
	const int largestSide = std::max(imageBGRA.cols, imageBGRA.rows);
	const double minimumInputScale = std::sqrt(100000.0 / (static_cast<double>(imageBGRA.cols) * imageBGRA.rows));
	const double scale =
		std::min(largestSide > 1920 ? 1920.0 / largestSide : 1.0, std::max(1.0, minimumInputScale));
	const cv::Size modelSize(std::max(1, static_cast<int>(std::lround(imageBGRA.cols * scale))),
				 std::max(1, static_cast<int>(std::lround(imageBGRA.rows * scale))));
	cv::Mat &sourceRGB = tf->studioSourceRGB;
	cv::Mat &backgroundRGB = tf->studioBackgroundRGB;
	cv::cvtColor(imageBGRA, sourceRGB, cv::COLOR_BGRA2RGB);
	cv::cvtColor(backgroundBGRA, backgroundRGB, cv::COLOR_BGRA2RGB);
	cv::Mat *modelSourceRGB = &sourceRGB;
	cv::Mat *modelBackgroundRGB = &backgroundRGB;
	if (modelSize != sourceRGB.size()) {
		const int interpolation = scale > 1.0 ? cv::INTER_CUBIC : cv::INTER_AREA;
		cv::resize(sourceRGB, tf->studioSourceResizedRGB, modelSize, 0.0, 0.0, interpolation);
		cv::resize(backgroundRGB, tf->studioBackgroundResizedRGB, modelSize, 0.0, 0.0, interpolation);
		modelSourceRGB = &tf->studioSourceResizedRGB;
		modelBackgroundRGB = &tf->studioBackgroundResizedRGB;
	}
	cv::Mat &sourceFloat = tf->studioSourceFloat;
	cv::Mat &backgroundFloat = tf->studioBackgroundFloat;
	cv::Mat &sourceCHW = tf->studioSourceCHW;
	cv::Mat &backgroundCHW = tf->studioBackgroundCHW;
	modelSourceRGB->convertTo(sourceFloat, CV_32F, 1.0 / 255.0);
	modelBackgroundRGB->convertTo(backgroundFloat, CV_32F, 1.0 / 255.0);
	hwc_to_chw(sourceFloat, sourceCHW);
	hwc_to_chw(backgroundFloat, backgroundCHW);

	const std::vector<std::vector<int64_t>> expectedInputDims = {{1, 3, modelSize.height, modelSize.width},
								     {1, 3, modelSize.height, modelSize.width}};
	const std::vector<std::vector<int64_t>> expectedOutputDims = {{1, 1, modelSize.height, modelSize.width},
								      {1, 3, modelSize.height, modelSize.width}};
	const bool tensorShapeChanged = tf->inputDims != expectedInputDims || tf->outputDims != expectedOutputDims;
	model->configureForFrame(tf->inputDims, tf->outputDims, modelSize.width, modelSize.height);
	if (tensorShapeChanged || tf->inputTensor.empty() || tf->outputTensor.empty() ||
	    tf->inputTensorValues.size() != tf->inputDims.size() ||
	    tf->outputTensorValues.size() != tf->outputDims.size()) {
		model->allocateTensorBuffers(tf->inputDims, tf->outputDims, tf->outputTensorValues,
					     tf->inputTensorValues, tf->inputTensor, tf->outputTensor);
	}
	model->loadStudioFrames(sourceCHW, backgroundCHW, tf->inputTensorValues);
	model->runNetworkInference(tf->session, tf->inputNames, tf->outputNames, tf->inputTensor, tf->outputTensor);

	cv::Mat alpha = model->getNetworkOutput(tf->outputDims, tf->outputTensorValues);
	model->postprocessOutput(alpha);
	if (alpha.empty() || alpha.type() != CV_32FC1 || alpha.size() != modelSize) {
		obs_log(LOG_ERROR, "Background Matting V2 returned an invalid alpha shape");
		return false;
	}
	if (modelSize != imageBGRA.size()) {
		cv::resize(alpha, alpha, imageBGRA.size(), 0.0, 0.0, cv::INTER_LINEAR);
	}
	output = alpha.clone();

	if (reconstructedForegroundBGRA) {
		cv::Mat foregroundRGB = model->getForegroundOutput(tf->outputDims, tf->outputTensorValues);
		if (foregroundRGB.empty() || foregroundRGB.type() != CV_32FC3 || foregroundRGB.size() != modelSize) {
			obs_log(LOG_ERROR, "Background Matting V2 returned an invalid foreground shape");
			return false;
		}
		if (modelSize != imageBGRA.size()) {
			cv::resize(foregroundRGB, foregroundRGB, imageBGRA.size(), 0.0, 0.0, cv::INTER_LINEAR);
		}
		cv::Mat foregroundRGB8;
		foregroundRGB.convertTo(foregroundRGB8, CV_8UC3, 255.0);
		cv::cvtColor(foregroundRGB8, *reconstructedForegroundBGRA, cv::COLOR_RGB2BGRA);
	}
	return true;
}

bool runGenericInferenceFloat(filter_data *tf, const cv::Mat &imageBGRA, cv::Mat &output)
{
	if (tf->session.get() == nullptr || tf->model.get() == nullptr) {
		return false;
	}

	// To RGB
	cv::Mat imageRGB;
	cv::cvtColor(imageBGRA, imageRGB, cv::COLOR_BGRA2RGB);

	// Letterbox segmentation/depth models instead of stretching a 16:9 camera
	// into a square network input. The inverse transform below restores the
	// model output to the camera aspect ratio before post-processing.
	uint32_t inputWidth, inputHeight;
	tf->model->getNetworkInputSize(tf->inputDims, inputWidth, inputHeight);

	const double scale = std::min(static_cast<double>(inputWidth) / imageRGB.cols,
				      static_cast<double>(inputHeight) / imageRGB.rows);
	const int scaledWidth =
		std::clamp(static_cast<int>(std::lround(imageRGB.cols * scale)), 1, static_cast<int>(inputWidth));
	const int scaledHeight =
		std::clamp(static_cast<int>(std::lround(imageRGB.rows * scale)), 1, static_cast<int>(inputHeight));
	const int padLeft = (static_cast<int>(inputWidth) - scaledWidth) / 2;
	const int padTop = (static_cast<int>(inputHeight) - scaledHeight) / 2;
	cv::Mat resizedImageRGB;
	cv::resize(imageRGB, resizedImageRGB, cv::Size(scaledWidth, scaledHeight), 0.0, 0.0, cv::INTER_LINEAR);
	if (scaledWidth != static_cast<int>(inputWidth) || scaledHeight != static_cast<int>(inputHeight)) {
		cv::copyMakeBorder(resizedImageRGB, resizedImageRGB, padTop,
				   static_cast<int>(inputHeight) - scaledHeight - padTop, padLeft,
				   static_cast<int>(inputWidth) - scaledWidth - padLeft, cv::BORDER_REPLICATE);
	}

	// Prepare input to network
	cv::Mat resizedImage, preprocessedImage;
	resizedImageRGB.convertTo(resizedImage, CV_32F);

	tf->model->prepareInputToNetwork(resizedImage, preprocessedImage);

	tf->model->loadInputToTensor(preprocessedImage, inputWidth, inputHeight, tf->inputTensorValues);

	// Run network inference
	tf->model->runNetworkInference(tf->session, tf->inputNames, tf->outputNames, tf->inputTensor, tf->outputTensor);

	// Get output
	cv::Mat outputImage = tf->model->getNetworkOutput(tf->outputDims, tf->outputTensorValues);

	// Assign output to input in some models that have temporal information
	tf->model->assignOutputToInput(tf->outputTensorValues, tf->inputTensorValues);

	// Post-process output. The image will now be in [0,1] float, BHWC format.
	tf->model->postprocessOutput(outputImage);
	if (outputImage.cols == static_cast<int>(inputWidth) && outputImage.rows == static_cast<int>(inputHeight) &&
	    (scaledWidth != static_cast<int>(inputWidth) || scaledHeight != static_cast<int>(inputHeight))) {
		const int outputPadLeft =
			static_cast<int>(std::lround(padLeft * outputImage.cols / static_cast<double>(inputWidth)));
		const int outputPadTop =
			static_cast<int>(std::lround(padTop * outputImage.rows / static_cast<double>(inputHeight)));
		const int outputWidth = std::max(1, static_cast<int>(std::lround(scaledWidth * outputImage.cols /
										 static_cast<double>(inputWidth))));
		const int outputHeight = std::max(1, static_cast<int>(std::lround(scaledHeight * outputImage.rows /
										  static_cast<double>(inputHeight))));
		const cv::Rect roi(
			std::clamp(outputPadLeft, 0, outputImage.cols - 1),
			std::clamp(outputPadTop, 0, outputImage.rows - 1),
			std::min(outputWidth, outputImage.cols - std::clamp(outputPadLeft, 0, outputImage.cols - 1)),
			std::min(outputHeight, outputImage.rows - std::clamp(outputPadTop, 0, outputImage.rows - 1)));
		cv::Mat cropped = outputImage(roi);
		cv::resize(cropped, output, imageRGB.size(), 0.0, 0.0, cv::INTER_LINEAR);
	} else {
		output = outputImage.clone();
	}
	return true;
}

} // namespace

int createOrtSession(filter_data *tf)
{
	if (tf->model.get() == nullptr) {
		obs_log(LOG_ERROR, "Model object is not initialized");
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_MODEL;
	}

	Ort::SessionOptions sessionOptions;

	sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
	if (tf->useGPU != USEGPU_CPU && tf->useGPU != USEGPU_AUTO) {
		sessionOptions.DisableMemPattern();
		sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
	} else {
		sessionOptions.SetInterOpNumThreads(tf->numThreads);
		sessionOptions.SetIntraOpNumThreads(tf->numThreads);
	}

	char *modelFilepath_rawPtr = obs_module_file(tf->modelSelection.c_str());

	if (modelFilepath_rawPtr == nullptr) {
		obs_log(LOG_ERROR, "Unable to get model filename %s from plugin.", tf->modelSelection.c_str());
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_FILE_NOT_FOUND;
	}

	std::string modelFilepath_s(modelFilepath_rawPtr);

#if _WIN32
	int outLength = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, modelFilepath_rawPtr, -1, nullptr, 0);
	tf->modelFilepath = std::wstring(outLength, L'\0');
	MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, modelFilepath_rawPtr, -1, tf->modelFilepath.data(), outLength);
#else
	tf->modelFilepath = std::string(modelFilepath_rawPtr);
#endif

	bfree(modelFilepath_rawPtr);

	try {
#ifdef HAVE_ONNXRUNTIME_DML_EP
		if (tf->useGPU == USEGPU_DIRECTML) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(sessionOptions, 0));
		}
#endif
#ifdef HAVE_ONNXRUNTIME_CUDA_EP
		if (tf->useGPU == USEGPU_CUDA) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CUDA(sessionOptions, 0));
		}
#endif
#ifdef HAVE_ONNXRUNTIME_ROCM_EP
		if (tf->useGPU == USEGPU_ROCM) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_ROCM(sessionOptions, 0));
		}
#endif
#ifdef HAVE_ONNXRUNTIME_MIGRAPHX_EP
		if (tf->useGPU == USEGPU_MIGRAPHX) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_MIGraphX(sessionOptions, 0));
		}
#endif
#ifdef HAVE_ONNXRUNTIME_TENSORRT_EP
		if (tf->useGPU == USEGPU_TENSORRT) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_Tensorrt(sessionOptions, 0));
		}
#endif
#if defined(__APPLE__)
		if (tf->useGPU == USEGPU_COREML) {
			uint32_t coreml_flags = 0;
			coreml_flags |= COREML_FLAG_ENABLE_ON_SUBGRAPH;
			Ort::ThrowOnError(
				OrtSessionOptionsAppendExecutionProvider_CoreML(sessionOptions, coreml_flags));
		}
#endif
		tf->session.reset(new Ort::Session(*tf->env, tf->modelFilepath.c_str(), sessionOptions));
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "%s", e.what());
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP;
	}

	Ort::AllocatorWithDefaultOptions allocator;

	tf->model->populateInputOutputNames(tf->session, tf->inputNames, tf->outputNames);

	if (!tf->model->populateInputOutputShapes(tf->session, tf->inputDims, tf->outputDims)) {
		obs_log(LOG_ERROR, "Unable to get model input and output shapes");
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_INPUT_OUTPUT;
	}

	for (size_t i = 0; i < tf->inputNames.size(); i++) {
		obs_log(LOG_INFO, "Model %s input %d: name %s shape (%d dim) %d x %d x %d x %d",
			tf->modelSelection.c_str(), (int)i, tf->inputNames[i].get(), (int)tf->inputDims[i].size(),
			(int)tf->inputDims[i][0], ((int)tf->inputDims[i].size() > 1) ? (int)tf->inputDims[i][1] : 0,
			((int)tf->inputDims[i].size() > 2) ? (int)tf->inputDims[i][2] : 0,
			((int)tf->inputDims[i].size() > 3) ? (int)tf->inputDims[i][3] : 0);
	}
	for (size_t i = 0; i < tf->outputNames.size(); i++) {
		obs_log(LOG_INFO, "Model %s output %d: name %s shape (%d dim) %d x %d x %d x %d",
			tf->modelSelection.c_str(), (int)i, tf->outputNames[i].get(), (int)tf->outputDims[i].size(),
			(int)tf->outputDims[i][0], ((int)tf->outputDims[i].size() > 1) ? (int)tf->outputDims[i][1] : 0,
			((int)tf->outputDims[i].size() > 2) ? (int)tf->outputDims[i][2] : 0,
			((int)tf->outputDims[i].size() > 3) ? (int)tf->outputDims[i][3] : 0);
	}

	// Allocate buffers
	tf->model->allocateTensorBuffers(tf->inputDims, tf->outputDims, tf->outputTensorValues, tf->inputTensorValues,
					 tf->inputTensor, tf->outputTensor);

	return OBS_BGREMOVAL_ORT_SESSION_SUCCESS;
}

bool runFilterModelInference(filter_data *tf, const cv::Mat &imageBGRA, cv::Mat &output)
{
	cv::Mat floatOutput;
	if (!runFilterModelInferenceFloat(tf, imageBGRA, floatOutput)) {
		return false;
	}
	floatOutput.convertTo(output, CV_8U, 255.0);
	return true;
}

bool runFilterModelInferenceFloat(filter_data *tf, const cv::Mat &imageBGRA, cv::Mat &output,
				  cv::Mat *reconstructedForegroundBGRA)
{
	if (tf == nullptr || tf->session.get() == nullptr || tf->model.get() == nullptr || imageBGRA.empty()) {
		return false;
	}
	if (tf->modelSelection == MODEL_RVM) {
		return runRvmInference(tf, imageBGRA, output, reconstructedForegroundBGRA);
	}
	if (tf->modelSelection == MODEL_BGMV2) {
		return runBackgroundMattingV2Inference(tf, imageBGRA, output, reconstructedForegroundBGRA);
	}
	if (reconstructedForegroundBGRA) {
		*reconstructedForegroundBGRA = imageBGRA.clone();
	}
	return runGenericInferenceFloat(tf, imageBGRA, output);
}

void resetFilterModelState(filter_data *tf)
{
	if (!tf || !tf->model) {
		return;
	}
	if (auto *rvm = dynamic_cast<ModelRVM *>(tf->model.get())) {
		rvm->resetRecurrentState();
	}
}
