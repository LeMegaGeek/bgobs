// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MODEL_BACKGROUND_MATTING_V2_H
#define MODEL_BACKGROUND_MATTING_V2_H

#include "Model.hpp"

class ModelBackgroundMattingV2 : public ModelBCHW {
public:
	void populateInputOutputNames(const std::unique_ptr<Ort::Session> &session,
				      std::vector<Ort::AllocatedStringPtr> &inputNames,
				      std::vector<Ort::AllocatedStringPtr> &outputNames) override
	{
		Ort::AllocatorWithDefaultOptions allocator;
		inputNames.clear();
		outputNames.clear();
		for (size_t i = 0; i < session->GetInputCount(); ++i) {
			inputNames.push_back(session->GetInputNameAllocated(i, allocator));
		}
		// pha and fgr are the only outputs required for the OBS composite.
		for (size_t i = 0; i < std::min<size_t>(2, session->GetOutputCount()); ++i) {
			outputNames.push_back(session->GetOutputNameAllocated(i, allocator));
		}
	}

	bool populateInputOutputShapes(const std::unique_ptr<Ort::Session> &session,
				       std::vector<std::vector<int64_t>> &inputDims,
				       std::vector<std::vector<int64_t>> &outputDims) override
	{
		if (session->GetInputCount() != 2 || session->GetOutputCount() < 2) {
			obs_log(LOG_ERROR,
				"Background Matting V2 model must expose two inputs and at least two outputs");
			return false;
		}
		inputDims = {{1, 3, 1, 1}, {1, 3, 1, 1}};
		outputDims = {{1, 1, 1, 1}, {1, 3, 1, 1}};
		return true;
	}

	void configureForFrame(std::vector<std::vector<int64_t>> &inputDims,
			       std::vector<std::vector<int64_t>> &outputDims, int width, int height)
	{
		inputDims = {{1, 3, height, width}, {1, 3, height, width}};
		outputDims = {{1, 1, height, width}, {1, 3, height, width}};
	}

	void loadStudioFrames(const cv::Mat &sourceCHW, const cv::Mat &backgroundCHW,
			      std::vector<std::vector<float>> &inputTensorValues)
	{
		std::copy(sourceCHW.begin<float>(), sourceCHW.end<float>(), inputTensorValues[0].begin());
		std::copy(backgroundCHW.begin<float>(), backgroundCHW.end<float>(), inputTensorValues[1].begin());
	}

	cv::Mat getNetworkOutput(const std::vector<std::vector<int64_t>> &outputDims,
				 std::vector<std::vector<float>> &outputTensorValues) override
	{
		if (outputDims.empty() || outputTensorValues.empty()) {
			return {};
		}
		return cv::Mat(static_cast<int>(outputDims[0][2]), static_cast<int>(outputDims[0][3]), CV_32FC1,
			       outputTensorValues[0].data());
	}

	cv::Mat getForegroundOutput(const std::vector<std::vector<int64_t>> &outputDims,
				    std::vector<std::vector<float>> &outputTensorValues)
	{
		if (outputDims.size() < 2 || outputTensorValues.size() < 2) {
			return {};
		}
		const int height = static_cast<int>(outputDims[1][2]);
		const int width = static_cast<int>(outputDims[1][3]);
		const size_t plane = static_cast<size_t>(height) * width;
		const float *planes = outputTensorValues[1].data();
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
};

#endif // MODEL_BACKGROUND_MATTING_V2_H
