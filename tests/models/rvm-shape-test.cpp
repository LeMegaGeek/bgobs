// SPDX-License-Identifier: GPL-3.0-or-later

#include <obs-module.h>

#include "models/ModelRVM.hpp"

#include <cstdlib>
#include <iostream>

int main()
{
	ModelRVM model;
	std::vector<std::vector<int64_t>> inputDims(6);
	std::vector<std::vector<int64_t>> outputDims(6);

	model.configureForFrame(inputDims, outputDims, 1920, 1080, 0.25f);
	if (inputDims[0] != std::vector<int64_t>({1, 3, 1080, 1920}) ||
	    outputDims[0] != std::vector<int64_t>({1, 3, 1080, 1920}) ||
	    outputDims[1] != std::vector<int64_t>({1, 1, 1080, 1920}) ||
	    inputDims[1] != std::vector<int64_t>({1, 1, 1, 1}) ||
	    outputDims[2] != std::vector<int64_t>({1, 16, 135, 240})) {
		std::cerr << "unexpected initial RVM tensor shapes\n";
		return EXIT_FAILURE;
	}

	model.markRecurrentStateInitialized();
	model.configureForFrame(inputDims, outputDims, 1920, 1080, 0.5f);
	if (inputDims[1] != std::vector<int64_t>({1, 1, 1, 1}) ||
	    outputDims[2] != std::vector<int64_t>({1, 16, 270, 480})) {
		std::cerr << "RVM recurrent state was not reset when the downsample ratio changed\n";
		return EXIT_FAILURE;
	}

	model.resetRecurrentState();
	model.configureForFrame(inputDims, outputDims, 1280, 720, 0.25f);
	if (outputDims[0] != std::vector<int64_t>({1, 3, 720, 1280}) ||
	    outputDims[1] != std::vector<int64_t>({1, 1, 720, 1280}) ||
	    outputDims[5] != std::vector<int64_t>({1, 64, 12, 20})) {
		std::cerr << "unexpected resized RVM tensor shapes\n";
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
