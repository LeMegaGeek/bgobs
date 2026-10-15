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
				       std::vector<std::vector<int64_t>> &outputDims,
				       int sourceWidth, int sourceHeight, float ratio)
	{
		downsampleRatio = std::clamp(ratio, 0.05f, 1.0f);
		const int smallWidth = downsampleDimension(sourceWidth, downsampleRatio);
		const int smallHeight = downsampleDimension(sourceHeight, downsampleRatio);

		const bool sourceChanged = configuredWidth != sourceWidth || configuredHeight != sourceHeight;
		if (sourceChanged) {
			recurrentStateInitialized = false;
		}
		configuredWidth = sourceWidth;
		configuredHeight = sourceHeight;

		inputDims.resize(6);
		outputDims.resize(5);
		inputDims[0] = {1, 3, sourceHeight, sourceWidth};
		inputDims[5] = {1};
		for (size_t i = 1; i < 5; ++i) {
			if (recurrentStateInitialized && i < outputDims.size() && outputDims[i].size() == 4) {
				inputDims[i] = outputDims[i];
			} else {
				inputDims[i] = {1, 1, 1, 1};
			}
		}

		outputDims[0] = {1, 1, sourceHeight, sourceWidth};
		constexpr int stateChannels[] = {16, 20, 40, 64};
		for (size_t i = 1; i < 5; ++i) {
			const int divisor = 2 << (static_cast<int>(i) - 1);
			outputDims[i] = {1,
					static_cast<int64_t>(stateChannels[i - 1]),
					featureDimension(smallHeight, divisor),
					featureDimension(smallWidth, divisor)};
		}
	}

	/** Rebuild tensor views while preserving recurrent state vectors. */
	void prepareTensorBuffers(const std::vector<std::vector<int64_t>> &inputDims,
				  const std::vector<std::vector<int64_t>> &outputDims,
				  std::vector<std::vector<float>> &outputTensorValues,
				  std::vector<std::vector<float>> &inputTensorValues,
				  std::vector<Ort::Value> &inputTensor,
				  std::vector<Ort::Value> &outputTensor)
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
				memoryInfo, inputTensorValues[i].data(), inputTensorValues[i].size(), inputDims[i].data(),
				inputDims[i].size()));
		}
		outputTensor.clear();
		for (size_t i = 0; i < outputDims.size(); ++i) {
			outputTensor.push_back(Ort::Value::CreateTensor<float>(
				memoryInfo, outputTensorValues[i].data(), outputTensorValues[i].size(), outputDims[i].data(),
				outputDims[i].size()));
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
		for (size_t i = 1; i < session->GetOutputCount(); i++) {
			outputNames.push_back(session->GetOutputNameAllocated(i, allocator));
		}
	}

	virtual bool populateInputOutputShapes(const std::unique_ptr<Ort::Session> &session,
					       std::vector<std::vector<int64_t>> &inputDims,
					       std::vector<std::vector<int64_t>> &outputDims)
	{
		// Assuming model only has one input and one output image

		inputDims.clear();
		outputDims.clear();

		for (size_t i = 0; i < session->GetInputCount(); i++) {
			// Get input shape
			const Ort::TypeInfo inputTypeInfo = session->GetInputTypeInfo(i);
			const auto inputTensorInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
			inputDims.push_back(inputTensorInfo.GetShape());
		}

		for (size_t i = 1; i < session->GetOutputCount(); i++) {
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
		inputDims = {{1, 3, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1},
			     {1, 1, 1, 1}, {1, 1, 1, 1}, {1}};
		outputDims = {{1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1},
			      {1, 1, 1, 1}, {1, 1, 1, 1}};
		return true;
	}

	virtual void loadInputToTensor(const cv::Mat &preprocessedImage, uint32_t, uint32_t,
				       std::vector<std::vector<float>> &inputTensorValues)
	{
		inputTensorValues[0].assign(preprocessedImage.begin<float>(), preprocessedImage.end<float>());
		inputTensorValues[5][0] = downsampleRatio;
	}

	virtual void assignOutputToInput(std::vector<std::vector<float>> &outputTensorValues,
					 std::vector<std::vector<float>> &inputTensorValues)
	{
		for (size_t i = 1; i < 5; i++) {
			inputTensorValues[i].assign(outputTensorValues[i].begin(), outputTensorValues[i].end());
		}
		recurrentStateInitialized = true;
	}
};

#endif /* MODELRVM_H */
