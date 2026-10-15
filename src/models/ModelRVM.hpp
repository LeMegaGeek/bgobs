// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MODELRVM_H
#define MODELRVM_H

#include "Model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

class ModelRVM : public ModelBCHW {
private:
	float downsampleRatio = 1.0f;
	bool recurrentStateInitialized = false;
	int configuredWidth = 0;
	int configuredHeight = 0;
	float configuredRatio = 0.0f;

	static int downsampleDimension(int value, float ratio)
	{
		return std::max(1, static_cast<int>(std::floor(static_cast<float>(value) * ratio)));
	}

	static int featureDimension(int value, int divisor)
	{
		return std::max(1, static_cast<int>(std::ceil(static_cast<float>(value) / divisor)));
	}

public:
	ModelRVM(/* args */) {}
	~ModelRVM() {}

	void resetRecurrentState()
	{
		recurrentStateInitialized = false;
		configuredWidth = 0;
		configuredHeight = 0;
		configuredRatio = 0.0f;
	}

	bool hasRecurrentState() const { return recurrentStateInitialized; }

	float getDownsampleRatio() const { return downsampleRatio; }

	/**
	 * Configure the dynamic RVM tensors for a source frame.
	 *
	 * RVM deliberately takes the source at its original resolution. Only its
	 * recurrent backbone is reduced by downsampleRatio; the pha output remains
	 * full resolution. The first recurrent inputs are the [1,1,1,1] zero tensors
	 * documented by the upstream ONNX model. After the first run they are
	 * promoted to the output state shapes by the inference wrapper.
	 */
	void configureForFrame(std::vector<std::vector<int64_t>> &inputDims,
			       std::vector<std::vector<int64_t>> &outputDims, int sourceWidth, int sourceHeight,
			       float ratio)
	{
		downsampleRatio = std::clamp(ratio, 0.05f, 1.0f);
		const int smallWidth = downsampleDimension(sourceWidth, downsampleRatio);
		const int smallHeight = downsampleDimension(sourceHeight, downsampleRatio);

		const bool tensorLayoutChanged = configuredWidth != sourceWidth || configuredHeight != sourceHeight ||
						 std::abs(configuredRatio - downsampleRatio) > 0.000001f;
		if (tensorLayoutChanged) {
			recurrentStateInitialized = false;
		}
		configuredWidth = sourceWidth;
		configuredHeight = sourceHeight;
		configuredRatio = downsampleRatio;

		inputDims.resize(6);
		outputDims.resize(6);
		inputDims[0] = {1, 3, sourceHeight, sourceWidth};
		inputDims[5] = {1};
		for (size_t i = 1; i < 5; ++i) {
			const size_t stateOutput = i + 1;
			if (recurrentStateInitialized && stateOutput < outputDims.size() &&
			    outputDims[stateOutput].size() == 4) {
				inputDims[i] = outputDims[stateOutput];
			} else {
				inputDims[i] = {1, 1, 1, 1};
			}
		}

		outputDims[0] = {1, 3, sourceHeight, sourceWidth};
		outputDims[1] = {1, 1, sourceHeight, sourceWidth};
		constexpr int stateChannels[] = {16, 20, 40, 64};
		for (size_t i = 1; i < 5; ++i) {
			const int divisor = 2 << (static_cast<int>(i) - 1);
			outputDims[i + 1] = {1, static_cast<int64_t>(stateChannels[i - 1]),
					     featureDimension(smallHeight, divisor),
					     featureDimension(smallWidth, divisor)};
		}
	}

	/** Rebuild tensor views while preserving recurrent state vectors. */
	void prepareTensorBuffers(const std::vector<std::vector<int64_t>> &inputDims,
				  const std::vector<std::vector<int64_t>> &outputDims,
				  std::vector<std::vector<float>> &outputTensorValues,
				  std::vector<std::vector<float>> &inputTensorValues,
				  std::vector<Ort::Value> &inputTensor, std::vector<Ort::Value> &outputTensor)
	{
		Ort::MemoryInfo memoryInfo =
			Ort::MemoryInfo::CreateCpu(OrtAllocatorType::OrtDeviceAllocator, OrtMemType::OrtMemTypeDefault);

		if (inputTensorValues.size() != inputDims.size()) {
			inputTensorValues.resize(inputDims.size());
		}
		for (size_t i = 0; i < inputDims.size(); ++i) {
			const size_t required = vectorProduct(inputDims[i]);
			if (inputTensorValues[i].size() != required) {
				inputTensorValues[i].assign(required, 0.0f);
			}
		}

		outputTensorValues.resize(outputDims.size());
		for (size_t i = 0; i < outputDims.size(); ++i) {
			const size_t required = vectorProduct(outputDims[i]);
			if (outputTensorValues[i].size() != required) {
				outputTensorValues[i].assign(required, 0.0f);
			}
		}

		inputTensor.clear();
		for (size_t i = 0; i < inputDims.size(); ++i) {
			inputTensor.push_back(Ort::Value::CreateTensor<float>(
				memoryInfo, inputTensorValues[i].data(), inputTensorValues[i].size(),
				inputDims[i].data(), inputDims[i].size()));
		}
		outputTensor.clear();
		for (size_t i = 0; i < outputDims.size(); ++i) {
			outputTensor.push_back(Ort::Value::CreateTensor<float>(
				memoryInfo, outputTensorValues[i].data(), outputTensorValues[i].size(),
				outputDims[i].data(), outputDims[i].size()));
		}
	}

	void markRecurrentStateInitialized() { recurrentStateInitialized = true; }

	virtual void populateInputOutputNames(const std::unique_ptr<Ort::Session> &session,
					      std::vector<Ort::AllocatedStringPtr> &inputNames,
					      std::vector<Ort::AllocatedStringPtr> &outputNames)
	{
		Ort::AllocatorWithDefaultOptions allocator;

		inputNames.clear();
		outputNames.clear();

		for (size_t i = 0; i < session->GetInputCount(); i++) {
			inputNames.push_back(session->GetInputNameAllocated(i, allocator));
		}
		for (size_t i = 0; i < session->GetOutputCount(); i++) {
			outputNames.push_back(session->GetOutputNameAllocated(i, allocator));
		}
	}

	virtual bool populateInputOutputShapes(const std::unique_ptr<Ort::Session> &session,
					       std::vector<std::vector<int64_t>> &inputDims,
					       std::vector<std::vector<int64_t>> &outputDims)
	{
		// RVM exposes six inputs and outputs: source, four recurrent states,
		// ratio; then reconstructed foreground, alpha, and four recurrent states.

		inputDims.clear();
		outputDims.clear();

		for (size_t i = 0; i < session->GetInputCount(); i++) {
			// Get input shape
			const Ort::TypeInfo inputTypeInfo = session->GetInputTypeInfo(i);
			const auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
			inputDims.push_back(inputTensorInfo.GetShape());
		}

		for (size_t i = 0; i < session->GetOutputCount(); i++) {
			// Get output shape
			const Ort::TypeInfo outputTypeInfo = session->GetOutputTypeInfo(i);
			const auto outputTensorInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
			outputDims.push_back(outputTensorInfo.GetShape());
		}
		if (session->GetInputCount() != 6 || session->GetOutputCount() != 6) {
			obs_log(LOG_ERROR, "RVM model must expose 6 inputs and 6 outputs (got %zu and %zu)",
				session->GetInputCount(), session->GetOutputCount());
			return false;
		}

		// The ONNX model exposes dynamic spatial dimensions. Keep the initial
		// tensors minimal; configureForFrame() selects the source and backbone
		// dimensions immediately before the first inference.
		inputDims = {{1, 3, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1}};
		outputDims = {{1, 3, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}};
		return true;
	}

	virtual cv::Mat getNetworkOutput(const std::vector<std::vector<int64_t>> &outputDims,
					 std::vector<std::vector<float>> &outputTensorValues)
	{
		// The shared inference entry point consumes alpha. RVM's reconstructed
		// foreground is available separately through getForegroundOutput().
		if (outputDims.size() < 2 || outputTensorValues.size() < 2) {
			return {};
		}
		return cv::Mat(static_cast<int>(outputDims[1][2]), static_cast<int>(outputDims[1][3]), CV_32FC1,
			       outputTensorValues[1].data());
	}

	cv::Mat getForegroundOutput(const std::vector<std::vector<int64_t>> &outputDims,
				    std::vector<std::vector<float>> &outputTensorValues)
	{
		if (outputDims.empty() || outputTensorValues.empty()) {
			return {};
		}
		const int height = static_cast<int>(outputDims[0][2]);
		const int width = static_cast<int>(outputDims[0][3]);
		const size_t plane = static_cast<size_t>(height) * width;
		const float *planes = outputTensorValues[0].data();
		cv::Mat foreground(height, width, CV_32FC3);
		for (int y = 0; y < height; ++y) {
			cv::Vec3f *row = foreground.ptr<cv::Vec3f>(y);
			for (int x = 0; x < width; ++x) {
				const size_t index = static_cast<size_t>(y) * width + x;
				row[x] = cv::Vec3f(planes[index], planes[plane + index], planes[2 * plane + index]);
			}
		}
		cv::min(foreground, 1.0f, foreground);
		cv::max(foreground, 0.0f, foreground);
		return foreground;
	}

	virtual void loadInputToTensor(const cv::Mat &preprocessedImage, uint32_t, uint32_t,
				       std::vector<std::vector<float>> &inputTensorValues)
	{
		std::copy(preprocessedImage.begin<float>(), preprocessedImage.end<float>(),
			  inputTensorValues[0].begin());
		inputTensorValues[5][0] = downsampleRatio;
	}

	virtual void assignOutputToInput(std::vector<std::vector<float>> &outputTensorValues,
					 std::vector<std::vector<float>> &inputTensorValues)
	{
		for (size_t i = 1; i < 5; i++) {
			inputTensorValues[i].assign(outputTensorValues[i + 1].begin(), outputTensorValues[i + 1].end());
		}
		recurrentStateInitialized = true;
	}
};

#endif /* MODELRVM_H */
