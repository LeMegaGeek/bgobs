/*
 * SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cacam-usb-source.hpp"

#include "plugin-support.h"

#include <obs-module.h>
#include <util/platform.h>

#include <curl/curl.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/types.h>
#endif

namespace {

#ifdef _WIN32
using libusb_ssize_t = intptr_t;
#else
using libusb_ssize_t = ssize_t;
#endif

struct libusb_context;
struct libusb_device;
struct libusb_device_handle;

struct libusb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
};

struct libusb_endpoint_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
	uint8_t bRefresh;
	uint8_t bSynchAddress;
	const unsigned char *extra;
	int extra_length;
};

struct libusb_interface_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
	const libusb_endpoint_descriptor *endpoint;
	const unsigned char *extra;
	int extra_length;
};

struct libusb_interface {
	const libusb_interface_descriptor *altsetting;
	int num_altsetting;
};

struct libusb_config_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t MaxPower;
	const libusb_interface *interface;
	const unsigned char *extra;
	int extra_length;
};

constexpr uint16_t GOOGLE_VENDOR_ID = 0x18d1;
constexpr uint16_t AOA_PID_ACCESSORY = 0x2d00;
constexpr uint16_t AOA_PID_ACCESSORY_ADB = 0x2d01;
constexpr uint16_t AOA_PID_AUDIO = 0x2d02;
constexpr uint16_t AOA_PID_AUDIO_ADB = 0x2d03;
constexpr uint16_t AOA_PID_ACCESSORY_AUDIO = 0x2d04;
constexpr uint16_t AOA_PID_ACCESSORY_AUDIO_ADB = 0x2d05;

constexpr uint8_t LIBUSB_ENDPOINT_IN = 0x80;
constexpr uint8_t LIBUSB_ENDPOINT_OUT = 0x00;
constexpr uint8_t LIBUSB_REQUEST_TYPE_VENDOR = 0x40;
constexpr uint8_t LIBUSB_RECIPIENT_DEVICE = 0x00;
constexpr uint8_t LIBUSB_TRANSFER_TYPE_MASK = 0x03;
constexpr uint8_t LIBUSB_TRANSFER_TYPE_BULK = 0x02;
constexpr int LIBUSB_ERROR_TIMEOUT = -7;
constexpr int LIBUSB_ERROR_NO_DEVICE = -4;

constexpr uint8_t AOA_GET_PROTOCOL = 51;
constexpr uint8_t AOA_SEND_IDENT = 52;
constexpr uint8_t AOA_START = 53;

constexpr uint32_t CACAM_MAGIC = 0x4343414d;
constexpr uint8_t CACAM_PROTOCOL_VERSION = 1;
constexpr uint8_t CACAM_TYPE_HELLO = 1;
constexpr uint8_t CACAM_TYPE_NV21 = 3;
constexpr size_t CACAM_HEADER_SIZE = 20;
constexpr size_t CACAM_FRAME_METADATA_SIZE = 16;
constexpr uint32_t MAX_PAYLOAD_SIZE = 16 * 1024 * 1024;
constexpr size_t BULK_READ_BUFFER_SIZE = 1024 * 1024;
constexpr int64_t MAX_FRAME_AGE_US = 150 * 1000;
constexpr int64_t MAX_CLOCK_BASELINE_STEP_US = 100;

constexpr int BULK_TIMEOUT_MS = 1000;
constexpr int CONTROL_TIMEOUT_MS = 1000;
constexpr int RETRY_DELAY_MS = 1200;
constexpr int NO_DATA_WARNING_MS = 5000;
constexpr int ADB_LOCAL_PORT_DEFAULT = 18080;
constexpr int ADB_DEVICE_PORT_DEFAULT = 8080;
constexpr int ADB_SNAPSHOT_TIMEOUT_MS = 1200;
constexpr int ADB_HEALTH_TIMEOUT_MS = 800;
constexpr int ADB_HEALTH_RETRY_COUNT = 30;
constexpr int ADB_COMMAND_TIMEOUT_MS = 30000;
constexpr const char *CACAM_PACKAGE_NAME = "fr.lemegageek.cacam";
constexpr uint32_t DEFAULT_SOURCE_WIDTH = 1280;
constexpr uint32_t DEFAULT_SOURCE_HEIGHT = 720;

std::pair<uint32_t, uint32_t> source_canvas_size()
{
	struct obs_video_info video_info = {};
	if (obs_get_video_info(&video_info) && video_info.base_width > 0 && video_info.base_height > 0)
		return {video_info.base_width, video_info.base_height};
	return {DEFAULT_SOURCE_WIDTH, DEFAULT_SOURCE_HEIGHT};
}

enum class CacamConnectionMode {
	Usb,
	Adb,
};

enum class CacamQuality {
	Pourri,
	Low,
	Standard,
	Hd,
	Uhd,
};

struct CacamQualityInfo {
	CacamQuality quality;
	const char *setting;
	const char *intent_value;
	uint32_t width;
	uint32_t height;
	int fps;
};

constexpr CacamQualityInfo QUALITY_INFOS[] = {
	{CacamQuality::Pourri, "pourri", "Pourri", 426, 240, 8},
	{CacamQuality::Low, "low", "Low", 640, 360, 12},
	{CacamQuality::Standard, "standard", "Standard", 854, 480, 12},
	{CacamQuality::Hd, "hd", "Hd", 1920, 1080, 20},
	{CacamQuality::Uhd, "uhd", "Uhd", 3840, 2160, 15},
};

struct UsbApi {
#ifdef _WIN32
	HMODULE library = nullptr;
#else
	void *library = nullptr;
#endif
	int (*init)(libusb_context **) = nullptr;
	void (*exit)(libusb_context *) = nullptr;
	libusb_ssize_t (*get_device_list)(libusb_context *, libusb_device ***) = nullptr;
	void (*free_device_list)(libusb_device **, int) = nullptr;
	int (*get_device_descriptor)(libusb_device *, libusb_device_descriptor *) = nullptr;
	int (*open)(libusb_device *, libusb_device_handle **) = nullptr;
	void (*close)(libusb_device_handle *) = nullptr;
	int (*control_transfer)(libusb_device_handle *, uint8_t, uint8_t, uint16_t, uint16_t, unsigned char *, uint16_t,
				unsigned int) = nullptr;
	int (*get_active_config_descriptor)(libusb_device *, libusb_config_descriptor **) = nullptr;
	void (*free_config_descriptor)(libusb_config_descriptor *) = nullptr;
	int (*claim_interface)(libusb_device_handle *, int) = nullptr;
	int (*release_interface)(libusb_device_handle *, int) = nullptr;
	int (*bulk_transfer)(libusb_device_handle *, unsigned char, unsigned char *, int, int *,
			     unsigned int) = nullptr;
	int (*kernel_driver_active)(libusb_device_handle *, int) = nullptr;
	int (*detach_kernel_driver)(libusb_device_handle *, int) = nullptr;
	const char *(*error_name)(int) = nullptr;

	~UsbApi() { unload(); }

	bool load()
	{
		if (library)
			return true;

#ifdef _WIN32
		const std::string directory = module_directory();
		if (!directory.empty()) {
			const std::string path = directory + "\\libusb-1.0.dll";
			library = LoadLibraryA(path.c_str());
		}
		if (!library)
			library = LoadLibraryA("libusb-1.0.dll");
#else
		library = dlopen("libusb-1.0.so.0", RTLD_NOW | RTLD_LOCAL);
		if (!library)
			library = dlopen("libusb-1.0.dylib", RTLD_NOW | RTLD_LOCAL);
#endif
		if (!library)
			return false;

		const bool symbols_loaded =
			loadSymbol(init, "libusb_init") && loadSymbol(exit, "libusb_exit") &&
			loadSymbol(get_device_list, "libusb_get_device_list") &&
			loadSymbol(free_device_list, "libusb_free_device_list") &&
			loadSymbol(get_device_descriptor, "libusb_get_device_descriptor") &&
			loadSymbol(open, "libusb_open") && loadSymbol(close, "libusb_close") &&
			loadSymbol(control_transfer, "libusb_control_transfer") &&
			loadSymbol(get_active_config_descriptor, "libusb_get_active_config_descriptor") &&
			loadSymbol(free_config_descriptor, "libusb_free_config_descriptor") &&
			loadSymbol(claim_interface, "libusb_claim_interface") &&
			loadSymbol(release_interface, "libusb_release_interface") &&
			loadSymbol(bulk_transfer, "libusb_bulk_transfer") &&
			loadSymbol(kernel_driver_active, "libusb_kernel_driver_active") &&
			loadSymbol(detach_kernel_driver, "libusb_detach_kernel_driver") &&
			loadSymbol(error_name, "libusb_error_name");
		if (!symbols_loaded)
			unload();
		return symbols_loaded;
	}

	void unload()
	{
		if (!library)
			return;
#ifdef _WIN32
		FreeLibrary(library);
#else
		dlclose(library);
#endif
		library = nullptr;
	}

	template<typename T> bool loadSymbol(T &target, const char *name)
	{
#ifdef _WIN32
		target = reinterpret_cast<T>(GetProcAddress(library, name));
#else
		target = reinterpret_cast<T>(dlsym(library, name));
#endif
		return target != nullptr;
	}

#ifdef _WIN32
	static std::string module_directory()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
						GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCSTR>(&module_directory), &module)) {
			return {};
		}

		char path[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameA(module, path, static_cast<DWORD>(sizeof(path)));
		if (length == 0 || length >= sizeof(path))
			return {};

		std::string value(path, length);
		const size_t separator = value.find_last_of("\\/");
		return separator == std::string::npos ? std::string{} : value.substr(0, separator);
	}
#endif
};

const char *usb_error_name(const UsbApi &api, int result)
{
	return api.error_name ? api.error_name(result) : "unknown libusb error";
}

struct AccessoryConnection {
	libusb_device_handle *handle = nullptr;
	int interface_number = -1;
	unsigned char endpoint_in = 0;

	void close(UsbApi &api)
	{
		if (handle && interface_number >= 0)
			api.release_interface(handle, interface_number);
		if (handle)
			api.close(handle);
		handle = nullptr;
		interface_number = -1;
		endpoint_in = 0;
	}
};

bool is_aoa_accessory_interface(const libusb_interface_descriptor &descriptor)
{
	return descriptor.bInterfaceClass == 0xff && descriptor.bInterfaceSubClass == 0xff &&
	       descriptor.bInterfaceProtocol == 0x00;
}

bool is_adb_interface(const libusb_interface_descriptor &descriptor)
{
	return descriptor.bInterfaceClass == 0xff && descriptor.bInterfaceSubClass == 0x42 &&
	       descriptor.bInterfaceProtocol == 0x01;
}

bool is_aoa_product(uint16_t vendor, uint16_t product)
{
	if (vendor != GOOGLE_VENDOR_ID)
		return false;

	switch (product) {
	case AOA_PID_ACCESSORY:
	case AOA_PID_ACCESSORY_ADB:
	case AOA_PID_AUDIO:
	case AOA_PID_AUDIO_ADB:
	case AOA_PID_ACCESSORY_AUDIO:
	case AOA_PID_ACCESSORY_AUDIO_ADB:
		return true;
	default:
		return false;
	}
}

uint16_t read_u16_le(const unsigned char *data)
{
	return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8);
}

uint32_t read_u32_be(const unsigned char *data)
{
	return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
	       (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

uint64_t read_u64_be(const unsigned char *data)
{
	uint64_t value = 0;
	for (size_t index = 0; index < 8; ++index)
		value = (value << 8) | data[index];
	return value;
}

std::string plugin_module_directory()
{
#ifdef _WIN32
	HMODULE module = nullptr;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
						GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCSTR>(&plugin_module_directory), &module)) {
		return {};
	}

	char path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameA(module, path, static_cast<DWORD>(sizeof(path)));
	if (length == 0 || length >= sizeof(path))
		return {};

	std::string value(path, length);
	const size_t separator = value.find_last_of("\\/");
	return separator == std::string::npos ? std::string{} : value.substr(0, separator);
#else
	return {};
#endif
}

std::string default_adb_path()
{
#ifdef _WIN32
	const std::string directory = plugin_module_directory();
	if (!directory.empty()) {
		const std::string bundled = directory + "\\adb\\adb.exe";
		const DWORD attributes = GetFileAttributesA(bundled.c_str());
		if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			return bundled;
	}
	return "adb.exe";
#else
	return "adb";
#endif
}

CacamConnectionMode parse_connection_mode(const char *value)
{
	const std::string mode = value ? value : "";
	if (mode == "adb")
		return CacamConnectionMode::Adb;
	return CacamConnectionMode::Usb;
}

const char *connection_mode_setting(CacamConnectionMode mode)
{
	return mode == CacamConnectionMode::Adb ? "adb" : "usb";
}

const CacamQualityInfo &quality_info(CacamQuality quality)
{
	for (const CacamQualityInfo &info : QUALITY_INFOS) {
		if (info.quality == quality)
			return info;
	}
	return QUALITY_INFOS[2];
}

CacamQuality parse_quality(const char *value)
{
	const std::string setting = value ? value : "";
	for (const CacamQualityInfo &info : QUALITY_INFOS) {
		if (setting == info.setting)
			return info.quality;
	}
	return CacamQuality::Standard;
}

std::string obs_data_string(obs_data_t *settings, const char *name)
{
	const char *value = obs_data_get_string(settings, name);
	return value ? value : "";
}

std::string shell_quote(const std::string &value)
{
#ifdef _WIN32
	std::string quoted = "\"";
	for (const char character : value) {
		if (character == '"')
			quoted += "\\\"";
		else
			quoted += character;
	}
	quoted += "\"";
	return quoted;
#else
	std::string quoted = "'";
	for (const char character : value) {
		if (character == '\'')
			quoted += "'\\''";
		else
			quoted += character;
	}
	quoted += "'";
	return quoted;
#endif
}

std::string run_command_capture(const std::string &command, int *exit_code = nullptr)
{
	std::string output;
#ifdef _WIN32
	SECURITY_ATTRIBUTES security_attributes = {};
	security_attributes.nLength = sizeof(security_attributes);
	security_attributes.bInheritHandle = TRUE;

	HANDLE read_pipe = nullptr;
	HANDLE write_pipe = nullptr;
	if (!CreatePipe(&read_pipe, &write_pipe, &security_attributes, 0)) {
		if (exit_code)
			*exit_code = static_cast<int>(GetLastError());
		return output;
	}
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOA startup_info = {};
	startup_info.cb = sizeof(startup_info);
	startup_info.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	startup_info.hStdOutput = write_pipe;
	startup_info.hStdError = write_pipe;
	startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup_info.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION process_info = {};
	std::vector<char> command_line(command.begin(), command.end());
	command_line.push_back('\0');

	if (!CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
			    &startup_info, &process_info)) {
		const DWORD error = GetLastError();
		CloseHandle(write_pipe);
		CloseHandle(read_pipe);
		if (exit_code)
			*exit_code = static_cast<int>(error);
		output = "CreateProcess failed with Windows error " + std::to_string(error);
		return output;
	}

	CloseHandle(write_pipe);

	std::array<char, 512> buffer = {};
	bool timed_out = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ADB_COMMAND_TIMEOUT_MS);
	for (;;) {
		DWORD available = 0;
		if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr))
			break;

		while (available > 0) {
			DWORD bytes_read = 0;
			const DWORD to_read = std::min<DWORD>(static_cast<DWORD>(buffer.size()), available);
			if (!ReadFile(read_pipe, buffer.data(), to_read, &bytes_read, nullptr) || bytes_read == 0)
				break;
			output.append(buffer.data(), bytes_read);
			available -= bytes_read;
		}

		if (WaitForSingleObject(process_info.hProcess, 0) == WAIT_OBJECT_0) {
			if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0)
				break;
			continue;
		}

		if (std::chrono::steady_clock::now() >= deadline) {
			timed_out = true;
			TerminateProcess(process_info.hProcess, 1460);
			WaitForSingleObject(process_info.hProcess, 1000);
			break;
		}

		Sleep(25);
	}

	WaitForSingleObject(process_info.hProcess, INFINITE);
	DWORD process_exit_code = 0;
	GetExitCodeProcess(process_info.hProcess, &process_exit_code);
	CloseHandle(process_info.hThread);
	CloseHandle(process_info.hProcess);
	CloseHandle(read_pipe);

	if (timed_out)
		output += "Command timed out";
	if (exit_code)
		*exit_code = timed_out ? -2 : static_cast<int>(process_exit_code);
	return output;
#else
	FILE *pipe = popen((command + " 2>&1").c_str(), "r");
	if (!pipe) {
		if (exit_code)
			*exit_code = -1;
		return output;
	}

	std::array<char, 512> buffer = {};
	while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
		output += buffer.data();

	const int close_code = pclose(pipe);
	if (exit_code)
		*exit_code = close_code;
	return output;
#endif
}

std::string adb_command(const std::string &adb_path, const std::string &serial, const std::string &arguments)
{
	std::string command = shell_quote(adb_path.empty() ? default_adb_path() : adb_path);
	if (!serial.empty())
		command += " -s " + shell_quote(serial);
	if (!arguments.empty())
		command += " " + arguments;
	return command;
}

std::vector<std::string> adb_device_serials(const std::string &devices_output)
{
	std::vector<std::string> serials;
	std::istringstream stream(devices_output);
	std::string line;
	while (std::getline(stream, line)) {
		if (line.find("\tdevice") == std::string::npos)
			continue;
		const size_t separator = line.find_first_of(" \t");
		if (separator == std::string::npos || separator == 0)
			continue;
		serials.push_back(line.substr(0, separator));
	}
	return serials;
}

size_t curl_write_vector(void *contents, size_t size, size_t nmemb, void *user_data)
{
	const size_t byte_count = size * nmemb;
	auto *output = static_cast<std::vector<unsigned char> *>(user_data);
	const auto *bytes = static_cast<unsigned char *>(contents);
	output->insert(output->end(), bytes, bytes + byte_count);
	return byte_count;
}

bool http_get_binary(const std::string &url, long timeout_ms, std::vector<unsigned char> &response)
{
	CURL *curl = curl_easy_init();
	if (!curl)
		return false;

	response.clear();
	const std::string user_agent = std::string(PLUGIN_NAME) + "/" + PLUGIN_VERSION;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_vector);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	const CURLcode code = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);
	return code == CURLE_OK && http_code >= 200 && http_code < 300 && !response.empty();
}

int send_aoa_string(UsbApi &api, libusb_device_handle *handle, uint16_t index, const char *value)
{
	std::vector<unsigned char> bytes(std::strlen(value) + 1);
	std::memcpy(bytes.data(), value, bytes.size());
	return api.control_transfer(handle, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
				    AOA_SEND_IDENT, 0, index, bytes.data(), static_cast<uint16_t>(bytes.size()),
				    CONTROL_TIMEOUT_MS);
}

enum class AccessoryRequestStatus {
	NotSupported,
	Requested,
	OpenFailed,
	StartFailed,
};

struct AccessoryRequestResult {
	AccessoryRequestStatus status = AccessoryRequestStatus::NotSupported;
	uint16_t vendor = 0;
	uint16_t product = 0;
	uint16_t protocol = 0;
	int error = 0;
};

enum class AccessoryOpenStatus {
	NotFound,
	Opened,
	OpenFailed,
	AccessoryInterfaceInaccessible,
};

struct AccessoryOpenResult {
	AccessoryOpenStatus status = AccessoryOpenStatus::NotFound;
	uint16_t vendor = 0;
	uint16_t product = 0;
	int interface_number = -1;
	int error = 0;
	bool saw_adb_interface = false;
};

bool is_likely_android_vendor(uint16_t vendor)
{
	switch (vendor) {
	case 0x04e8: // Samsung
	case 0x0bb4: // HTC
	case 0x12d1: // Huawei
	case 0x18d1: // Google
	case 0x22b8: // Motorola
	case 0x2717: // Xiaomi
	case 0x2a70: // OnePlus
	case 0x2d95: // vivo
	case 0x2e17: // OPPO
		return true;
	default:
		return false;
	}
}

AccessoryRequestResult request_accessory_mode(UsbApi &api, libusb_device *device)
{
	libusb_device_descriptor descriptor = {};
	if (api.get_device_descriptor(device, &descriptor) != 0)
		return {};
	if (is_aoa_product(descriptor.idVendor, descriptor.idProduct))
		return {};

	AccessoryRequestResult request = {};
	request.vendor = descriptor.idVendor;
	request.product = descriptor.idProduct;

	libusb_device_handle *handle = nullptr;
	const int open_result = api.open(device, &handle);
	if (open_result != 0 || !handle) {
		const int error = open_result != 0 ? open_result : LIBUSB_ERROR_NO_DEVICE;
		if (is_likely_android_vendor(descriptor.idVendor) && error != LIBUSB_ERROR_NO_DEVICE) {
			request.status = AccessoryRequestStatus::OpenFailed;
			request.error = error;
		}
		return request;
	}

	unsigned char version_bytes[2] = {};
	const int protocol_result =
		api.control_transfer(handle, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
				     AOA_GET_PROTOCOL, 0, 0, version_bytes, sizeof(version_bytes), CONTROL_TIMEOUT_MS);
	if (protocol_result < 2 || read_u16_le(version_bytes) == 0) {
		api.close(handle);
		return request;
	}
	request.protocol = read_u16_le(version_bytes);

	const char *identifiers[] = {
		"LeMegaGeek",
		"CaCam USB",
		"CaCam direct USB video source",
		"1.0",
		"https://github.com/LeMegaGeek/CaCam",
		"CaCam",
	};
	for (uint16_t index = 0; index < std::size(identifiers); ++index) {
		const int ident_result = send_aoa_string(api, handle, index, identifiers[index]);
		if (ident_result >= 0)
			continue;
		api.close(handle);
		request.status = AccessoryRequestStatus::StartFailed;
		request.error = ident_result;
		return request;
	}

	const int start_result =
		api.control_transfer(handle, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
				     AOA_START, 0, 0, nullptr, 0, CONTROL_TIMEOUT_MS);

	api.close(handle);
	request.status = start_result >= 0 ? AccessoryRequestStatus::Requested : AccessoryRequestStatus::StartFailed;
	request.error = start_result;
	return request;
}

AccessoryOpenResult select_accessory_endpoint(UsbApi &api, libusb_device *device, libusb_device_handle *handle,
					      AccessoryConnection &connection)
{
	AccessoryOpenResult result = {};

	libusb_config_descriptor *config = nullptr;
	if (api.get_active_config_descriptor(device, &config) != 0 || !config)
		return result;

	for (uint8_t interface_index = 0; interface_index < config->bNumInterfaces; ++interface_index) {
		const libusb_interface &interface = config->interface[interface_index];
		for (int alt_index = 0; alt_index < interface.num_altsetting; ++alt_index) {
			const libusb_interface_descriptor &descriptor = interface.altsetting[alt_index];
			if (is_adb_interface(descriptor)) {
				result.saw_adb_interface = true;
				continue;
			}
			if (!is_aoa_accessory_interface(descriptor))
				continue;

			unsigned char endpoint_in = 0;
			for (uint8_t endpoint_index = 0; endpoint_index < descriptor.bNumEndpoints; ++endpoint_index) {
				const libusb_endpoint_descriptor &endpoint = descriptor.endpoint[endpoint_index];
				const bool is_bulk = (endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
						     LIBUSB_TRANSFER_TYPE_BULK;
				const bool is_in = (endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN) != 0;
				if (is_bulk && is_in)
					endpoint_in = endpoint.bEndpointAddress;
			}
			if (endpoint_in == 0)
				continue;

			const int interface_number = descriptor.bInterfaceNumber;
			if (api.kernel_driver_active(handle, interface_number) == 1)
				api.detach_kernel_driver(handle, interface_number);
			const int claim_result = api.claim_interface(handle, interface_number);
			if (claim_result == 0) {
				connection.handle = handle;
				connection.interface_number = interface_number;
				connection.endpoint_in = endpoint_in;
				result.status = AccessoryOpenStatus::Opened;
				result.interface_number = interface_number;
				api.free_config_descriptor(config);
				return result;
			}

			result.status = AccessoryOpenStatus::AccessoryInterfaceInaccessible;
			result.interface_number = interface_number;
			result.error = claim_result;
		}
	}

	api.free_config_descriptor(config);
	return result;
}

AccessoryOpenResult open_accessory(UsbApi &api, libusb_context *context, AccessoryConnection &connection)
{
	libusb_device **devices = nullptr;
	const libusb_ssize_t count = api.get_device_list(context, &devices);
	if (count < 0)
		return {};

	AccessoryOpenResult first_failure = {};
	for (libusb_ssize_t index = 0; index < count; ++index) {
		libusb_device *device = devices[index];
		libusb_device_descriptor descriptor = {};
		if (api.get_device_descriptor(device, &descriptor) != 0 ||
		    !is_aoa_product(descriptor.idVendor, descriptor.idProduct)) {
			continue;
		}

		libusb_device_handle *handle = nullptr;
		const int open_result = api.open(device, &handle);
		if (open_result != 0 || !handle) {
			if (first_failure.status == AccessoryOpenStatus::NotFound) {
				first_failure.status = AccessoryOpenStatus::OpenFailed;
				first_failure.vendor = descriptor.idVendor;
				first_failure.product = descriptor.idProduct;
				first_failure.error = open_result != 0 ? open_result : LIBUSB_ERROR_NO_DEVICE;
			}
			continue;
		}

		AccessoryOpenResult result = select_accessory_endpoint(api, device, handle, connection);
		result.vendor = descriptor.idVendor;
		result.product = descriptor.idProduct;
		if (result.status == AccessoryOpenStatus::Opened) {
			api.free_device_list(devices, 1);
			return result;
		}
		if (first_failure.status == AccessoryOpenStatus::NotFound &&
		    (result.status != AccessoryOpenStatus::NotFound || result.saw_adb_interface)) {
			first_failure = result;
		}

		api.close(handle);
	}

	api.free_device_list(devices, 1);
	return first_failure;
}

AccessoryRequestResult request_first_android_accessory(UsbApi &api, libusb_context *context)
{
	libusb_device **devices = nullptr;
	const libusb_ssize_t count = api.get_device_list(context, &devices);
	if (count < 0)
		return {};

	AccessoryRequestResult best_result = {};
	for (libusb_ssize_t index = 0; index < count; ++index) {
		const AccessoryRequestResult result = request_accessory_mode(api, devices[index]);
		if (result.status == AccessoryRequestStatus::Requested) {
			best_result = result;
			break;
		}
		if (result.status != AccessoryRequestStatus::NotSupported)
			best_result = result;
	}

	api.free_device_list(devices, 1);
	return best_result;
}

class BulkReader {
public:
	BulkReader(UsbApi &api_, AccessoryConnection &connection_, const std::atomic<bool> &stop_requested_)
		: api(api_),
		  connection(connection_),
		  stop_requested(stop_requested_)
	{
		buffer.reserve(BULK_READ_BUFFER_SIZE);
		read_buffer.resize(BULK_READ_BUFFER_SIZE);
	}

	int read_exact(unsigned char *output, size_t size)
	{
		size_t copied = 0;
		while (copied < size && !stop_requested.load()) {
			const size_t available = buffer.size() - buffer_offset;
			if (available == 0) {
				buffer.clear();
				buffer_offset = 0;
				const int fill_result = fill();
				if (fill_result == LIBUSB_ERROR_TIMEOUT)
					continue;
				if (fill_result < 0)
					return fill_result;
				continue;
			}

			const size_t count = std::min(available, size - copied);
			std::memcpy(output + copied, buffer.data() + buffer_offset, count);
			buffer_offset += count;
			copied += count;

			if (buffer_offset == buffer.size()) {
				buffer.clear();
				buffer_offset = 0;
			}
		}
		return stop_requested.load() ? -1 : 0;
	}

private:
	UsbApi &api;
	AccessoryConnection &connection;
	const std::atomic<bool> &stop_requested;
	std::vector<unsigned char> buffer;
	std::vector<unsigned char> read_buffer;
	size_t buffer_offset = 0;
	std::chrono::steady_clock::time_point last_data_at = std::chrono::steady_clock::now();
	bool received_data = false;
	bool wait_warning_logged = false;

	int fill()
	{
		int transferred = 0;
		const int result = api.bulk_transfer(connection.handle, connection.endpoint_in, read_buffer.data(),
						     static_cast<int>(read_buffer.size()), &transferred,
						     BULK_TIMEOUT_MS);
		if (result == LIBUSB_ERROR_TIMEOUT) {
			log_wait_warning();
			return result;
		}
		if (result < 0)
			return result;
		if (transferred > 0) {
			buffer.assign(read_buffer.begin(), read_buffer.begin() + transferred);
			last_data_at = std::chrono::steady_clock::now();
			received_data = true;
			wait_warning_logged = false;
		}
		return result;
	}

	void log_wait_warning()
	{
		if (wait_warning_logged ||
		    std::chrono::steady_clock::now() - last_data_at < std::chrono::milliseconds(NO_DATA_WARNING_MS)) {
			return;
		}

		if (received_data) {
			obs_log(LOG_WARNING, "[CaCam USB] No video data for 5 seconds; waiting for the phone");
		} else {
			obs_log(LOG_WARNING,
				"[CaCam USB] Accessory opened but CaCam sent no data. Unlock the phone and keep CaCam in "
				"the foreground.");
		}
		wait_warning_logged = true;
	}
};

int read_header(BulkReader &reader, unsigned char *header, size_t size)
{
	if (size < 4)
		return -1;

	const unsigned char magic[] = {'C', 'C', 'A', 'M'};
	size_t matched = 0;
	while (matched < sizeof(magic)) {
		unsigned char byte = 0;
		const int result = reader.read_exact(&byte, 1);
		if (result < 0)
			return result;

		if (byte == magic[matched]) {
			header[matched++] = byte;
		} else if (byte == magic[0]) {
			header[0] = byte;
			matched = 1;
		} else {
			matched = 0;
		}
	}

	return reader.read_exact(header + sizeof(magic), size - sizeof(magic));
}

struct SourceSettings {
	CacamConnectionMode connection_mode = CacamConnectionMode::Usb;
	CacamQuality quality = CacamQuality::Standard;
	bool bgobs_optimized = true;
	bool auto_connect = true;
	std::string adb_path = default_adb_path();
	std::string adb_serial;
	std::string package_name = CACAM_PACKAGE_NAME;
	int adb_local_port = ADB_LOCAL_PORT_DEFAULT;
	int adb_device_port = ADB_DEVICE_PORT_DEFAULT;

	bool operator==(const SourceSettings &) const = default;
};

class CacamUsbSource {
public:
	CacamUsbSource(obs_data_t *settings, obs_source_t *source_) : source(source_)
	{
		obs_source_set_async_unbuffered(source, true);
		update(settings);
	}

	~CacamUsbSource() { stop(); }

	void update(obs_data_t *settings)
	{
		SourceSettings next_settings = read_settings(settings);
		verbose_logging.store(obs_data_get_bool(settings, "verbose_logging"));

		bool should_restart = false;
		{
			std::lock_guard<std::mutex> guard(settings_mutex);
			should_restart = !(next_settings == current_settings);
			current_settings = next_settings;
		}

		if (should_restart)
			stop();

		if (next_settings.auto_connect) {
			log_verbose("[CaCam] Auto-connect enabled (%s)", connection_mode_setting(next_settings.connection_mode));
			start();
		} else {
			log_verbose("[CaCam] Auto-connect disabled");
			stop();
		}
	}

	void activate()
	{
		if (settings_snapshot().auto_connect) {
			log_verbose("[CaCam] Source activated");
			start();
		}
	}

	void deactivate()
	{
		// Auto-connect is a device connection policy, not a scene visibility policy.
		// Keeping the worker alive also avoids losing AOA negotiation while OBS
		// temporarily deactivates sources during scene loading and Studio Mode.
		if (!settings_snapshot().auto_connect)
			stop();
	}

	uint32_t get_width() const { return source_canvas_size().first; }
	uint32_t get_height() const { return source_canvas_size().second; }

private:
	obs_source_t *source = nullptr;
	std::thread worker;
	std::mutex worker_mutex;
	mutable std::mutex settings_mutex;
	std::atomic<bool> stop_requested{false};
	std::atomic<bool> verbose_logging{false};
	std::array<std::vector<unsigned char>, 3> video_buffers;
	size_t video_buffer_index = 0;
	SourceSettings current_settings;

	static SourceSettings read_settings(obs_data_t *settings)
	{
		SourceSettings value;
		value.connection_mode = parse_connection_mode(obs_data_get_string(settings, "connection_mode"));
		value.quality = parse_quality(obs_data_get_string(settings, "quality"));
		value.bgobs_optimized = obs_data_get_bool(settings, "bgobs_optimized");
		value.auto_connect = obs_data_get_bool(settings, "auto_connect");
		value.adb_path = obs_data_string(settings, "adb_path");
		if (value.adb_path.empty())
			value.adb_path = default_adb_path();
		value.adb_serial = obs_data_string(settings, "adb_serial");
		value.package_name = obs_data_string(settings, "adb_package_name");
		if (value.package_name.empty())
			value.package_name = CACAM_PACKAGE_NAME;
		value.adb_local_port = static_cast<int>(obs_data_get_int(settings, "adb_local_port"));
		if (value.adb_local_port <= 0)
			value.adb_local_port = ADB_LOCAL_PORT_DEFAULT;
		value.adb_device_port = static_cast<int>(obs_data_get_int(settings, "adb_device_port"));
		if (value.adb_device_port <= 0)
			value.adb_device_port = ADB_DEVICE_PORT_DEFAULT;
		return value;
	}

	SourceSettings settings_snapshot() const
	{
		std::lock_guard<std::mutex> guard(settings_mutex);
		return current_settings;
	}

	void log_verbose(const char *format, ...) const
	{
		if (!verbose_logging.load())
			return;

		va_list args;
		va_start(args, format);
		bgobs_logv(LOG_INFO, format, args);
		va_end(args);
	}

	void start()
	{
		std::lock_guard<std::mutex> guard(worker_mutex);
		if (worker.joinable())
			return;
		const SourceSettings settings = settings_snapshot();
		stop_requested.store(false);
		log_verbose("[CaCam] Starting %s worker", connection_mode_setting(settings.connection_mode));
		worker = std::thread(&CacamUsbSource::worker_loop, this, settings);
	}

	void stop()
	{
		std::lock_guard<std::mutex> guard(worker_mutex);
		stop_requested.store(true);
		if (worker.joinable()) {
			log_verbose("[CaCam] Stopping worker");
			worker.join();
		}
	}

	bool prepare_adb_device(const SourceSettings &settings, std::string &serial)
	{
		const std::string adb_path = settings.adb_path.empty() ? default_adb_path() : settings.adb_path;
		int exit_code = 0;
		run_command_capture(adb_command(adb_path, "", "start-server"), &exit_code);
		if (exit_code != 0)
			log_verbose("[CaCam ADB] adb start-server returned %d", exit_code);

		const std::string devices_output = run_command_capture(adb_command(adb_path, "", "devices"), &exit_code);
		if (exit_code != 0) {
			obs_log(LOG_ERROR, "[CaCam ADB] Cannot list ADB devices with %s", adb_path.c_str());
			return false;
		}

		const std::vector<std::string> serials = adb_device_serials(devices_output);
		if (!settings.adb_serial.empty()) {
			serial = settings.adb_serial;
			if (std::find(serials.begin(), serials.end(), serial) == serials.end()) {
				obs_log(LOG_WARNING, "[CaCam ADB] Configured device %s is not currently listed as authorized",
					serial.c_str());
				if (serials.empty()) {
					obs_log(LOG_WARNING,
						"[CaCam ADB] No authorized ADB phone. Unlock the phone and accept USB debugging.");
					return false;
				}
				if (serials.size() > 1) {
					obs_log(LOG_WARNING,
						"[CaCam ADB] Multiple ADB phones are authorized; set the serial in source properties.");
					return false;
				}
				serial = serials.front();
				obs_log(LOG_INFO, "[CaCam ADB] Falling back to the only authorized ADB phone: %s",
					serial.c_str());
			}
		} else {
			if (serials.empty()) {
				obs_log(LOG_WARNING, "[CaCam ADB] No authorized ADB phone. Unlock the phone and accept USB debugging.");
				return false;
			}
			if (serials.size() > 1) {
				obs_log(LOG_WARNING,
					"[CaCam ADB] Multiple ADB phones are authorized; using %s. Set the serial in source properties if needed.",
					serials.front().c_str());
			}
			serial = serials.front();
		}

		const std::string local_port = std::to_string(settings.adb_local_port);
		run_command_capture(adb_command(adb_path, serial, "forward --remove tcp:" + local_port), &exit_code);

		const std::string forward_arguments =
			"forward tcp:" + local_port + " tcp:" + std::to_string(settings.adb_device_port);
		const std::string forward_output = run_command_capture(adb_command(adb_path, serial, forward_arguments), &exit_code);
		if (exit_code != 0) {
			obs_log(LOG_ERROR, "[CaCam ADB] Cannot create ADB forward tcp:%d -> tcp:%d: %s",
				settings.adb_local_port, settings.adb_device_port, forward_output.c_str());
			return false;
		}

		const CacamQualityInfo &info = quality_info(settings.quality);
		const std::string component = settings.package_name + "/.MainActivity";
		const std::string launch_arguments =
			"shell am start --activity-single-top -n " + shell_quote(component) +
			" --es connection ADB --es quality " + shell_quote(info.intent_value) +
			" --ei fps " + std::to_string(info.fps) +
			" --ez bgobs_optimized " + (settings.bgobs_optimized ? "true" : "false") +
			" --ez start_stream true";
		const std::string launch_output = run_command_capture(adb_command(adb_path, serial, launch_arguments), &exit_code);
		if (exit_code != 0) {
			obs_log(LOG_ERROR, "[CaCam ADB] Cannot launch CaCam on %s: %s", serial.c_str(), launch_output.c_str());
			return false;
		}

		log_verbose("[CaCam ADB] Forward ready on 127.0.0.1:%d for %s %s", settings.adb_local_port,
			    serial.c_str(), info.setting);
		return true;
	}

	bool wait_for_adb_health(const SourceSettings &settings)
	{
		const std::string health_url =
			"http://127.0.0.1:" + std::to_string(settings.adb_local_port) + "/health";
		std::vector<unsigned char> response;
		for (int attempt = 0; attempt < ADB_HEALTH_RETRY_COUNT && !stop_requested.load(); ++attempt) {
			if (http_get_binary(health_url, ADB_HEALTH_TIMEOUT_MS, response)) {
				const std::string body(response.begin(), response.end());
				if (body.find("ok") != std::string::npos)
					return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
		return false;
	}

	void remove_adb_forward(const SourceSettings &settings, const std::string &serial)
	{
		if (serial.empty())
			return;
		const std::string adb_path = settings.adb_path.empty() ? default_adb_path() : settings.adb_path;
		int exit_code = 0;
		run_command_capture(adb_command(adb_path, serial,
						"forward --remove tcp:" + std::to_string(settings.adb_local_port)),
				    &exit_code);
		UNUSED_PARAMETER(exit_code);
	}

	void adb_worker_loop(const SourceSettings &settings)
	{
		curl_global_init(CURL_GLOBAL_DEFAULT);
		std::string serial;
		while (!stop_requested.load()) {
			if (!prepare_adb_device(settings, serial)) {
				sleep_retry();
				continue;
			}

			if (!wait_for_adb_health(settings)) {
				obs_log(LOG_WARNING, "[CaCam ADB] CaCam did not answer on 127.0.0.1:%d",
					settings.adb_local_port);
				remove_adb_forward(settings, serial);
				sleep_retry();
				continue;
			}

			read_adb_snapshots(settings);
			remove_adb_forward(settings, serial);
			sleep_retry();
		}
	}

	void read_adb_snapshots(const SourceSettings &settings)
	{
		const CacamQualityInfo &info = quality_info(settings.quality);
		const std::string snapshot_url =
			"http://127.0.0.1:" + std::to_string(settings.adb_local_port) + "/snapshot.jpg";
		const auto frame_interval = std::chrono::milliseconds(1000 / std::max(1, info.fps));
		std::vector<unsigned char> jpeg;
		bool first_frame_logged = false;
		bool warning_logged = false;
		int consecutive_failures = 0;
		while (!stop_requested.load()) {
			const auto frame_started_at = std::chrono::steady_clock::now();
			if (http_get_binary(snapshot_url, ADB_SNAPSHOT_TIMEOUT_MS, jpeg) && output_jpeg(jpeg)) {
				if (!first_frame_logged) {
					log_verbose("[CaCam ADB] First snapshot frame received");
					first_frame_logged = true;
				}
				warning_logged = false;
				consecutive_failures = 0;
			} else if (!warning_logged) {
				obs_log(LOG_WARNING, "[CaCam ADB] Waiting for snapshots from %s", snapshot_url.c_str());
				warning_logged = true;
				++consecutive_failures;
			} else {
				++consecutive_failures;
			}

			if (consecutive_failures >= 30) {
				obs_log(LOG_WARNING, "[CaCam ADB] Snapshot stream lost; reconnecting");
				return;
			}

			const auto elapsed = std::chrono::steady_clock::now() - frame_started_at;
			if (elapsed < frame_interval)
				std::this_thread::sleep_for(frame_interval - elapsed);
		}
	}

	void worker_loop(SourceSettings settings)
	{
		if (settings.connection_mode == CacamConnectionMode::Adb) {
			adb_worker_loop(settings);
			return;
		}

		UsbApi api;
		if (!api.load()) {
			obs_log(LOG_ERROR, "[CaCam USB] libusb runtime not found");
			return;
		}

		libusb_context *context = nullptr;
		if (api.init(&context) != 0 || !context) {
			obs_log(LOG_ERROR, "[CaCam USB] libusb initialization failed");
			return;
		}

		AccessoryRequestResult last_request = {};
		bool has_last_request = false;
		AccessoryOpenResult last_open = {};
		bool has_last_open = false;
		while (!stop_requested.load()) {
			AccessoryConnection connection;
			const AccessoryOpenResult open_result = open_accessory(api, context, connection);
			if (open_result.status != AccessoryOpenStatus::Opened) {
				const bool open_changed = !has_last_open || open_result.status != last_open.status ||
							 open_result.vendor != last_open.vendor ||
							 open_result.product != last_open.product ||
							 open_result.interface_number != last_open.interface_number ||
							 open_result.error != last_open.error ||
							 open_result.saw_adb_interface != last_open.saw_adb_interface;
				if (open_changed) {
					switch (open_result.status) {
					case AccessoryOpenStatus::OpenFailed:
						obs_log(LOG_WARNING,
							"[CaCam USB] Android Open Accessory device %04x:%04x is not "
							"accessible: %s (%d). On Windows, verify the WinUSB driver.",
							open_result.vendor, open_result.product,
							usb_error_name(api, open_result.error), open_result.error);
						break;
					case AccessoryOpenStatus::AccessoryInterfaceInaccessible:
						obs_log(LOG_WARNING,
							"[CaCam USB] Android Open Accessory interface %d on %04x:%04x "
							"is present but not accessible: %s (%d). Install a WinUSB "
							"driver for the accessory interface. The ADB interface is not "
							"the CaCam video stream.",
							open_result.interface_number, open_result.vendor,
							open_result.product, usb_error_name(api, open_result.error),
							open_result.error);
						break;
					case AccessoryOpenStatus::NotFound:
						if (open_result.saw_adb_interface)
							obs_log(LOG_WARNING,
								"[CaCam USB] Only the Android ADB interface is accessible. "
								"Install a WinUSB driver for the Android Accessory "
								"interface; ADB does not carry the CaCam video stream.");
						break;
					case AccessoryOpenStatus::Opened:
						break;
					}
				}
				last_open = open_result;
				has_last_open = true;

				if (open_result.status != AccessoryOpenStatus::NotFound || open_result.saw_adb_interface) {
					sleep_retry();
					continue;
				}

				const AccessoryRequestResult request = request_first_android_accessory(api, context);
				const bool request_changed = !has_last_request || request.status != last_request.status ||
							     request.vendor != last_request.vendor ||
							     request.product != last_request.product ||
							     request.protocol != last_request.protocol ||
							     request.error != last_request.error;
				if (request_changed)
					switch (request.status) {
					case AccessoryRequestStatus::Requested:
						log_verbose(
							"[CaCam USB] Requested Android Open Accessory mode for %04x:%04x (AOA %u)",
							request.vendor, request.product, request.protocol);
						break;
					case AccessoryRequestStatus::OpenFailed:
						obs_log(LOG_WARNING,
							"[CaCam USB] Android device %04x:%04x is not accessible: %s (%d). "
							"On Windows, verify the WinUSB driver.",
							request.vendor, request.product,
							usb_error_name(api, request.error), request.error);
						break;
					case AccessoryRequestStatus::StartFailed:
						obs_log(LOG_WARNING,
							"[CaCam USB] AOA negotiation failed for %04x:%04x: %s (%d)",
							request.vendor, request.product,
							usb_error_name(api, request.error), request.error);
						break;
					case AccessoryRequestStatus::NotSupported:
						log_verbose("[CaCam USB] No Android Open Accessory device is visible to libusb. "
							    "On Windows, verify the phone uses a WinUSB-accessible interface; "
							    "MTP/WPD devices cannot be switched to accessory mode by BGOBS.");
						break;
					}
				last_request = request;
				has_last_request = true;
				sleep_retry();
				continue;
			}

			last_open = {};
			has_last_open = false;
			last_request = {};
			has_last_request = false;
			log_verbose("[CaCam USB] Accessory interface opened");
			read_stream(api, connection);
			connection.close(api);
			log_verbose("[CaCam USB] Disconnected");
			sleep_retry();
		}

		api.exit(context);
	}

	void sleep_retry()
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(RETRY_DELAY_MS);
		while (!stop_requested.load() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	void read_stream(UsbApi &api, AccessoryConnection &connection)
	{
		std::vector<unsigned char> header(CACAM_HEADER_SIZE);
		BulkReader reader(api, connection, stop_requested);
		bool first_frame_logged = false;
		int64_t minimum_clock_offset_us = INT64_MAX;
		uint64_t dropped_frames = 0;
		while (!stop_requested.load()) {
			const int header_result = read_header(reader, header.data(), header.size());
			if (header_result < 0)
				break;

			const uint32_t magic = read_u32_be(header.data());
			const uint8_t version = header[4];
			const uint8_t type = header[5];
			const uint32_t payload_size = read_u32_be(header.data() + 8);
			const uint64_t timestamp_us = read_u64_be(header.data() + 12);

			if (magic != CACAM_MAGIC || version != CACAM_PROTOCOL_VERSION ||
			    payload_size > MAX_PAYLOAD_SIZE) {
				obs_log(LOG_WARNING, "[CaCam USB] Invalid frame header");
				continue;
			}

			std::vector<unsigned char> payload(payload_size);
			if (payload_size > 0 && reader.read_exact(payload.data(), payload.size()) < 0) {
				break;
			}

			if (type == CACAM_TYPE_HELLO) {
				const std::string text(payload.begin(), payload.end());
				log_verbose("[CaCam USB] Connected: %s", text.c_str());
			} else if (type == CACAM_TYPE_NV21) {
				const int64_t clock_offset_us = static_cast<int64_t>(os_gettime_ns() / 1000) -
								static_cast<int64_t>(timestamp_us);
				if (minimum_clock_offset_us == INT64_MAX || clock_offset_us < minimum_clock_offset_us) {
					minimum_clock_offset_us = clock_offset_us;
				} else {
					minimum_clock_offset_us += std::min(clock_offset_us - minimum_clock_offset_us,
									    MAX_CLOCK_BASELINE_STEP_US);
				}
				const int64_t transport_backlog_us =
					std::max<int64_t>(0, clock_offset_us - minimum_clock_offset_us);
				int64_t sender_queue_us = 0;
				if (payload.size() >= CACAM_FRAME_METADATA_SIZE) {
					const uint64_t frame_timestamp_us = read_u64_be(payload.data() + 8);
					if (timestamp_us >= frame_timestamp_us &&
					    timestamp_us - frame_timestamp_us <= INT64_MAX)
						sender_queue_us =
							static_cast<int64_t>(timestamp_us - frame_timestamp_us);
				}
				const int64_t frame_age_us = sender_queue_us + transport_backlog_us;
				if (frame_age_us > MAX_FRAME_AGE_US) {
					++dropped_frames;
					if (dropped_frames == 1 || dropped_frames % 60 == 0) {
						obs_log(LOG_WARNING,
							"[CaCam USB] Dropping stale frame (%lld ms sender, %lld ms transport, %llu dropped)",
							static_cast<long long>(sender_queue_us / 1000),
							static_cast<long long>(transport_backlog_us / 1000),
							static_cast<unsigned long long>(dropped_frames));
					}
					continue;
				}
				if (output_nv21(payload, timestamp_us) && !first_frame_logged) {
					log_verbose("[CaCam USB] First video frame: %ux%u (%lld ms sender queue)",
						read_u32_be(payload.data()), read_u32_be(payload.data() + 4),
						static_cast<long long>(sender_queue_us / 1000));
					first_frame_logged = true;
				}
			}
		}
	}

	bool output_jpeg(const std::vector<unsigned char> &jpeg)
	{
		if (jpeg.empty())
			return false;

		const cv::Mat encoded(1, static_cast<int>(jpeg.size()), CV_8UC1, const_cast<unsigned char *>(jpeg.data()));
		const cv::Mat bgr = cv::imdecode(encoded, cv::IMREAD_COLOR);
		if (bgr.empty())
			return false;

		cv::Mat bgra;
		cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
		return output_bgra_frame(bgra);
	}

	bool output_nv21(const std::vector<unsigned char> &payload, uint64_t message_timestamp_us)
	{
		if (payload.size() < CACAM_FRAME_METADATA_SIZE)
			return false;

		const uint32_t frame_width = read_u32_be(payload.data());
		const uint32_t frame_height = read_u32_be(payload.data() + 4);
		const uint64_t frame_timestamp_us = read_u64_be(payload.data() + 8);
		if (frame_width == 0 || frame_height == 0 || frame_width > 4096 || frame_height > 4096)
			return false;

		const size_t expected_size =
			static_cast<size_t>(frame_width) * static_cast<size_t>(frame_height) * 3 / 2;
		if (payload.size() - CACAM_FRAME_METADATA_SIZE < expected_size)
			return false;

		auto *frame_data = const_cast<unsigned char *>(payload.data() + CACAM_FRAME_METADATA_SIZE);
		const cv::Mat nv21(static_cast<int>(frame_height + frame_height / 2), static_cast<int>(frame_width),
				   CV_8UC1, frame_data);
		cv::Mat bgra;
		cv::cvtColor(nv21, bgra, cv::COLOR_YUV2BGRA_NV21);
		UNUSED_PARAMETER(message_timestamp_us);
		UNUSED_PARAMETER(frame_timestamp_us);
		return output_bgra_frame(bgra);
	}

	bool output_bgra_frame(const cv::Mat &bgra)
	{
		if (bgra.empty())
			return false;

		const auto [canvas_width, canvas_height] = source_canvas_size();
		auto &video_buffer = video_buffers[video_buffer_index];
		video_buffer_index = (video_buffer_index + 1) % video_buffers.size();
		video_buffer.resize(static_cast<size_t>(canvas_width) * static_cast<size_t>(canvas_height) * 4);

		cv::Mat output(static_cast<int>(canvas_height), static_cast<int>(canvas_width), CV_8UC4, video_buffer.data());
		output.setTo(cv::Scalar(0, 0, 0, 255));

		const double scale =
			std::min(static_cast<double>(canvas_width) / static_cast<double>(bgra.cols),
				 static_cast<double>(canvas_height) / static_cast<double>(bgra.rows));
		const int scaled_width = std::max(1, static_cast<int>(static_cast<double>(bgra.cols) * scale));
		const int scaled_height = std::max(1, static_cast<int>(static_cast<double>(bgra.rows) * scale));
		const int x = (static_cast<int>(canvas_width) - scaled_width) / 2;
		const int y = (static_cast<int>(canvas_height) - scaled_height) / 2;
		cv::Mat roi = output(cv::Rect(x, y, scaled_width, scaled_height));
		cv::resize(bgra, roi, roi.size(), 0, 0, cv::INTER_AREA);

		obs_source_frame frame = {};
		frame.format = VIDEO_FORMAT_BGRA;
		frame.width = canvas_width;
		frame.height = canvas_height;
		frame.data[0] = output.data;
		frame.linesize[0] = static_cast<uint32_t>(output.step[0]);
		frame.timestamp = os_gettime_ns();
		obs_source_output_video(source, &frame);
		return true;
	}
};

std::string localized_text_with_version(const char *key)
{
	std::string text = obs_module_text(key);
	const std::string placeholder = "%1";
	const size_t position = text.find(placeholder);
	if (position != std::string::npos)
		text.replace(position, placeholder.size(), PLUGIN_VERSION);
	return text;
}

} // namespace

extern "C" const char *cacam_usb_source_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("CaCamUsbSource");
}

extern "C" void *cacam_usb_source_create(obs_data_t *settings, obs_source_t *source)
{
	return new CacamUsbSource(settings, source);
}

extern "C" void cacam_usb_source_destroy(void *data)
{
	delete static_cast<CacamUsbSource *>(data);
}

extern "C" uint32_t cacam_usb_source_get_width(void *data)
{
	const auto *source = static_cast<CacamUsbSource *>(data);
	return source ? source->get_width() : source_canvas_size().first;
}

extern "C" uint32_t cacam_usb_source_get_height(void *data)
{
	const auto *source = static_cast<CacamUsbSource *>(data);
	return source ? source->get_height() : source_canvas_size().second;
}

extern "C" void cacam_usb_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "connection_mode", "usb");
	obs_data_set_default_string(settings, "quality", "standard");
	obs_data_set_default_bool(settings, "bgobs_optimized", true);
	obs_data_set_default_string(settings, "adb_path", default_adb_path().c_str());
	obs_data_set_default_string(settings, "adb_serial", "");
	obs_data_set_default_string(settings, "adb_package_name", CACAM_PACKAGE_NAME);
	obs_data_set_default_int(settings, "adb_local_port", ADB_LOCAL_PORT_DEFAULT);
	obs_data_set_default_int(settings, "adb_device_port", ADB_DEVICE_PORT_DEFAULT);
	obs_data_set_default_bool(settings, "auto_connect", true);
	obs_data_set_default_bool(settings, "verbose_logging", false);
}

extern "C" obs_properties_t *cacam_usb_source_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *properties = obs_properties_create();
	const std::string version_info = localized_text_with_version("CaCamUsbVersionInfo");
	obs_properties_add_text(properties, "bgobs_version_info", version_info.c_str(), OBS_TEXT_INFO);

	obs_property_t *connection = obs_properties_add_list(properties, "connection_mode",
							     obs_module_text("CaCamConnectionMode"),
							     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(connection, obs_module_text("CaCamConnectionUsb"), "usb");
	obs_property_list_add_string(connection, obs_module_text("CaCamConnectionAdb"), "adb");

	obs_property_t *quality = obs_properties_add_list(properties, "quality", obs_module_text("CaCamQuality"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(quality, obs_module_text("CaCamQualityPourri"), "pourri");
	obs_property_list_add_string(quality, obs_module_text("CaCamQualityLow"), "low");
	obs_property_list_add_string(quality, obs_module_text("CaCamQualityStandard"), "standard");
	obs_property_list_add_string(quality, obs_module_text("CaCamQualityHd"), "hd");
	obs_property_list_add_string(quality, obs_module_text("CaCamQualityUhd"), "uhd");

	obs_properties_add_bool(properties, "bgobs_optimized", obs_module_text("CaCamBgobsOptimized"));
	obs_properties_add_text(properties, "adb_path", obs_module_text("CaCamAdbPath"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(properties, "adb_serial", obs_module_text("CaCamAdbSerial"), OBS_TEXT_DEFAULT);
	obs_properties_add_int(properties, "adb_local_port", obs_module_text("CaCamAdbLocalPort"), 1024, 65535, 1);
	obs_properties_add_int(properties, "adb_device_port", obs_module_text("CaCamAdbDevicePort"), 1024, 65535, 1);
	obs_properties_add_text(properties, "adb_package_name", obs_module_text("CaCamAdbPackageName"), OBS_TEXT_DEFAULT);
	obs_properties_add_bool(properties, "auto_connect", obs_module_text("CaCamUsbAutoConnect"));
	obs_properties_add_bool(properties, "verbose_logging", obs_module_text("CaCamUsbVerboseLogging"));
	return properties;
}

extern "C" void cacam_usb_source_update(void *data, obs_data_t *settings)
{
	auto *source = static_cast<CacamUsbSource *>(data);
	if (source)
		source->update(settings);
}

extern "C" void cacam_usb_source_activate(void *data)
{
	auto *source = static_cast<CacamUsbSource *>(data);
	if (source)
		source->activate();
}

extern "C" void cacam_usb_source_deactivate(void *data)
{
	auto *source = static_cast<CacamUsbSource *>(data);
	if (source)
		source->deactivate();
}
