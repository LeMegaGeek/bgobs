// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "background-filter.h"

#include <onnxruntime_cxx_api.h>

#ifdef _WIN32
#include <wchar.h>
#endif // _WIN32

#include <opencv2/imgproc.hpp>

#include <cstring>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <memory>
#include <exception>
#include <fstream>
#include <new>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <regex>
#include <thread>
#include <utility>

#include "plugin-support.h"
#include "models/ModelSINET.hpp"
#include "models/ModelMediapipe.hpp"
#include "models/ModelSelfie.hpp"
#include "models/ModelSelfieMulticlass.hpp"
#include "models/ModelRVM.hpp"
#include "models/ModelPPHumanSeg.hpp"
#include "models/ModelTCMonoDepth.hpp"
#include "FilterData.hpp"
#include "ort-utils/ort-session-utils.hpp"
#include "obs-utils/obs-utils.hpp"
#include "consts.h"
#include "plugin-info.hpp"
#include "background/mask-post-processing.hpp"
#include "update-checker/update-checker.h"

struct bgobs_background_filter : public filter_data, public std::enable_shared_from_this<bgobs_background_filter> {
	std::string beautyPreset;
	std::string mattingMode;
	std::atomic<int> settingsVersion{2};
	std::atomic<bool> enableThreshold{true};
	std::atomic<bool> stopWhenSourceIsInactive{true};
	std::atomic<float> threshold{0.5f};
	cv::Scalar backgroundColor{0, 0, 0, 0};
	std::atomic<float> edgeSoftness{0.05f};
	std::atomic<float> edgeRefinement{0.25f};
	std::atomic<float> foregroundCleanup{0.15f};
	std::atomic<float> contourFilter{0.05f};
	std::atomic<float> smoothContour{0.5f};
	std::atomic<float> feather{0.0f};
	std::atomic<int> maskExpansion{0};
	std::atomic<bool> showMaskPreview{false};

	cv::Mat backgroundMask;
	cv::Mat lastBackgroundMask;
	cv::Mat lastImageBGRA;
	std::mutex legacySimilarityMutex;
	std::atomic<float> temporalSmoothFactor{0.0f};
	std::atomic<float> imageSimilarityThreshold{35.0f};
	std::atomic<bool> enableImageSimilarity{true};
	std::atomic<int> maskEveryXFrames{1};
	std::atomic<int> maskEveryXFramesCount{0};
	std::atomic<int64_t> blurBackground{0};
	std::atomic<bool> enableFocalBlur{false};
	std::atomic<float> blurFocusPoint{0.1f};
	std::atomic<float> blurFocusDepth{0.1f};

	gs_effect_t *effect = nullptr;
	gs_effect_t *kawaseBlurEffect = nullptr;
	gs_texture_t *alphaTexture = nullptr;
	uint32_t alphaTextureWidth = 0;
	uint32_t alphaTextureHeight = 0;
	gs_color_format alphaTextureFormat = GS_UNKNOWN;
	std::vector<uint16_t> alphaUploadBuffer;
	gs_texture_t *blurTexture = nullptr;
	uint32_t blurTextureWidth = 0;
	uint32_t blurTextureHeight = 0;

	std::mutex modelMutex;

	// A capacity-one queue keeps a slow inference from accumulating latency in
	// the OBS callbacks. The worker always consumes the newest camera frame.
	std::thread inferenceWorker;
	std::mutex inferenceQueueMutex;
	std::condition_variable inferenceQueueCondition;
	bool stopInferenceWorker = false;
	cv::Mat pendingInferenceFrame;
	uint64_t pendingInferenceGeneration = 0;
	std::atomic<uint64_t> inferenceGeneration{1};

	// RVM's soft alpha stays float until the render thread uploads it. The
	// existing 8-bit mask remains the path for segmentation models.
	cv::Mat backgroundAlpha;
	uint64_t backgroundAlphaGeneration = 0;
	std::atomic<bool> nativeAlpha{false};
	std::atomic<float> rvmDownsampleRatio{0.25f};
	std::atomic<uint64_t> submittedFrames{0};
	std::atomic<uint64_t> completedFrames{0};
	std::atomic<float> lastInferenceMs{0.0f};

	~bgobs_background_filter() { obs_log(LOG_INFO, "BGOBS background filter destructor called"); }
};

namespace {

const char *const BGOBS_PRESET_NATURAL = "natural";
const char *const BGOBS_PRESET_STUDIO = "studio";
const char *const BGOBS_PRESET_CRISP = "crisp";
const char *const BGOBS_PRESET_PERFORMANCE = "performance";
const char *const BGOBS_PRESET_CUSTOM = "custom";
const char *const BGOBS_PRESET_LEGACY_CLEAN = "clean";
const char *const BGOBS_PRESET_LEGACY_SOFT = "soft";
const char *const BGOBS_MATTING_AUTO = "auto";
const char *const BGOBS_MATTING_QUALITY = "quality";
const char *const BGOBS_MATTING_PERFORMANCE = "performance";
const char *const BGOBS_MATTING_LEGACY = "legacy";
constexpr int BGOBS_SETTINGS_VERSION = 2;

struct BackgroundBeautyPreset {
	const char *id;
	double threshold;
	double edgeSoftness;
	double edgeRefinement;
	double foregroundCleanup;
	double contourFilter;
	double smoothContour;
	double temporalSmoothFactor;
	double imageSimilarityThreshold;
	int maskExpansion;
	int maskEveryXFrames;
	int numThreads;
};

const BackgroundBeautyPreset BGOBS_PRESETS[] = {
	{BGOBS_PRESET_NATURAL, 0.50, 0.10, 0.40, 0.22, 0.030, 0.58, 0.84, 35.0, 0, 1, 1},
	{BGOBS_PRESET_STUDIO, 0.47, 0.18, 0.55, 0.20, 0.020, 0.74, 0.88, 34.0, 1, 1, 1},
	{BGOBS_PRESET_CRISP, 0.54, 0.05, 0.28, 0.26, 0.040, 0.40, 0.78, 36.0, 0, 1, 1},
	{BGOBS_PRESET_PERFORMANCE, 0.52, 0.04, 0.15, 0.10, 0.050, 0.32, 0.65, 38.0, 0, 2, 1},
};

thread_local bool applyingBeautyPreset = false;

class BeautyPresetApplicationGuard {
public:
	BeautyPresetApplicationGuard() : previous(applyingBeautyPreset) { applyingBeautyPreset = true; }

	~BeautyPresetApplicationGuard() { applyingBeautyPreset = previous; }

private:
	bool previous;
};

const BackgroundBeautyPreset *findBeautyPreset(const char *presetId)
{
	if (!presetId || strcmp(presetId, BGOBS_PRESET_CUSTOM) == 0) {
		return nullptr;
	}

	for (const BackgroundBeautyPreset &preset : BGOBS_PRESETS) {
		if (strcmp(presetId, preset.id) == 0) {
			return &preset;
		}
	}

	if (strcmp(presetId, BGOBS_PRESET_LEGACY_CLEAN) == 0) {
		return findBeautyPreset(BGOBS_PRESET_NATURAL);
	}

	if (strcmp(presetId, BGOBS_PRESET_LEGACY_SOFT) == 0) {
		return findBeautyPreset(BGOBS_PRESET_STUDIO);
	}

	return nullptr;
}

bool doubleMatches(double actual, double expected)
{
	return std::abs(actual - expected) <= 0.000001;
}

bool settingsMatchBeautyPreset(obs_data_t *settings, const BackgroundBeautyPreset &preset)
{
	if (!settings) {
		return false;
	}

	return obs_data_get_bool(settings, "enable_threshold") &&
	       doubleMatches(obs_data_get_double(settings, "threshold"), preset.threshold) &&
	       doubleMatches(obs_data_get_double(settings, "edge_softness"), preset.edgeSoftness) &&
	       doubleMatches(obs_data_get_double(settings, "edge_refinement"), preset.edgeRefinement) &&
	       doubleMatches(obs_data_get_double(settings, "foreground_cleanup"), preset.foregroundCleanup) &&
	       doubleMatches(obs_data_get_double(settings, "contour_filter"), preset.contourFilter) &&
	       doubleMatches(obs_data_get_double(settings, "smooth_contour"), preset.smoothContour) &&
	       doubleMatches(obs_data_get_double(settings, "temporal_smooth_factor"), preset.temporalSmoothFactor) &&
	       doubleMatches(obs_data_get_double(settings, "image_similarity_threshold"),
			     preset.imageSimilarityThreshold) &&
	       obs_data_get_bool(settings, "enable_image_similarity") &&
	       doubleMatches(obs_data_get_double(settings, "mask_expansion"), preset.maskExpansion) &&
	       obs_data_get_int(settings, "mask_every_x_frames") == preset.maskEveryXFrames &&
	       obs_data_get_int(settings, "numThreads") == preset.numThreads;
}

const BackgroundBeautyPreset *findMatchingBeautyPreset(obs_data_t *settings)
{
	for (const BackgroundBeautyPreset &preset : BGOBS_PRESETS) {
		if (settingsMatchBeautyPreset(settings, preset)) {
			return &preset;
		}
	}

	return nullptr;
}

void applyBeautyPreset(obs_data_t *settings, const BackgroundBeautyPreset &preset)
{
	obs_data_set_bool(settings, "enable_threshold", true);
	obs_data_set_double(settings, "threshold", preset.threshold);
	obs_data_set_double(settings, "edge_softness", preset.edgeSoftness);
	obs_data_set_double(settings, "edge_refinement", preset.edgeRefinement);
	obs_data_set_double(settings, "foreground_cleanup", preset.foregroundCleanup);
	obs_data_set_double(settings, "contour_filter", preset.contourFilter);
	obs_data_set_double(settings, "smooth_contour", preset.smoothContour);
	obs_data_set_double(settings, "temporal_smooth_factor", preset.temporalSmoothFactor);
	obs_data_set_double(settings, "image_similarity_threshold", preset.imageSimilarityThreshold);
	obs_data_set_bool(settings, "enable_image_similarity", true);
	obs_data_set_double(settings, "mask_expansion", preset.maskExpansion);
	obs_data_set_int(settings, "mask_every_x_frames", preset.maskEveryXFrames);
	obs_data_set_int(settings, "numThreads", preset.numThreads);
}

void markBeautyPresetCustom(obs_data_t *settings)
{
	if (applyingBeautyPreset) {
		return;
	}

	if (!settings) {
		return;
	}

	if (const BackgroundBeautyPreset *preset = findBeautyPreset(obs_data_get_string(settings, "beauty_preset"));
	    preset && settingsMatchBeautyPreset(settings, *preset)) {
		return;
	}

	if (const BackgroundBeautyPreset *preset = findMatchingBeautyPreset(settings)) {
		obs_data_set_string(settings, "beauty_preset", preset->id);
		return;
	}

	obs_data_set_string(settings, "beauty_preset", BGOBS_PRESET_CUSTOM);
}

void setPropertyVisible(obs_properties_t *ppts, const char *propertyName, bool visible)
{
	obs_property_t *property = obs_properties_get(ppts, propertyName);
	if (property) {
		obs_property_set_visible(property, visible);
	}
}

void setAdvancedPropertiesVisible(obs_properties_t *ppts, bool visible)
{
	for (const char *propertyName :
	     {"model_select", "useGPU", "mask_every_x_frames", "numThreads", "enable_focal_blur", "enable_threshold",
	      "threshold_group", "focal_blur_group", "temporal_smooth_factor", "image_similarity_threshold",
	      "enable_image_similarity", "mask_expansion"}) {
		setPropertyVisible(ppts, propertyName, visible);
	}

	setPropertyVisible(ppts, "blur_background", true);
}

void migrateBackgroundSettings(obs_data_t *settings)
{
	if (!settings) {
		return;
	}
	const bool hasUserVersion = obs_data_has_user_value(settings, "settings_version");
	if (hasUserVersion && obs_data_get_int(settings, "settings_version") >= BGOBS_SETTINGS_VERSION) {
		return;
	}

	const char *oldModelValue = obs_data_get_string(settings, "model_select");
	const std::string oldModel = oldModelValue ? oldModelValue : "";
	if (!oldModel.empty() && oldModel != MODEL_RVM && oldModel != MODEL_DEPTH_TCMONODEPTH) {
		// Keep an escape hatch for users who explicitly relied on the old
		// segmentation model. New and migrated scenes use RVM HD by default.
		obs_data_set_string(settings, "legacy_model_select", oldModel.c_str());
		obs_data_set_string(settings, "model_select", MODEL_RVM);
	}
	if (oldModel != MODEL_DEPTH_TCMONODEPTH) {
		obs_data_set_string(settings, "matting_mode", BGOBS_MATTING_AUTO);
	} else {
		obs_data_set_string(settings, "matting_mode", BGOBS_MATTING_LEGACY);
	}
	obs_data_set_int(settings, "settings_version", BGOBS_SETTINGS_VERSION);
}

std::string effectiveModelSelection(obs_data_t *settings, const std::string &mattingMode)
{
	if (mattingMode == BGOBS_MATTING_LEGACY) {
		const char *legacyModel = obs_data_get_string(settings, "legacy_model_select");
		if (legacyModel && legacyModel[0] != '\0') {
			return legacyModel;
		}
	}
	const char *model = obs_data_get_string(settings, "model_select");
	return model && model[0] != '\0' ? model : MODEL_RVM;
}

float chooseRvmDownsampleRatio(const std::string &mattingMode, int width, int height)
{
	const int largestSide = std::max(width, height);
	if (mattingMode == BGOBS_MATTING_QUALITY) {
		if (largestSide <= 1280)
			return 0.375f;
		if (largestSide <= 1920)
			return 0.25f;
		return 0.125f;
	}
	if (mattingMode == BGOBS_MATTING_PERFORMANCE) {
		return 0.10f;
	}
	// Automatic mode keeps the internal backbone around 256-320 px while
	// retaining RVM's full-resolution refinement output.
	return std::clamp(320.0f / static_cast<float>(largestSide), 0.10f, 0.25f);
}

uint16_t floatToHalf(float value)
{
	union {
		float f;
		uint32_t u;
	} bits{value};
	const uint32_t sign = (bits.u >> 16) & 0x8000u;
	const uint32_t exponent = (bits.u >> 23) & 0xffu;
	const uint32_t mantissa = bits.u & 0x7fffffu;
	if (exponent == 0xffu) {
		return static_cast<uint16_t>(sign | 0x7c00u | (mantissa ? 0x0200u : 0u));
	}
	int halfExponent = static_cast<int>(exponent) - 127 + 15;
	if (halfExponent >= 31) {
		return static_cast<uint16_t>(sign | 0x7c00u);
	}
	if (halfExponent <= 0) {
		if (halfExponent < -10) {
			return static_cast<uint16_t>(sign);
		}
		uint32_t normalizedMantissa = mantissa | 0x800000u;
		const int shift = 14 - halfExponent;
		uint32_t halfMantissa = normalizedMantissa >> shift;
		if ((normalizedMantissa >> (shift - 1)) & 1u) {
			++halfMantissa;
		}
		return static_cast<uint16_t>(sign | halfMantissa);
	}
	uint32_t halfMantissa = mantissa >> 13;
	if (mantissa & 0x1000u) {
		++halfMantissa;
		if (halfMantissa == 0x400u) {
			halfMantissa = 0;
			++halfExponent;
			if (halfExponent >= 31) {
				return static_cast<uint16_t>(sign | 0x7c00u);
			}
		}
	}
	return static_cast<uint16_t>(sign | (static_cast<uint32_t>(halfExponent) << 10) | halfMantissa);
}

} // namespace

static void invalidateInference(struct bgobs_background_filter *tf);
static void startInferenceWorker(const std::shared_ptr<bgobs_background_filter> &tf);
static void stopInferenceWorker(const std::shared_ptr<bgobs_background_filter> &tf);
static void submitInferenceFrame(const std::shared_ptr<bgobs_background_filter> &tf, cv::Mat frame);

const char *background_filter_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("BackgroundRemoval");
}

/**                   PROPERTIES                     */

static bool visible_on_bool(obs_properties_t *ppts, obs_data_t *settings, const char *bool_prop, const char *prop_name)
{
	const bool enabled = obs_data_get_bool(settings, bool_prop);
	obs_property_t *p = obs_properties_get(ppts, prop_name);
	obs_property_set_visible(p, enabled);
	return true;
}

static bool enable_threshold_modified(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	markBeautyPresetCustom(settings);
	return visible_on_bool(ppts, settings, "enable_threshold", "threshold_group");
}

static bool beauty_preset_modified(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(ppts);
	UNUSED_PARAMETER(p);

	const BackgroundBeautyPreset *preset = findBeautyPreset(obs_data_get_string(settings, "beauty_preset"));
	if (preset) {
		BeautyPresetApplicationGuard guard;
		applyBeautyPreset(settings, *preset);
		obs_data_set_string(settings, "beauty_preset", preset->id);
	}

	return true;
}

static bool beauty_tuning_modified(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(ppts);
	UNUSED_PARAMETER(p);

	markBeautyPresetCustom(settings);
	return true;
}

static bool enable_focal_blur(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	return visible_on_bool(ppts, settings, "enable_focal_blur", "focal_blur_group");
}

static bool enable_image_similarity(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	markBeautyPresetCustom(settings);
	return visible_on_bool(ppts, settings, "enable_image_similarity", "image_similarity_threshold");
}

static bool enable_advanced_settings(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);

	const bool enabled = obs_data_get_bool(settings, "advanced");
	setAdvancedPropertiesVisible(ppts, enabled);

	if (enabled) {
		visible_on_bool(ppts, settings, "enable_threshold", "threshold_group");
		visible_on_bool(ppts, settings, "enable_focal_blur", "focal_blur_group");
		visible_on_bool(ppts, settings, "enable_image_similarity", "image_similarity_threshold");
	}

	return true;
}

obs_properties_t *background_filter_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *advanced = obs_properties_add_bool(props, "advanced", obs_module_text("Advanced"));

	obs_properties_add_bool(props, "stop_when_source_is_inactive",
				obs_module_text("Stop filter when source is inactive"));

	// If advanced is selected show the advanced settings, otherwise hide them
	obs_property_set_modified_callback(advanced, enable_advanced_settings);

	obs_property_t *p_beauty_preset = obs_properties_add_list(
		props, "beauty_preset", obs_module_text("BeautyPreset"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p_beauty_preset, obs_module_text("PresetNatural"), BGOBS_PRESET_NATURAL);
	obs_property_list_add_string(p_beauty_preset, obs_module_text("PresetStudio"), BGOBS_PRESET_STUDIO);
	obs_property_list_add_string(p_beauty_preset, obs_module_text("PresetCrisp"), BGOBS_PRESET_CRISP);
	obs_property_list_add_string(p_beauty_preset, obs_module_text("PresetPerformance"), BGOBS_PRESET_PERFORMANCE);
	obs_property_list_add_string(p_beauty_preset, obs_module_text("PresetCustom"), BGOBS_PRESET_CUSTOM);
	obs_property_set_modified_callback(p_beauty_preset, beauty_preset_modified);

	obs_property_t *p_matting_mode = obs_properties_add_list(
		props, "matting_mode", obs_module_text("MattingMode"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p_matting_mode, obs_module_text("MattingAutomatic"), BGOBS_MATTING_AUTO);
	obs_property_list_add_string(p_matting_mode, obs_module_text("MattingQuality"), BGOBS_MATTING_QUALITY);
	obs_property_list_add_string(p_matting_mode, obs_module_text("MattingPerformance"),
				     BGOBS_MATTING_PERFORMANCE);
	obs_property_list_add_string(p_matting_mode, obs_module_text("MattingLegacy"), BGOBS_MATTING_LEGACY);

	obs_properties_add_bool(props, "show_mask_preview", obs_module_text("ShowMaskPreview"));

	/* Threshold props */
	obs_property_t *p_enable_threshold =
		obs_properties_add_bool(props, "enable_threshold", obs_module_text("EnableThreshold"));
	obs_property_set_modified_callback(p_enable_threshold, enable_threshold_modified);

	// Threshold props group
	obs_properties_t *threshold_props = obs_properties_create();

	obs_property_t *p_threshold = obs_properties_add_float_slider(threshold_props, "threshold",
								      obs_module_text("Threshold"), 0.0, 1.0, 0.025);
	obs_property_set_modified_callback(p_threshold, beauty_tuning_modified);

	obs_property_t *p_edge_softness = obs_properties_add_float_slider(
		threshold_props, "edge_softness", obs_module_text("EdgeSoftness"), 0.0, 1.0, 0.01);
	obs_property_set_modified_callback(p_edge_softness, beauty_tuning_modified);

	obs_property_t *p_edge_refinement = obs_properties_add_float_slider(
		threshold_props, "edge_refinement", obs_module_text("EdgeRefinement"), 0.0, 1.0, 0.05);
	obs_property_set_modified_callback(p_edge_refinement, beauty_tuning_modified);

	obs_property_t *p_foreground_cleanup = obs_properties_add_float_slider(
		threshold_props, "foreground_cleanup", obs_module_text("ForegroundCleanup"), 0.0, 1.0, 0.05);
	obs_property_set_modified_callback(p_foreground_cleanup, beauty_tuning_modified);

	obs_property_t *p_contour_filter = obs_properties_add_float_slider(
		threshold_props, "contour_filter", obs_module_text("ContourFilterPercentOfImage"), 0.0, 1.0, 0.025);
	obs_property_set_modified_callback(p_contour_filter, beauty_tuning_modified);

	obs_property_t *p_smooth_contour = obs_properties_add_float_slider(
		threshold_props, "smooth_contour", obs_module_text("SmoothSilhouette"), 0.0, 1.0, 0.05);
	obs_property_set_modified_callback(p_smooth_contour, beauty_tuning_modified);

	obs_properties_add_float_slider(threshold_props, "feather", obs_module_text("FeatherBlendSilhouette"), 0.0, 1.0,
					0.05);

	obs_properties_add_group(props, "threshold_group", obs_module_text("ThresholdGroup"), OBS_GROUP_NORMAL,
				 threshold_props);

	/* Mask expansion slider - in advanced settings */
	obs_property_t *p_mask_expansion =
		obs_properties_add_int_slider(props, "mask_expansion", obs_module_text("MaskExpansion"), -30, 30, 1);
	obs_property_set_modified_callback(p_mask_expansion, beauty_tuning_modified);

	/* GPU, CPU and performance Props */
	obs_property_t *p_use_gpu = obs_properties_add_list(props, "useGPU", obs_module_text("InferenceDevice"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(p_use_gpu, obs_module_text("Automatic"), USEGPU_AUTO);
	obs_property_list_add_string(p_use_gpu, obs_module_text("CPU"), USEGPU_CPU);
#ifdef HAVE_ONNXRUNTIME_DML_EP
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUDirectML"), USEGPU_DIRECTML);
#endif
#ifdef HAVE_ONNXRUNTIME_CUDA_EP
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUCUDA"), USEGPU_CUDA);
#endif
#ifdef HAVE_ONNXRUNTIME_ROCM_EP
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUROCM"), USEGPU_ROCM);
#endif
#ifdef HAVE_ONNXRUNTIME_MIGRAPHX_EP
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUMIGRAPHX"), USEGPU_MIGRAPHX);
#endif
#ifdef HAVE_ONNXRUNTIME_TENSORRT_EP
	obs_property_list_add_string(p_use_gpu, obs_module_text("TENSORRT"), USEGPU_TENSORRT);
#endif
#if defined(__APPLE__)
	obs_property_list_add_string(p_use_gpu, obs_module_text("CoreML"), USEGPU_COREML);
#endif

	obs_property_t *p_mask_every_x_frames = obs_properties_add_int(
		props, "mask_every_x_frames", obs_module_text("CalculateMaskEveryXFrame"), 1, 300, 1);
	obs_property_set_modified_callback(p_mask_every_x_frames, beauty_tuning_modified);

	obs_property_t *p_num_threads =
		obs_properties_add_int_slider(props, "numThreads", obs_module_text("NumThreads"), 0, 8, 1);
	obs_property_set_modified_callback(p_num_threads, beauty_tuning_modified);

	/* Model selection Props */
	obs_property_t *p_model_select = obs_properties_add_list(props, "model_select",
								 obs_module_text("SegmentationModel"),
								 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(p_model_select, obs_module_text("SINet"), MODEL_SINET);
	obs_property_list_add_string(p_model_select, obs_module_text("MediaPipe"), MODEL_MEDIAPIPE);
	obs_property_list_add_string(p_model_select, obs_module_text("Selfie Segmentation"), MODEL_SELFIE);
	obs_property_list_add_string(p_model_select, obs_module_text("Selfie Multiclass"), MODEL_SELFIE_MULTICLASS);
	obs_property_list_add_string(p_model_select, obs_module_text("PPHumanSeg"), MODEL_PPHUMANSEG);
	obs_property_list_add_string(p_model_select, obs_module_text("Robust Video Matting"), MODEL_RVM);
	obs_property_list_add_string(p_model_select, obs_module_text("TCMonoDepth"), MODEL_DEPTH_TCMONODEPTH);

	obs_property_t *p_temporal_smooth_factor = obs_properties_add_float_slider(
		props, "temporal_smooth_factor", obs_module_text("TemporalSmoothFactor"), 0.0, 1.0, 0.01);
	obs_property_set_modified_callback(p_temporal_smooth_factor, beauty_tuning_modified);

	obs_property_t *p_enable_image_similarity =
		obs_properties_add_bool(props, "enable_image_similarity", obs_module_text("EnableImageSimilarity"));
	obs_property_set_modified_callback(p_enable_image_similarity, enable_image_similarity);

	obs_property_t *p_image_similarity_threshold = obs_properties_add_float_slider(
		props, "image_similarity_threshold", obs_module_text("ImageSimilarityThreshold"), 0.0, 100.0, 1.0);
	obs_property_set_modified_callback(p_image_similarity_threshold, beauty_tuning_modified);

	/* Background Blur Props */
	obs_properties_add_int_slider(props, "blur_background", obs_module_text("BlurBackgroundFactor0NoBlurUseColor"),
				      0, 20, 1);

	obs_property_t *p_enable_focal_blur =
		obs_properties_add_bool(props, "enable_focal_blur", obs_module_text("EnableFocalBlur"));
	obs_property_set_modified_callback(p_enable_focal_blur, enable_focal_blur);

	obs_properties_t *focal_blur_props = obs_properties_create();

	obs_properties_add_float_slider(focal_blur_props, "blur_focus_point", obs_module_text("BlurFocusPoint"), 0.0,
					1.0, 0.05);
	obs_properties_add_float_slider(focal_blur_props, "blur_focus_depth", obs_module_text("BlurFocusDepth"), 0.0,
					0.3, 0.02);

	obs_properties_add_group(props, "focal_blur_group", obs_module_text("FocalBlurGroup"), OBS_GROUP_NORMAL,
				 focal_blur_props);

	// Add an informative text about the plugin.
	std::string basic_info = bgobs_plugin_info_html();
	// Check for update
	const char *latest_version = get_latest_version();
	if (latest_version != nullptr) {
		basic_info +=
			std::regex_replace(PLUGIN_INFO_TEMPLATE_UPDATE_AVAILABLE, std::regex("%1"), latest_version);
	}
	obs_properties_add_text(props, "info", basic_info.c_str(), OBS_TEXT_INFO);

	setAdvancedPropertiesVisible(props, false);

	UNUSED_PARAMETER(data);
	return props;
}

void background_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "settings_version", BGOBS_SETTINGS_VERSION);
	obs_data_set_default_string(settings, "matting_mode", BGOBS_MATTING_AUTO);
	obs_data_set_default_bool(settings, "advanced", false);
	obs_data_set_default_string(settings, "beauty_preset", BGOBS_PRESET_NATURAL);
	obs_data_set_default_bool(settings, "stop_when_source_is_inactive", true);
	obs_data_set_default_bool(settings, "enable_threshold", true);
	obs_data_set_default_double(settings, "threshold", 0.5);
	obs_data_set_default_double(settings, "edge_softness", 0.10);
	obs_data_set_default_double(settings, "edge_refinement", 0.40);
	obs_data_set_default_double(settings, "foreground_cleanup", 0.22);
	obs_data_set_default_double(settings, "contour_filter", 0.030);
	obs_data_set_default_double(settings, "smooth_contour", 0.58);
	obs_data_set_default_double(settings, "mask_expansion", 0);
	obs_data_set_default_double(settings, "feather", 0.0);
	obs_data_set_default_bool(settings, "show_mask_preview", false);
	obs_data_set_default_string(settings, "useGPU", USEGPU_AUTO);
	obs_data_set_default_string(settings, "model_select", MODEL_RVM);
	obs_data_set_default_int(settings, "mask_every_x_frames", 1);
	obs_data_set_default_int(settings, "blur_background", 0);
	obs_data_set_default_int(settings, "numThreads", 0);
	obs_data_set_default_bool(settings, "enable_focal_blur", false);
	obs_data_set_default_double(settings, "temporal_smooth_factor", 0.84);
	obs_data_set_default_double(settings, "image_similarity_threshold", 35.0);
	obs_data_set_default_bool(settings, "enable_image_similarity", true);
	obs_data_set_default_double(settings, "blur_focus_point", 0.1);
	obs_data_set_default_double(settings, "blur_focus_depth", 0.0);
}

void background_filter_update(void *data, obs_data_t *settings)
{
	obs_log(LOG_INFO, "BGOBS background filter updated");

	// Cast to shared_ptr pointer and create a local shared_ptr
	auto *ptr = static_cast<std::shared_ptr<bgobs_background_filter> *>(data);
	if (!ptr) {
		return;
	}

	std::shared_ptr<bgobs_background_filter> tf = *ptr;
	if (!tf) {
		return;
	}

	tf->isDisabled = true;
	migrateBackgroundSettings(settings);
	tf->settingsVersion = obs_data_get_int(settings, "settings_version");

	const char *mattingModeValue = obs_data_get_string(settings, "matting_mode");
	const std::string mattingMode =
		mattingModeValue && mattingModeValue[0] != '\0' ? mattingModeValue : BGOBS_MATTING_AUTO;
	{
		std::lock_guard<std::mutex> lock(tf->modelMutex);
		tf->mattingMode = mattingMode;
	}
	if (mattingMode != BGOBS_MATTING_LEGACY && mattingMode != BGOBS_MATTING_PERFORMANCE) {
		// The high-resolution RVM path is the default for all new and migrated
		// background-removal filters.
		obs_data_set_string(settings, "model_select", MODEL_RVM);
	}

	const char *beautyPreset = obs_data_get_string(settings, "beauty_preset");
	tf->beautyPreset = beautyPreset ? beautyPreset : BGOBS_PRESET_CUSTOM;
	const BackgroundBeautyPreset *preset = findBeautyPreset(tf->beautyPreset.c_str());
	if (!preset && tf->beautyPreset == BGOBS_PRESET_CUSTOM) {
		preset = findMatchingBeautyPreset(settings);
	}
	if (preset) {
		BeautyPresetApplicationGuard guard;
		applyBeautyPreset(settings, *preset);
		obs_data_set_string(settings, "beauty_preset", preset->id);
		tf->beautyPreset = preset->id;
	}

	tf->stopWhenSourceIsInactive = obs_data_get_bool(settings, "stop_when_source_is_inactive");
	tf->enableThreshold = (float)obs_data_get_bool(settings, "enable_threshold");
	tf->threshold = (float)obs_data_get_double(settings, "threshold");
	tf->edgeSoftness = (float)obs_data_get_double(settings, "edge_softness");
	tf->edgeRefinement = (float)obs_data_get_double(settings, "edge_refinement");
	tf->foregroundCleanup = (float)obs_data_get_double(settings, "foreground_cleanup");

	tf->contourFilter = (float)obs_data_get_double(settings, "contour_filter");
	tf->smoothContour = (float)obs_data_get_double(settings, "smooth_contour");
	tf->maskExpansion = (int)obs_data_get_double(settings, "mask_expansion");
	tf->feather = (float)obs_data_get_double(settings, "feather");
	tf->showMaskPreview = obs_data_get_bool(settings, "show_mask_preview");
	tf->maskEveryXFrames = std::max(1, (int)obs_data_get_int(settings, "mask_every_x_frames"));
	tf->maskEveryXFramesCount.store(0, std::memory_order_relaxed);
	tf->blurBackground = obs_data_get_int(settings, "blur_background");
	tf->enableFocalBlur = (float)obs_data_get_bool(settings, "enable_focal_blur");
	tf->blurFocusPoint = (float)obs_data_get_double(settings, "blur_focus_point");
	tf->blurFocusDepth = (float)obs_data_get_double(settings, "blur_focus_depth");
	tf->temporalSmoothFactor = (float)obs_data_get_double(settings, "temporal_smooth_factor");
	tf->imageSimilarityThreshold = (float)obs_data_get_double(settings, "image_similarity_threshold");
	tf->enableImageSimilarity = (float)obs_data_get_bool(settings, "enable_image_similarity");

	std::string newUseGpu = obs_data_get_string(settings, "useGPU");
	if (newUseGpu.empty() || newUseGpu == USEGPU_AUTO) {
#ifdef HAVE_ONNXRUNTIME_DML_EP
		newUseGpu = USEGPU_DIRECTML;
#else
		newUseGpu = USEGPU_CPU;
#endif
	}
	std::string newModel = effectiveModelSelection(settings, mattingMode);
	if (mattingMode == BGOBS_MATTING_PERFORMANCE) {
		newModel = MODEL_MEDIAPIPE;
	}
	uint32_t newNumThreads = (uint32_t)obs_data_get_int(settings, "numThreads");
	if (newNumThreads == 0) {
		const unsigned int hardwareThreads = std::max(2u, std::thread::hardware_concurrency());
		newNumThreads = std::clamp(hardwareThreads / 2u, 2u, 4u);
	}
	tf->nativeAlpha = (newModel == MODEL_RVM && mattingMode != BGOBS_MATTING_LEGACY);
	tf->rvmDownsampleRatio = 0.25f;
	invalidateInference(tf.get());

	std::string modelSelectionForLog;
	std::string useGpuForLog;
	uint32_t numThreadsForLog = 0;
#ifdef _WIN32
	std::wstring modelFilepathForLog;
#else
	std::string modelFilepathForLog;
#endif
	{
		std::unique_lock<std::mutex> lock(tf->modelMutex);
		if (tf->modelSelection.empty() || tf->modelSelection != newModel || tf->useGPU != newUseGpu ||
		    tf->numThreads != newNumThreads) {

			// Re-initialize model if it is not already selected or the inference device changed.
			tf->modelSelection = newModel;
			tf->useGPU = newUseGpu;
			tf->numThreads = newNumThreads;

			if (tf->modelSelection == MODEL_SINET)
				tf->model.reset(new ModelSINET);
			if (tf->modelSelection == MODEL_SELFIE)
				tf->model.reset(new ModelSelfie);
			if (tf->modelSelection == MODEL_SELFIE_MULTICLASS)
				tf->model.reset(new ModelSelfieMulticlass);
			if (tf->modelSelection == MODEL_MEDIAPIPE)
				tf->model.reset(new ModelMediaPipe);
			if (tf->modelSelection == MODEL_RVM)
				tf->model.reset(new ModelRVM);
			if (tf->modelSelection == MODEL_PPHUMANSEG)
				tf->model.reset(new ModelPPHumanSeg);
			if (tf->modelSelection == MODEL_DEPTH_TCMONODEPTH)
				tf->model.reset(new ModelTCMonoDepth);

			const int ortSessionResult = createOrtSession(tf.get());
			if (ortSessionResult != OBS_BGREMOVAL_ORT_SESSION_SUCCESS) {
				obs_log(LOG_ERROR, "Failed to create ONNXRuntime session. Error code: %d",
					ortSessionResult);
				if (tf->useGPU != USEGPU_CPU) {
					obs_log(LOG_WARNING, "Retrying model initialization on CPU");
					tf->useGPU = USEGPU_CPU;
					tf->session.reset();
					resetFilterModelState(tf.get());
					if (createOrtSession(tf.get()) != OBS_BGREMOVAL_ORT_SESSION_SUCCESS) {
						obs_log(LOG_ERROR, "Failed to create CPU ONNXRuntime session");
						tf->isDisabled = true;
						tf->model.reset();
						return;
					}
				} else {
					tf->isDisabled = true;
					tf->model.reset();
					return;
				}
			}
		}
		resetFilterModelState(tf.get());

		modelSelectionForLog = tf->modelSelection;
		useGpuForLog = tf->useGPU;
		numThreadsForLog = tf->numThreads;
		modelFilepathForLog = tf->modelFilepath;
	}

	obs_enter_graphics();

	char *effect_path = obs_module_file(EFFECT_PATH);
	gs_effect_destroy(tf->effect);
	tf->effect = gs_effect_create_from_file(effect_path, NULL);
	bfree(effect_path);

	char *kawaseBlurEffectPath = obs_module_file(KAWASE_BLUR_EFFECT_PATH);
	gs_effect_destroy(tf->kawaseBlurEffect);
	tf->kawaseBlurEffect = gs_effect_create_from_file(kawaseBlurEffectPath, NULL);
	bfree(kawaseBlurEffectPath);

	obs_leave_graphics();

	// Log the currently selected options
	obs_log(LOG_INFO, "BGOBS filter options:");
	// name of the source that the filter is attached to
	obs_log(LOG_INFO, "  Source: %s", obs_source_get_name(tf->source));
	obs_log(LOG_INFO, "  Preset: %s", tf->beautyPreset.c_str());
	obs_log(LOG_INFO, "  Matting mode: %s", mattingMode.c_str());
	obs_log(LOG_INFO, "  Model: %s", modelSelectionForLog.c_str());
	obs_log(LOG_INFO, "  Inference Device: %s", useGpuForLog.c_str());
	obs_log(LOG_INFO, "  Num Threads: %u", numThreadsForLog);
	obs_log(LOG_INFO, "  Enable Threshold: %s", tf->enableThreshold.load() ? "true" : "false");
	obs_log(LOG_INFO, "  Threshold: %f", tf->threshold.load());
	obs_log(LOG_INFO, "  Edge Softness: %f", tf->edgeSoftness.load());
	obs_log(LOG_INFO, "  Edge Refinement: %f", tf->edgeRefinement.load());
	obs_log(LOG_INFO, "  Foreground Cleanup: %f", tf->foregroundCleanup.load());
	obs_log(LOG_INFO, "  Contour Filter: %f", tf->contourFilter.load());
	obs_log(LOG_INFO, "  Smooth Contour: %f", tf->smoothContour.load());
	obs_log(LOG_INFO, "  Mask Expansion: %d", tf->maskExpansion.load());
	obs_log(LOG_INFO, "  Feather: %f", tf->feather.load());
	obs_log(LOG_INFO, "  Mask Preview: %s", tf->showMaskPreview.load() ? "true" : "false");
	obs_log(LOG_INFO, "  Mask Every X Frames: %d", tf->maskEveryXFrames.load());
	obs_log(LOG_INFO, "  Enable Image Similarity: %s", tf->enableImageSimilarity.load() ? "true" : "false");
	obs_log(LOG_INFO, "  Image Similarity Threshold: %f", tf->imageSimilarityThreshold.load());
	obs_log(LOG_INFO, "  Blur Background: %lld", static_cast<long long>(tf->blurBackground.load()));
	obs_log(LOG_INFO, "  Enable Focal Blur: %s", tf->enableFocalBlur.load() ? "true" : "false");
	obs_log(LOG_INFO, "  Blur Focus Point: %f", tf->blurFocusPoint.load());
	obs_log(LOG_INFO, "  Blur Focus Depth: %f", tf->blurFocusDepth.load());
	obs_log(LOG_INFO, "  Disabled: %s", tf->isDisabled ? "true" : "false");
#ifdef _WIN32
	obs_log(LOG_INFO, "  Model file path: %S", modelFilepathForLog.c_str());
#else
	obs_log(LOG_INFO, "  Model file path: %s", modelFilepathForLog.c_str());
#endif

	// enable
	tf->isDisabled = false;
}

void background_filter_activate(void *data)
{
	auto *ptr = static_cast<std::shared_ptr<bgobs_background_filter> *>(data);
	if (!ptr) {
		return;
	}

	std::shared_ptr<bgobs_background_filter> tf = *ptr;
	if (tf && tf->stopWhenSourceIsInactive) {
		obs_log(LOG_INFO, "BGOBS background filter activated");
		tf->isDisabled = false;
		tf->maskEveryXFramesCount.store(0, std::memory_order_relaxed);
		invalidateInference(tf.get());
		std::lock_guard<std::mutex> lock(tf->modelMutex);
		resetFilterModelState(tf.get());
	}
}

void background_filter_deactivate(void *data)
{
	auto *ptr = static_cast<std::shared_ptr<bgobs_background_filter> *>(data);
	if (!ptr) {
		return;
	}

	std::shared_ptr<bgobs_background_filter> tf = *ptr;
	if (tf && tf->stopWhenSourceIsInactive) {
		obs_log(LOG_INFO, "BGOBS background filter deactivated");
		tf->isDisabled = true;
		tf->maskEveryXFramesCount.store(0, std::memory_order_relaxed);
		invalidateInference(tf.get());
	}
}

/**                   FILTER CORE                     */

void *background_filter_create(obs_data_t *settings, obs_source_t *source)
{
	obs_log(LOG_INFO, "BGOBS background filter created");
	try {
		// Create the instance as a shared_ptr
		auto instance = std::make_shared<bgobs_background_filter>();

		instance->source = source;
		instance->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);

		std::string instanceName{"bgobs-background-inference"};
		instance->env.reset(new Ort::Env(OrtLoggingLevel::ORT_LOGGING_LEVEL_ERROR, instanceName.c_str()));

		instance->modelSelection = MODEL_MEDIAPIPE;

		// Create pointer to shared_ptr for the update call
		auto ptr = new std::shared_ptr<bgobs_background_filter>(instance);
		background_filter_update(ptr, settings);
		startInferenceWorker(instance);

		// Return the pointer to the shared_ptr
		// This keeps the reference count at least 1 until destroy is called
		return ptr;
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "Failed to create BGOBS background filter: %s", e.what());
		return nullptr;
	}
}

void background_filter_destroy(void *data)
{
	obs_log(LOG_INFO, "BGOBS background filter destroyed");

	// Cast back to shared_ptr pointer
	auto *ptr = static_cast<std::shared_ptr<bgobs_background_filter> *>(data);
	if (ptr) {
		if (*ptr) {
			// Mark as disabled to prevent further processing
			(*ptr)->isDisabled = true;
			invalidateInference(ptr->get());
			stopInferenceWorker(*ptr);

			// Perform cleanup
			obs_enter_graphics();
			gs_texrender_destroy((*ptr)->texrender);
			if ((*ptr)->stagesurface) {
				gs_stagesurface_destroy((*ptr)->stagesurface);
			}
			gs_effect_destroy((*ptr)->effect);
			gs_effect_destroy((*ptr)->kawaseBlurEffect);
			gs_texture_destroy((*ptr)->alphaTexture);
			gs_texture_destroy((*ptr)->blurTexture);
			obs_leave_graphics();
		}
		// Delete the pointer to shared_ptr
		// This decrements the ref count. If no other threads hold a shared_ptr, the instance is deleted
		delete ptr;
	}
}

static void postProcessImageForBackground(struct bgobs_background_filter *tf, const cv::Mat &outputImage,
					  const cv::Mat &sourceImage, const cv::Size &targetSize,
					  cv::Mat &backgroundMask)
{
	bgobs::MaskPostProcessingSettings settings;
	settings.enableThreshold = tf->enableThreshold;
	settings.threshold = tf->threshold;
	settings.edgeSoftness = tf->edgeSoftness;
	settings.foregroundCleanup = tf->foregroundCleanup;
	settings.contourFilter = tf->contourFilter;
	settings.smoothContour = tf->smoothContour;
	settings.feather = tf->feather;
	settings.maskExpansion = tf->maskExpansion;

	backgroundMask = bgobs::postProcessForegroundMask(outputImage, targetSize, settings);

	bgobs::ImageGuidedMaskRefinementSettings refinementSettings;
	refinementSettings.edgeRefinement = tf->edgeRefinement;
	backgroundMask = bgobs::refineBackgroundMaskWithImage(backgroundMask, sourceImage, refinementSettings);
}

static void invalidateInference(struct bgobs_background_filter *tf)
{
	tf->inferenceGeneration.fetch_add(1, std::memory_order_acq_rel);
	{
		std::lock_guard<std::mutex> lock(tf->inferenceQueueMutex);
		tf->pendingInferenceFrame.release();
		tf->pendingInferenceGeneration = tf->inferenceGeneration.load(std::memory_order_acquire);
	}
	{
		std::lock_guard<std::mutex> lock(tf->outputLock);
		tf->backgroundMask.release();
		tf->backgroundAlpha.release();
		tf->lastBackgroundMask.release();
	}
	{
		std::lock_guard<std::mutex> lock(tf->legacySimilarityMutex);
		tf->lastImageBGRA.release();
	}
	tf->maskEveryXFramesCount.store(0, std::memory_order_relaxed);
	tf->inferenceQueueCondition.notify_one();
}

static void inferenceWorkerLoop(std::shared_ptr<bgobs_background_filter> tf)
{
	for (;;) {
		cv::Mat frame;
		uint64_t generation = 0;
		{
			std::unique_lock<std::mutex> lock(tf->inferenceQueueMutex);
			tf->inferenceQueueCondition.wait(lock, [&] {
				return tf->stopInferenceWorker || !tf->pendingInferenceFrame.empty();
			});
			if (tf->stopInferenceWorker) {
				return;
			}
			frame = std::move(tf->pendingInferenceFrame);
			generation = tf->pendingInferenceGeneration;
		}

		if (frame.empty() || tf->isDisabled || generation != tf->inferenceGeneration.load()) {
			continue;
		}

		const auto started = std::chrono::steady_clock::now();
		try {
			const bool nativeAlpha = tf->nativeAlpha.load();
			// Preserve the legacy image-similarity optimization for segmentation
			// modes. RVM must receive every submitted frame to keep its recurrent
			// state coherent, while legacy filters can safely reuse their mask.
			if (!nativeAlpha) {
				std::lock_guard<std::mutex> lock(tf->legacySimilarityMutex);
				if (tf->enableImageSimilarity && !tf->lastImageBGRA.empty() &&
				    tf->lastImageBGRA.size() == frame.size() &&
				    cv::PSNR(tf->lastImageBGRA, frame) > tf->imageSimilarityThreshold) {
					continue;
				}
				tf->lastImageBGRA = frame.clone();
			}
			cv::Mat output;
			bool inferenceSucceeded = false;
			{
				std::unique_lock<std::mutex> lock(tf->modelMutex);
				if (!tf->model || generation != tf->inferenceGeneration.load()) {
					continue;
				}
				if (nativeAlpha) {
					tf->rvmDownsampleRatio = chooseRvmDownsampleRatio(tf->mattingMode, frame.cols, frame.rows);
					inferenceSucceeded = runFilterModelInferenceFloat(tf.get(), frame, output);
				} else {
					inferenceSucceeded = runFilterModelInference(tf.get(), frame, output);
				}
			}

			if (!inferenceSucceeded || generation != tf->inferenceGeneration.load()) {
				continue;
			}

			if (nativeAlpha) {
				if (output.type() != CV_32FC1) {
					obs_log(LOG_WARNING, "RVM returned a non-floating alpha; frame discarded");
					continue;
				}
				std::lock_guard<std::mutex> lock(tf->outputLock);
				if (generation == tf->inferenceGeneration.load()) {
					tf->backgroundAlpha = std::move(output);
					tf->backgroundMask.release();
					tf->backgroundAlphaGeneration = generation;
				}
			} else {
				cv::Mat backgroundMask;
				postProcessImageForBackground(tf.get(), output, frame, frame.size(), backgroundMask);
				if (backgroundMask.empty()) {
					continue;
				}
				bgobs::TemporalMaskSmoothingSettings temporalSettings;
				temporalSettings.temporalSmoothFactor = tf->temporalSmoothFactor;
				cv::Mat previousBackgroundMask;
				{
					std::lock_guard<std::mutex> lock(tf->outputLock);
					if (generation != tf->inferenceGeneration.load()) {
						continue;
					}
					previousBackgroundMask = tf->lastBackgroundMask.clone();
				}
				backgroundMask = bgobs::smoothTemporalBackgroundMask(backgroundMask, previousBackgroundMask,
										   temporalSettings);
				{
					std::lock_guard<std::mutex> lock(tf->outputLock);
					if (generation != tf->inferenceGeneration.load()) {
						continue;
					}
					tf->lastBackgroundMask = backgroundMask.clone();
					backgroundMask.copyTo(tf->backgroundMask);
				}
			}

			const auto finished = std::chrono::steady_clock::now();
			const float elapsedMs = std::chrono::duration<float, std::milli>(finished - started).count();
			tf->lastInferenceMs = elapsedMs;
			tf->completedFrames.fetch_add(1, std::memory_order_relaxed);
		} catch (const Ort::Exception &e) {
			obs_log(LOG_ERROR, "ONNXRuntime Exception in inference worker: %s", e.what());
			std::unique_lock<std::mutex> lock(tf->modelMutex);
			if (tf->useGPU != USEGPU_CPU) {
				obs_log(LOG_WARNING, "GPU inference failed; falling back to CPU");
				tf->useGPU = USEGPU_CPU;
				tf->session.reset();
				resetFilterModelState(tf.get());
				tf->inputTensor.clear();
				tf->outputTensor.clear();
				if (createOrtSession(tf.get()) != OBS_BGREMOVAL_ORT_SESSION_SUCCESS) {
					obs_log(LOG_ERROR, "Failed to create CPU fallback session; disabling filter");
					tf->isDisabled = true;
				}
			}
		} catch (const std::exception &e) {
			obs_log(LOG_ERROR, "BGOBS inference worker exception: %s", e.what());
		}
	}
}

static void startInferenceWorker(const std::shared_ptr<bgobs_background_filter> &tf)
{
	std::lock_guard<std::mutex> lock(tf->inferenceQueueMutex);
	if (tf->inferenceWorker.joinable()) {
		return;
	}
	tf->stopInferenceWorker = false;
	tf->inferenceWorker = std::thread(inferenceWorkerLoop, tf);
}

static void stopInferenceWorker(const std::shared_ptr<bgobs_background_filter> &tf)
{
	{
		std::lock_guard<std::mutex> lock(tf->inferenceQueueMutex);
		tf->stopInferenceWorker = true;
		tf->pendingInferenceFrame.release();
	}
	tf->inferenceQueueCondition.notify_all();
	if (tf->inferenceWorker.joinable()) {
		tf->inferenceWorker.join();
	}
}

static void submitInferenceFrame(const std::shared_ptr<bgobs_background_filter> &tf, cv::Mat frame)
{
	if (frame.empty() || tf->isDisabled) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(tf->inferenceQueueMutex);
		tf->pendingInferenceFrame = std::move(frame);
		tf->pendingInferenceGeneration = tf->inferenceGeneration.load(std::memory_order_acquire);
		tf->submittedFrames.fetch_add(1, std::memory_order_relaxed);
	}
	tf->inferenceQueueCondition.notify_one();
}

void background_filter_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	auto *ptr = static_cast<std::shared_ptr<bgobs_background_filter> *>(data);
	if (!ptr || !*ptr) {
		return;
	}
	std::shared_ptr<bgobs_background_filter> tf = *ptr;
	if (tf->isDisabled || !obs_source_enabled(tf->source)) {
		return;
	}
	cv::Mat imageBGRA;
	{
		std::unique_lock<std::mutex> lock(tf->inputBGRALock, std::try_to_lock);
		if (!lock.owns_lock() || tf->inputBGRA.empty()) {
			return;
		}
		imageBGRA = tf->inputBGRA.clone();
	}
	if (tf->nativeAlpha.load()) {
		tf->maskEveryXFramesCount.store(0, std::memory_order_relaxed);
	} else {
		const int every = std::max(1, tf->maskEveryXFrames.load(std::memory_order_relaxed));
		const int count = (tf->maskEveryXFramesCount.fetch_add(1, std::memory_order_relaxed) + 1) % every;
		if (count != 0) {
			return;
		}
	}
	submitInferenceFrame(tf, std::move(imageBGRA));
}

static gs_texture_t *blur_background(std::shared_ptr<bgobs_background_filter> tf, uint32_t width, uint32_t height,
				     gs_texture_t *alphaTexture)
{
	if (tf->blurBackground == 0 || !tf->kawaseBlurEffect) {
		return nullptr;
	}
	if (!tf->blurTexture || tf->blurTextureWidth != width || tf->blurTextureHeight != height) {
		gs_texture_destroy(tf->blurTexture);
		tf->blurTexture = gs_texture_create(width, height, GS_BGRA, 1, nullptr, 0);
		tf->blurTextureWidth = width;
		tf->blurTextureHeight = height;
	}
	if (!tf->blurTexture)
		return nullptr;
	gs_copy_texture(tf->blurTexture, gs_texrender_get_texture(tf->texrender));
	gs_eparam_t *image = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "image");
	gs_eparam_t *focalmask = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "focalmask");
	gs_eparam_t *xOffset = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "xOffset");
	gs_eparam_t *yOffset = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "yOffset");
	gs_eparam_t *blurIter = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "blurIter");
	gs_eparam_t *blurTotal = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "blurTotal");
	gs_eparam_t *blurFocusPointParam = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "blurFocusPoint");
	gs_eparam_t *blurFocusDepthParam = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "blurFocusDepth");

	for (int i = 0; i < (int)tf->blurBackground; i++) {
		gs_texrender_reset(tf->texrender);
		if (!gs_texrender_begin(tf->texrender, width, height)) {
			obs_log(LOG_INFO, "Could not open background blur texrender!");
			return tf->blurTexture;
		}

		gs_effect_set_texture(image, tf->blurTexture);
		gs_effect_set_texture(focalmask, alphaTexture);
		gs_effect_set_float(xOffset, ((float)i + 0.5f) / (float)width);
		gs_effect_set_float(yOffset, ((float)i + 0.5f) / (float)height);
		gs_effect_set_int(blurIter, i);
		gs_effect_set_int(blurTotal, (int)tf->blurBackground);
		gs_effect_set_float(blurFocusPointParam, tf->blurFocusPoint);
		gs_effect_set_float(blurFocusDepthParam, tf->blurFocusDepth);

		struct vec4 background;
		vec4_zero(&background);
		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		const char *blur_type = (tf->enableFocalBlur) ? "DrawFocalBlur" : "Draw";

		while (gs_effect_loop(tf->kawaseBlurEffect, blur_type)) {
			gs_draw_sprite(tf->blurTexture, 0, width, height);
		}
		gs_blend_state_pop();
		gs_texrender_end(tf->texrender);
		gs_copy_texture(tf->blurTexture, gs_texrender_get_texture(tf->texrender));
	}
	return tf->blurTexture;
}

void background_filter_video_render(void *data, gs_effect_t *_effect)
{
	UNUSED_PARAMETER(_effect);

	// Cast to shared_ptr pointer and create a local shared_ptr
	auto *ptr = static_cast<std::shared_ptr<bgobs_background_filter> *>(data);
	if (!ptr) {
		return;
	}

	// Create a local shared_ptr
	std::shared_ptr<bgobs_background_filter> tf = *ptr;

	if (!tf || tf->isDisabled) {
		if (tf && tf->source) {
			obs_source_skip_video_filter(tf->source);
		}
		return;
	}

	uint32_t width, height;
	if (!getRGBAFromStageSurface(tf.get(), width, height)) {
		if (tf->source) {
			obs_source_skip_video_filter(tf->source);
		}
		return;
	}

	if (!tf->effect) {
		// Effect failed to load, skip rendering
		if (tf->source) {
			obs_source_skip_video_filter(tf->source);
		}
		return;
	}

	{
		std::lock_guard<std::mutex> lock(tf->outputLock);
		const bool useNativeAlpha = tf->nativeAlpha.load() && !tf->backgroundAlpha.empty();
		if (!useNativeAlpha && tf->backgroundMask.empty()) {
			obs_log(LOG_WARNING, "Background mask is empty during render, skipping frame.");
			if (tf->source) {
				obs_source_skip_video_filter(tf->source);
			}
			return;
		}

		const uint32_t maskWidth = width;
		const uint32_t maskHeight = height;
		const gs_color_format requestedFormat = useNativeAlpha ? GS_R16F : GS_R8;
		const bool formatMismatch = useNativeAlpha ?
			(tf->alphaTextureFormat != GS_R16F && tf->alphaTextureFormat != GS_R8) :
			(tf->alphaTextureFormat != GS_R8);
		if (!tf->alphaTexture || tf->alphaTextureWidth != maskWidth || tf->alphaTextureHeight != maskHeight ||
		    formatMismatch) {
			gs_texture_destroy(tf->alphaTexture);
			tf->alphaTexture = gs_texture_create(maskWidth, maskHeight, requestedFormat, 1, nullptr, GS_DYNAMIC);
			if (!tf->alphaTexture && requestedFormat == GS_R16F) {
				// Some older graphics backends do not expose R16F textures. The
				// float processing still protects the alpha until this final upload.
				tf->alphaTexture = gs_texture_create(maskWidth, maskHeight, GS_R8, 1, nullptr, GS_DYNAMIC);
				tf->alphaTextureFormat = GS_R8;
			} else {
				tf->alphaTextureFormat = requestedFormat;
			}
			tf->alphaTextureWidth = maskWidth;
			tf->alphaTextureHeight = maskHeight;
		}

		if (!tf->alphaTexture) {
			obs_log(LOG_ERROR, "Failed to create alpha texture");
			if (tf->source) {
				obs_source_skip_video_filter(tf->source);
			}
			return;
		}

		if (useNativeAlpha) {
			cv::Mat alpha = tf->backgroundAlpha;
			if (alpha.cols != static_cast<int>(maskWidth) || alpha.rows != static_cast<int>(maskHeight)) {
				cv::resize(alpha, alpha, cv::Size(maskWidth, maskHeight), 0.0, 0.0, cv::INTER_LINEAR);
			}
			cv::min(alpha, 1.0f, alpha);
			cv::max(alpha, 0.0f, alpha);
			// The effect consumes a background mask (1 = background), whereas
			// RVM returns foreground alpha (1 = subject).
			alpha = 1.0f - alpha;
			if (tf->alphaTextureFormat == GS_R16F) {
				tf->alphaUploadBuffer.resize(static_cast<size_t>(maskWidth) * maskHeight);
				for (size_t i = 0; i < tf->alphaUploadBuffer.size(); ++i) {
					tf->alphaUploadBuffer[i] = floatToHalf(alpha.ptr<float>()[i]);
				}
				gs_texture_set_image(tf->alphaTexture,
						     reinterpret_cast<const uint8_t *>(tf->alphaUploadBuffer.data()),
						     maskWidth * sizeof(uint16_t), false);
			} else {
				cv::Mat alpha8;
				alpha.convertTo(alpha8, CV_8UC1, 255.0);
				gs_texture_set_image(tf->alphaTexture, alpha8.data,
						     static_cast<uint32_t>(alpha8.step[0]), false);
			}
		} else {
			cv::Mat mask = tf->backgroundMask;
			if (mask.cols != static_cast<int>(maskWidth) || mask.rows != static_cast<int>(maskHeight)) {
				cv::resize(mask, mask, cv::Size(maskWidth, maskHeight), 0.0, 0.0, cv::INTER_LINEAR);
			}
			gs_texture_set_image(tf->alphaTexture, mask.data, static_cast<uint32_t>(mask.step[0]), false);
		}
	}

	// Output the masked image. Mask preview is a diagnostic path and should not spend time rendering blur passes.
	gs_texture_t *blurredTexture = tf->showMaskPreview ? nullptr
							   : blur_background(tf, width, height, tf->alphaTexture);

	const enum gs_color_space sourceColorSpace = obs_source_get_color_space(tf->source, 0, nullptr);
	if (!obs_source_process_filter_begin_with_color_space(tf->source, GS_RGBA, sourceColorSpace,
								 OBS_ALLOW_DIRECT_RENDERING)) {
		if (tf->source) {
			obs_source_skip_video_filter(tf->source);
		}
		return;
	}

	gs_eparam_t *alphamask = gs_effect_get_param_by_name(tf->effect, "alphamask");
	gs_eparam_t *blurredBackground = gs_effect_get_param_by_name(tf->effect, "blurredBackground");

	gs_effect_set_texture(alphamask, tf->alphaTexture);

	if (tf->blurBackground > 0) {
		gs_effect_set_texture(blurredBackground, blurredTexture);
	}

	gs_blend_state_push();
	gs_reset_blend_state();

	const char *techName;
	if (tf->showMaskPreview) {
		techName = "DrawMaskPreview";
	} else if (tf->blurBackground > 0) {
		if (tf->enableFocalBlur)
			techName = "DrawWithFocalBlur";
		else
			techName = "DrawWithBlur";
	} else {
		techName = "DrawWithoutBlur";
	}

	obs_source_process_filter_tech_end(tf->source, tf->effect, 0, 0, techName);

	gs_blend_state_pop();
}
